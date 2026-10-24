package dev.fsr4linux.client;

import com.mojang.blaze3d.platform.InputConstants;
import dev.fsr4linux.core.BackendId;
import dev.fsr4linux.core.Fsr4Config;
import dev.fsr4linux.core.Notification;
import dev.fsr4linux.core.UpscaleController;
import java.io.IOException;
import java.nio.file.Path;
import java.util.ArrayDeque;
import java.util.List;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientLifecycleEvents;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.fabricmc.fabric.api.client.keymapping.v1.KeyMappingHelper;
import net.fabricmc.fabric.api.client.rendering.v1.hud.HudElementRegistry;
import net.fabricmc.loader.api.FabricLoader;
import net.minecraft.client.KeyMapping;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.GameRenderer;
import net.minecraft.resources.Identifier;
import org.joml.Matrix4f;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/** Client entry point: key bindings (F8 version, F9 quality, F10 on/off), notifications, wiring of the controller to the renderer. */
public class Fsr4LinuxClient implements ClientModInitializer {
	public static final Logger LOGGER = LoggerFactory.getLogger("fsr4linux");
	private static final Identifier ID = Identifier.fromNamespaceAndPath("fsr4linux", "main");

	private static KeyMapping keyVersion, keyQuality, keyToggle, keyFg, keySettings;
	private static UpscaleController controller;
	private static Path configFile;
	private static final ArrayDeque<Toast> toasts = new ArrayDeque<>();
	public record Toast(Notification n, long until) { }
	/** Action requested while the helper was still starting; replayed once it is running (0 none, 1 toggle, 2 version, 3 quality). */
	private static int pendingKey;
	private static long lastFrameNs;
	private static volatile boolean stopping;   // client is shutting down: the bridge is gone, do not touch the controller any more

	public static UpscaleController controller() { return controller; }

	@Override
	public void onInitializeClient() {
		configFile = FabricLoader.getInstance().getConfigDir().resolve("fsr4linux.json");
		Fsr4Config cfg = Fsr4Config.load(configFile);
		boolean wasEnabled = cfg.enabled;
		cfg.enabled = false;   // re-enabled once the external process is up (never claim "active" before that)
		controller = new UpscaleController(cfg, List.of(new BridgeBackend(BackendId.FSR3_1_5), new BridgeBackend(BackendId.FSR4_1_1)),
			Fsr4LinuxClient::notifyUser, c -> save(c));

		KeyMapping.Category cat = KeyMapping.Category.register(ID);
		keyVersion = KeyMappingHelper.registerKeyMapping(new KeyMapping("key.fsr4linux.version", InputConstants.KEY_F8, cat));
		keyQuality = KeyMappingHelper.registerKeyMapping(new KeyMapping("key.fsr4linux.quality", InputConstants.KEY_F9, cat));
		keyToggle = KeyMappingHelper.registerKeyMapping(new KeyMapping("key.fsr4linux.toggle", InputConstants.KEY_F10, cat));
		keyFg = KeyMappingHelper.registerKeyMapping(new KeyMapping("key.fsr4linux.framegen", InputConstants.KEY_F7, cat));
		keySettings = KeyMappingHelper.registerKeyMapping(new KeyMapping("key.fsr4linux.settings", InputConstants.KEY_F6, cat));
		net.fabricmc.fabric.api.client.screen.v1.ScreenEvents.AFTER_INIT.register((client, screen, w, h) -> {   // button in Options > Video Settings
			if (screen instanceof net.minecraft.client.gui.screens.options.VideoSettingsScreen) {
				net.fabricmc.fabric.api.client.screen.v1.Screens.getWidgets(screen).add(net.minecraft.client.gui.components.Button.builder(net.minecraft.network.chat.Component.literal("FSR..."), b -> openSettings(screen)).bounds(w - 66, 6, 60, 20).build());
			}
		});

		ClientTickEvents.END_CLIENT_TICK.register(Fsr4LinuxClient::onTick);
		if (TestHarness.enabled()) ClientTickEvents.END_CLIENT_TICK.register(TestHarness::tick);
		HudElementRegistry.addLast(Identifier.fromNamespaceAndPath("fsr4linux", "overlay"), FsrHud::render);
		ClientLifecycleEvents.CLIENT_STOPPING.register(c -> { stopping = true; controller.setRendererReady(false); FsrRuntime.get().shutdown(); FsrBootstrap.get().shutdown(); });

		// Start bootstrap (extracts native libs, AMD DLLs, downloads GE-Proton)
		FsrBootstrap.get().startAsync(FabricLoader.getInstance().getConfigDir(), Fsr4LinuxClient::notifyUser);

		if (wasEnabled) { pendingKey = 1; }
		LOGGER.info("fsr4linux initialised (config {}, previous state enabled={}, backend={}, quality={})", configFile, wasEnabled, cfg.backend, cfg.quality);
	}

