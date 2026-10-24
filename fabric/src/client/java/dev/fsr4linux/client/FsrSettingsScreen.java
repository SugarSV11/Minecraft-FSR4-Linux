package dev.fsr4linux.client;

import dev.fsr4linux.core.FgStats;
import dev.fsr4linux.core.BackendId;
import dev.fsr4linux.core.BackendStatus;
import dev.fsr4linux.core.Fsr4Config;
import dev.fsr4linux.core.UpscaleController;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.components.AbstractSliderButton;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.gui.screens.options.controls.KeyBindsScreen;
import net.minecraft.network.chat.Component;

/**
 * In-game settings of the mod (Options > Video Settings > "FSR...", or the F6 key). Every button goes through the same code path as the hot keys
 * (F10 upscaling, F8 version, F9 quality, F7 frame generation), so the controller's safe-switch/rollback logic applies. Right panel: live values
 * only (what the AMD runtime confirmed, resolutions, timings, errors) - nothing here is assumed.
 */
public final class FsrSettingsScreen extends Screen {
	private final Screen parent;
	private Button bUpscale, bVersion, bQuality, bFg, bMult, bPacing, bOverlay, bFps;
	private long lastRateNs; private FgStats.Window rateWindow; private FgStats.Summary rate;

	public FsrSettingsScreen(Screen parent) {
		super(Component.literal("FSR Upscaling y Frame Generation"));
		this.parent = parent;
	}

	private Fsr4Config cfg() { return Fsr4LinuxClient.controller().config(); }
	private static String onOff(boolean b) { return b ? "SÍ" : "NO"; }

	@Override
	protected void init() {
		int colW = Math.min(200, this.width / 2 - 10), half = (colW - 4) / 2, x = this.width / 2 - colW - 4, y = 28, step = 22;
		bUpscale = addRenderableWidget(Button.builder(Component.empty(), b -> Fsr4LinuxClient.press(1)).bounds(x, y, colW, 20).build()); y += step;
		bVersion = addRenderableWidget(Button.builder(Component.empty(), b -> Fsr4LinuxClient.press(2)).bounds(x, y, colW, 20).build()); y += step;
		bQuality = addRenderableWidget(Button.builder(Component.empty(), b -> Fsr4LinuxClient.press(3)).bounds(x, y, colW, 20).build()); y += step;
		addRenderableWidget(new AbstractSliderButton(x, y, colW, 20, Component.empty(), cfg().sharpness) {
			{ updateMessage(); }
			@Override protected void updateMessage() { setMessage(Component.literal(String.format("Nitidez: %d %%", Math.round(value * 100)))); }
			@Override protected void applyValue() { cfg().sharpness = (float) value; }
			@Override public void onRelease(net.minecraft.client.input.MouseButtonEvent e) { super.onRelease(e); Fsr4LinuxClient.save(cfg()); }
		}); y += step + 8;
		bFg = addRenderableWidget(Button.builder(Component.empty(), b -> Fsr4LinuxClient.press(4)).bounds(x, y, colW, 20).build()); y += step;
		bMult = addRenderableWidget(Button.builder(Component.empty(), b -> { int n = FsrRuntime.get().nextFgCandidate(cfg().fgMultiplier); if (n != 0) { cfg().fgMultiplier = n; Fsr4LinuxClient.save(cfg()); } }).bounds(x, y, colW, 20).build()); y += step;
		bPacing = addRenderableWidget(Button.builder(Component.empty(), b -> { cfg().fgPacing = !cfg().fgPacing; Fsr4LinuxClient.save(cfg()); }).bounds(x, y, colW, 20).build()); y += step + 8;
		bOverlay = addRenderableWidget(Button.builder(Component.empty(), b -> { cfg().debugOverlay = !cfg().debugOverlay; Fsr4LinuxClient.save(cfg()); }).bounds(x, y, half, 20).build());
		bFps = addRenderableWidget(Button.builder(Component.empty(), b -> { cfg().fpsCounter = !cfg().fpsCounter; Fsr4LinuxClient.save(cfg()); }).bounds(x + half + 4, y, half, 20).build()); y += step + 8;
		addRenderableWidget(Button.builder(Component.literal("Atajos..."), b -> this.minecraft.gui.setScreen(new KeyBindsScreen(this, this.minecraft.options))).bounds(x, y, half, 20).build());
		addRenderableWidget(Button.builder(Component.literal("Hecho"), b -> onClose()).bounds(x + half + 4, y, half, 20).build());
		refreshLabels();
	}

	private void refreshLabels() {
		UpscaleController c = Fsr4LinuxClient.controller(); Fsr4Config cfg = c.config();
		FsrRuntime rt = FsrRuntime.get();
		if (!cfg.frameGeneration && !rt.fgModeCandidate(cfg.fgMultiplier) && rt.firstFgCandidate() != 0) {
			cfg.fgMultiplier = rt.firstFgCandidate(); Fsr4LinuxClient.save(cfg);
		}
		bUpscale.setMessage(Component.literal("Reescalado (F10): " + onOff(cfg.enabled)));
		bVersion.setMessage(Component.literal("Versión (F8): " + cfg.backendId().label));
		bQuality.setMessage(Component.literal("Calidad (F9): " + cfg.qualityMode().label));
		bFg.setMessage(Component.literal("Frame Generation (F7): " + onOff(cfg.frameGeneration)));
		bMult.setMessage(Component.literal(cfg.fgMultiplier >= 3 ? "Multiplicador FG: x" + cfg().fgMultiplier + " (propio)" : "Multiplicador FG: x2 (AMD oficial)"));
		bFg.active = cfg.frameGeneration || rt.firstFgCandidate() != 0;
		bMult.active = rt.firstFgCandidate() != 0;
		bPacing.setMessage(Component.literal("Pacing FG: " + onOff(cfg.fgPacing)));
		bOverlay.setMessage(Component.literal("Overlay: " + onOff(cfg.debugOverlay)));
		bFps.setMessage(Component.literal("FPS: " + onOff(cfg.fpsCounter)));
	}

