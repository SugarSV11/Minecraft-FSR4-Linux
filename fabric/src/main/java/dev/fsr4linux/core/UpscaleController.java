package dev.fsr4linux.core;

import java.util.ArrayList;
import java.util.EnumMap;
import java.util.List;
import java.util.Map;
import java.util.Queue;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.function.Consumer;

/**
 * Owns the user-visible upscaling state and applies changes safely.
 *
 * <p>Key handlers only <em>request</em> changes. They are applied in {@link #onFrameBoundary} on the render
 * thread, after {@link GpuSync#waitForGpuIdle()}, so no GPU resource that is still in flight is replaced.
 * If anything fails the previous configuration is restored (rollback) and the cause is reported.
 * While a transition is running ({@code busy}) further requests are rejected, never queued blindly.
 */
public final class UpscaleController {
	private enum Request { TOGGLE, CYCLE_QUALITY, CYCLE_BACKEND, RESET_HISTORY, RESIZE }

	private final Map<BackendId, UpscalerBackend> backends = new EnumMap<>(BackendId.class);
	private final Fsr4Config config;
	private final Consumer<Notification> sink;
	private final Consumer<Fsr4Config> persist;
	private final Queue<Request> requests = new ConcurrentLinkedQueue<>();
	private volatile boolean busy;           // true while initialising/destroying/switching
	private volatile boolean rendererReady;  // false before the renderer finished init / after destruction began
	private int outW, outH;
	private UpscalerBackend active;          // initialised backend currently used, or null
	private boolean needsResize;

	public UpscaleController(Fsr4Config config, List<UpscalerBackend> all, Consumer<Notification> sink, Consumer<Fsr4Config> persist) {
		this.config = config;
		this.sink = sink;
		this.persist = persist;
		for (UpscalerBackend b : all) backends.put(b.id(), b);
	}

	// ---- requests (any thread, e.g. key handlers) ------------------------------------------------------------
	public void requestToggle() { requests.add(Request.TOGGLE); }
	public void requestCycleQuality() { requests.add(Request.CYCLE_QUALITY); }
	public void requestCycleBackend() { requests.add(Request.CYCLE_BACKEND); }
	public void requestResetHistory() { requests.add(Request.RESET_HISTORY); }
	public void requestResize(int w, int h) { outW = w; outH = h; needsResize = true; requests.add(Request.RESIZE); }

	public void setRendererReady(boolean ready) { rendererReady = ready; }
	public boolean isBusy() { return busy; }
	public boolean isActive() { return config.enabled && active != null && active.status().state() == BackendState.READY; }
	public UpscalerBackend activeBackend() { return active; }
	public Fsr4Config config() { return config; }
	public UpscalerBackend backend(BackendId id) { return backends.get(id); }

	// ---- frame-boundary application (render thread) ---------------------------------------------------------
	public void onFrameBoundary(GpuSync gpu) {
		Request r;
		while ((r = requests.poll()) != null) {
			if (!rendererReady || busy) {
				// "No permitir cambios inseguros durante la inicializacion o destruccion del renderizador"
				if (r != Request.RESIZE) sink.accept(new Notification("FSR: RENDERIZADOR OCUPADO, CAMBIO IGNORADO", true));
				continue;
			}
			busy = true;
			try { apply(r, gpu); } finally { busy = false; }
		}
	}

	private void apply(Request r, GpuSync gpu) {
		Fsr4Config before = snapshot();
		try {
			switch (r) {
				case TOGGLE -> toggle(gpu);
				case CYCLE_QUALITY -> cycleQuality(gpu);
				case CYCLE_BACKEND -> cycleBackend(gpu);
				case RESET_HISTORY -> { if (active != null) active.resetHistory(); sink.accept(new Notification("FSR: HISTORIAL REINICIADO", false)); }
				case RESIZE -> { if (needsResize && config.enabled) recreate(gpu, config.backendId(), config.qualityMode()); needsResize = false; }
			}
			persist.accept(config);
		} catch (BackendException e) {
			restore(before, gpu, e);
		}
	}