	static void save(Fsr4Config c) {
		try { c.save(configFile); } catch (IOException e) { LOGGER.warn("cannot save {}: {}", configFile, e.toString()); }
	}

	static void notifyUser(Notification n) {
		toasts.add(new Toast(n, System.currentTimeMillis() + Notification.DURATION_MS));
		while (toasts.size() > 3) toasts.poll();
		if (n.error()) LOGGER.warn("{}", n.text()); else LOGGER.info("{}", n.text());
	}

	public static ArrayDeque<Toast> toasts() { return toasts; }

	private static void onTick(Minecraft mc) {
		// consumeClick() yields one event per physical press (holding the key does not repeat); screens swallow keys while typing.
		boolean t = false, v = false, q = false, g = false;
		while (keySettings.consumeClick()) openSettings(mc.gui.screen());
		while (keyFg.consumeClick()) g = true;
		if (g) toggleFg();
		while (keyToggle.consumeClick()) t = true;
		while (keyVersion.consumeClick()) v = true;
		while (keyQuality.consumeClick()) q = true;
		FsrRuntime rt = FsrRuntime.get();
		if (t) request(1); if (v) request(2); if (q) request(3);
		if (pendingKey != 0) {
			switch (rt.bridgeState) {
				case RUNNING -> { int k = pendingKey; pendingKey = 0; dispatch(k); }
				case FAILED -> { pendingKey = 0; notifyUser(new Notification("FSR NO DISPONIBLE: " + rt.bridgeFailure, true)); }
				case NOT_STARTED -> rt.startBridgeAsync();
				default -> { }
			}
		}
	}

	/** Same path as a physical key press (1 toggle, 2 version, 3 quality). */
	public static void press(int key) { request(key); }

	private static void request(int key) {
		if (key == 4) { toggleFg(); return; }   // F7: frame generation is independent of the upscaler; FsrRuntime.fgBoundary starts the bridge when needed
		FsrRuntime rt = FsrRuntime.get();
		if (rt.bridgeState == FsrRuntime.BridgeState.RUNNING) { dispatch(key); return; }
		if (rt.bridgeState == FsrRuntime.BridgeState.FAILED) rt.bridgeState = FsrRuntime.BridgeState.NOT_STARTED;   // F-key retries a failed start
		if (key == 3 && !controller.config().enabled) { dispatch(key); return; }   // quality can be chosen without the bridge
		pendingKey = key; rt.startBridgeAsync();
		notifyUser(new Notification("FSR: INICIANDO PUENTE WINE (~2 s)", false));
	}

	static void openSettings(net.minecraft.client.gui.screens.Screen parent) {
		if (controller == null) return;
		Minecraft mc = Minecraft.getInstance();
		if (mc.gui.screen() instanceof FsrSettingsScreen) return;
		mc.gui.setScreen(new FsrSettingsScreen(parent));
	}