	@Override
	public void tick() {
		refreshLabels();
		long now = System.nanoTime();
		if (rateWindow == null || now - lastRateNs > 1_000_000_000L) {   // live rendered / generated / presented FPS, 1 s windows
			if (rateWindow != null) rate = FsrRuntime.get().fg.end(rateWindow);
			rateWindow = FsrRuntime.get().fg.begin(); lastRateNs = now;
		}
	}

	private List<String[]> info() {   // {text, "e" for error}
		UpscaleController c = Fsr4LinuxClient.controller(); Fsr4Config cfg = c.config(); FsrRuntime rt = FsrRuntime.get();
		List<String[]> l = new ArrayList<>();
		String running = c.runningLabel();
		l.add(new String[]{"Backend activo: " + (running != null ? running : (cfg.enabled ? "pendiente de confirmación del runtime" : "ninguno (render nativo)")), ""});
		l.add(new String[]{rt.isAllocated() ? "Interna " + rt.renderWidth() + "x" + rt.renderHeight() + " -> final " + rt.outputWidth() + "x" + rt.outputHeight() : "Resolución nativa (sin reescalado)", ""});
		l.add(new String[]{String.format("FPS del juego: %.0f (%.2f ms)", 1000.0 / Math.max(0.1f, Fsr4LinuxClient.lastFrameMs()), Fsr4LinuxClient.lastFrameMs()), ""});
		l.add(new String[]{String.format("GPU escalador: %.2f ms", rt.lastGpuDispatchMs), ""});
		l.add(new String[]{String.format("Helper: %.2f ms | hilo render: %.2f ms", rt.lastHelperMs, rt.lastBridgeFrameMs), ""});
		l.add(new String[]{"", ""});
		l.add(new String[]{"Frame Generation: " + rt.fgStatus, ""});
		if (rt.x3Active()) l.add(new String[]{"FG x3: backend PROPIO experimental (sin código de AMD); GPU " + (rt.x3GpuMs() >= 0 ? String.format("%.2f ms", rt.x3GpuMs()) : "n/d (FSR4_GPU_TS=1)") + String.format(", memoria %d MiB", rt.x3Bytes() / 1048576), ""});
		else if (!rt.fgProviderReported.isEmpty()) l.add(new String[]{"Proveedor confirmado por AMD: " + rt.fgProviderReported, ""});
		if (rate != null) l.add(new String[]{String.format("FPS renderizados %.0f / generados %.0f / presentados %.0f", rate.renderedFps(), rate.generatedFps(), rate.presentedFps()), ""});
		if (rate != null && rt.fgActive()) l.add(new String[]{String.format("GPU FG %.2f ms | latencia añadida %.2f ms", rate.gpuFgMs(), rate.addedLatencyMs()), ""});
		l.add(new String[]{"", ""});
		for (BackendId id : new BackendId[]{BackendId.FSR3_1_5, BackendId.FSR4_1_1}) {
			var b = c.backend(id); BackendStatus s = b == null ? null : b.probe();
			l.add(new String[]{id.label + ": " + (s == null ? "?" : s.available() ? "disponible" : "NO disponible (" + s.failureCause() + ")"), s != null && !s.available() ? "e" : ""});
		}
		String err = !rt.lastError.isEmpty() ? rt.lastError : !rt.fgError.isEmpty() ? rt.fgError : (rt.bridgeState == FsrRuntime.BridgeState.FAILED ? rt.bridgeFailure : "");
		l.add(new String[]{err.isEmpty() ? "Errores: ninguno" : "Último error: " + err, err.isEmpty() ? "" : "e"});
		return l;
	}

	@Override
	public void extractRenderState(GuiGraphicsExtractor g, int mouseX, int mouseY, float a) {
		super.extractRenderState(g, mouseX, mouseY, a);
		g.centeredText(this.font, Component.literal("FSR: reescalado y Frame Generation"), this.width / 2, 10, 0xFFFFFFFF);
		int x = this.width / 2 + 4, y = 30, maxW = this.width / 2 - 12;
		g.fill(x - 4, y - 4, this.width - 4, this.height - 8, 0x88101418);
		for (String[] row : info()) {
			if (row[0].isEmpty()) { y += 6; continue; }
			for (var seq : this.font.split(Component.literal(row[0]), maxW)) {
				if (y > this.height - 20) return;
				g.text(this.font, seq, x, y, row[1].equals("e") ? 0xFFE5484D : 0xFFE8EAF0, false); y += 10;
			}
		}
	}

	@Override
	public void onClose() { Fsr4LinuxClient.save(cfg()); this.minecraft.gui.setScreen(parent); }
}