	private void toggle(GpuSync gpu) throws BackendException {
		if (config.enabled) {
			shutdownActive(gpu);
			config.enabled = false;
			sink.accept(new Notification("FSR DESACTIVADO", false));
		} else {
			BackendId id = config.backendId() == BackendId.NATIVE ? firstUsableFsr() : config.backendId();
			if (id == BackendId.FSR4_1_1 && !backends.get(id).probe().available()
				&& backends.get(BackendId.FSR3_1_5).probe().available()) id = BackendId.FSR3_1_5;
			if (id == null) {
				sink.accept(new Notification("FSR NO DISPONIBLE: " + anyCause(), true));
				return;
			}
			try { recreate(gpu, id, config.qualityMode()); }
			catch (BackendException e) {
				if (id != BackendId.FSR4_1_1 || !backends.get(BackendId.FSR3_1_5).probe().available()) throw e;
				id = BackendId.FSR3_1_5;
				recreate(gpu, id, config.qualityMode());
				sink.accept(new Notification("FSR 4.1.1 NO DISPONIBLE (" + e.getMessage() + "); usando FSR 3.1.5", true));
			}
			config.enabled = true;
			config.backend = id.name();
			sink.accept(new Notification("FSR ACTIVANDO... | " + id.label + " | " + config.qualityMode().label, false));
		}
	}

	private void cycleQuality(GpuSync gpu) throws BackendException {
		UpscalerBackend b = backends.get(config.backendId());
		var supported = b == null ? java.util.EnumSet.allOf(QualityMode.class) : b.supportedQualities();   // NATIVE (no backend yet): all modes can be pre-selected
		QualityMode[] all = QualityMode.values();
		QualityMode cur = config.qualityMode();
		QualityMode next = null;
		for (int i = 1; i <= all.length; i++) {
			QualityMode c = all[(cur.ordinal() + i) % all.length];
			if (supported.contains(c)) { next = c; break; }   // "Volver al primer modo compatible" by wrap-around
		}
		if (next == null || next == cur && !supported.contains(cur)) {
			sink.accept(new Notification("FSR: SIN MODOS DE CALIDAD COMPATIBLES", true));
			return;
		}
		if (config.enabled) recreate(gpu, config.backendId(), next);
		config.quality = next.name();
		sink.accept(new Notification("FSR CALIDAD: " + next.label, false));
	}

	private void cycleBackend(GpuSync gpu) throws BackendException {
		BackendId[] order = {BackendId.FSR3_1_5, BackendId.FSR4_1_1};
		BackendId cur = config.backendId();
		int start = cur == BackendId.FSR3_1_5 ? 1 : 0;   // next after the current one; NATIVE starts at FSR 3.1.5
		for (int i = 0; i < order.length; i++) {
			BackendId cand = order[(start + i) % order.length];
			if (cand == cur) continue;
			BackendStatus s = backends.get(cand).probe();
			if (!s.available()) {
				// keep current backend, explain why
				sink.accept(new Notification(cand.label + " NO DISPONIBLE" + (s.failureCause() == null ? "" : " (" + s.failureCause() + ")"), true));
				continue;
			}
			QualityMode q = backends.get(cand).supportedQualities().contains(config.qualityMode()) ? config.qualityMode() : QualityMode.QUALITY;
			if (config.enabled) recreate(gpu, cand, q);
			config.backend = cand.name();
			config.quality = q.name();
			sink.accept(new Notification("FSR VERSIÓN: " + cand.label, false));
			return;
		}
	}