	/** F7. The state only becomes ACTIVE when the AMD runtime created the FG context (shown by a second notification); failures turn it off again and say why. */
	static void toggleFg() {
		if (controller == null) return;
		Fsr4Config cfg = controller.config();
		FsrRuntime rt = FsrRuntime.get();
		if (!cfg.frameGeneration && !rt.fgModeCandidate(cfg.fgMultiplier)) {
			int candidate = rt.firstFgCandidate();
			if (candidate == 0) {
				notifyUser(new Notification("FRAME GENERATION NO DISPONIBLE EN LA GPU VULKAN SELECCIONADA", true));
				return;
			}
			cfg.fgMultiplier = candidate;
		}
		if (!cfg.frameGeneration && rt.bridgeState == FsrRuntime.BridgeState.FAILED) rt.bridgeState = FsrRuntime.BridgeState.NOT_STARTED;   // like the other F-keys: retry a failed bridge
		cfg.frameGeneration = !cfg.frameGeneration; save(cfg);
		notifyUser(new Notification(cfg.frameGeneration ? "FRAME GENERATION: ACTIVANDO..." : "FRAME GENERATION DESACTIVADO", false));
	}

	/** Called from the render thread when FG fails at runtime. */
	static void requestFgOff(String why) {
		if (controller == null) return;
		controller.config().frameGeneration = false; save(controller.config());
		notifyUser(new Notification("FRAME GENERATION: FALLO (" + why + ") - DESACTIVADO", true));
	}

	private static void dispatch(int key) {
		switch (key) { case 1 -> controller.requestToggle(); case 2 -> controller.requestCycleBackend(); case 3 -> controller.requestCycleQuality(); default -> { } }
	}

	// ---- renderer hooks (render thread) -----------------------------------------------------------------------
	public static void onFrameStart(GameRenderer gr) {
		if (controller == null || stopping) return;
		long now = System.nanoTime(); lastFrameMs = lastFrameNs == 0 ? 16.6f : (now - lastFrameNs) / 1e6f; lastFrameNs = now;
		TestHarness.frame(lastFrameMs);
		controller.setRendererReady(true);
		var main = gr.mainRenderTarget();
		FsrRuntime rt = FsrRuntime.get();
		if (controller.config().enabled && rt.isAllocated() && (main.width != rt.outputWidth() || main.height != rt.outputHeight())) controller.requestResize(main.width, main.height);
		else if (!controller.config().enabled || !rt.isAllocated()) controller.requestResize(main.width, main.height);
		rt.helperWatchdog(controller, controller.config(), Fsr4LinuxClient::notifyUser);
		controller.onFrameBoundary(rt.gpuSync());
		rt.fgBoundary(gr, controller.config(), Fsr4LinuxClient::notifyUser);
	}

	private static volatile float lastFrameMs = 16.6f;
	public static float lastFrameMs() { return lastFrameMs; }

	public static void worldPrep(GameRenderer gr) { if (controller != null && !stopping) FsrRuntime.get().worldPrep(gr); }
	public static void beginWorld(GameRenderer gr) { if (controller != null) FsrRuntime.get().beginWorld(gr, controller); }
	public static void endWorld(GameRenderer gr) { if (controller != null) FsrRuntime.get().endWorld(gr, controller, lastFrameMs, controller.config().sharpness); }
	public static Matrix4f jitter(Matrix4f projection) {
		FsrRuntime rt = FsrRuntime.get();
		if (rt.worldPhaseActive()) return rt.jitterProjection(projection);
		rt.captureProjection(projection);
		return projection;
	}
	/** Mixin hooks around Minecraft's swapchain blit/present (see MinecraftMixin). */
	public static void presentHook(Minecraft mc, com.mojang.renderpearl.api.device.GpuSurface surface) { if (controller != null && !stopping) FsrRuntime.get().presentHook(mc, surface, controller.config()); }
	public static void presentReal(com.mojang.renderpearl.api.device.GpuSurface surface) { if (controller != null && !stopping && dev.fsr4linux.client.BatchPresent.hasPending()) FsrRuntime.get().presentBatch(surface); else surface.present(); }
	public static void afterRealPresent() { if (controller != null && !stopping) FsrRuntime.get().afterRealPresent(); }
}