	// ---- helpers ----------------------------------------------------------------------------------------------
	/** Replace the active backend/size/quality after the GPU is idle. Bridge backends share one native runtime. */
	private void recreate(GpuSync gpu, BackendId id, QualityMode q) throws BackendException {
		UpscalerBackend next = backends.get(id);
		if (next == null) throw new BackendException("backend desconocido: " + id);
		BackendStatus st = next.probe();
		if (!st.available()) throw new BackendException(id.label + " no disponible: " + st.failureCause());
		gpu.waitForGpuIdle();
		if (next == active) {
			next.resize(outW, outH, q);
		} else {
			if (active != null) { active.shutdown(); active = null; }
			try { next.initialize(outW, outH, q); }
			catch (BackendException e) { next.shutdown(); throw e; }
			active = next;
		}
		active.resetHistory();   // backend, size or quality changed -> temporal history is invalid
	}

	private void shutdownActive(GpuSync gpu) {
		if (active != null) { gpu.waitForGpuIdle(); active.shutdown(); active = null; }
	}

	private void restore(Fsr4Config before, GpuSync gpu, BackendException cause) {
		// The failed backend must not stay half-initialised. Try to bring the previous configuration back.
		String msg = "FSR: ERROR (" + cause.getMessage() + ")";
		config.enabled = before.enabled; config.backend = before.backend; config.quality = before.quality;
		try {
			if (active != null && active.status().state() != BackendState.READY) { active.shutdown(); active = null; }
			if (config.enabled && active == null) recreate(gpu, config.backendId(), config.qualityMode());
			sink.accept(new Notification(msg + " - RESTAURADO " + config.backendId().label, true));
		} catch (BackendException again) {
			config.enabled = false;
			active = null;
			sink.accept(new Notification(msg + " - FSR DESACTIVADO", true));
		}
		persist.accept(config);
	}

	private Fsr4Config snapshot() {
		Fsr4Config c = new Fsr4Config();
		c.enabled = config.enabled; c.backend = config.backend; c.quality = config.quality;
		return c;
	}

	private BackendId firstUsableFsr() {
		for (BackendId id : new BackendId[]{BackendId.FSR4_1_1, BackendId.FSR3_1_5}) {
			UpscalerBackend b = backends.get(id);
			if (b != null && b.probe().available()) return id;
		}
		return null;
	}

	private String anyCause() {
		List<String> causes = new ArrayList<>();
		for (BackendId id : new BackendId[]{BackendId.FSR3_1_5, BackendId.FSR4_1_1}) {
			UpscalerBackend b = backends.get(id);
			if (b != null) causes.add(id.label + ": " + b.probe().failureCause());
		}
		return String.join("; ", causes);
	}

	/**
	 * The external upscaler failed while rendering (helper died, dispatch rejected...). FSR is switched off safely; backend and
	 * quality are kept so that the user can switch it on again, and the cause is shown. Must be called on the render thread.
	 */
	public void reportRuntimeFailure(GpuSync gpu, String cause) {
		busy = true;
		try {
			boolean fallback = active != null && active.id() == BackendId.FSR4_1_1
				&& ProviderFallback.from411To315(cause) && backends.get(BackendId.FSR3_1_5).probe().available();
			if (active != null) { gpu.waitForGpuIdle(); try { active.shutdown(); } catch (RuntimeException ignored) { } active = null; }
			config.enabled = false;
			if (fallback) { config.backend = BackendId.FSR3_1_5.name(); requests.add(Request.TOGGLE); }
			persist.accept(config);
			sink.accept(new Notification(fallback ? "FSR 4.1.1: FALLO (" + cause + "); intentando FSR 3.1.5"
				: "FSR: FALLO (" + cause + ") - FSR DESACTIVADO", true));
		} finally { busy = false; }
	}

	/**
	 * Label that may be shown as "running". Only returns a value when the active backend has really executed
	 * an implementation whose id was reported by the runtime; otherwise null (never a silent fallback label).
	 */
	public String runningLabel() {
		if (!isActive()) return null;
		BackendStatus s = active.status();
		return s.executedImplementationId() == null ? null : active.id().label + " [" + s.executedImplementationId() + "]";
	}
}
