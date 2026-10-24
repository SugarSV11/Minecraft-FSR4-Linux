package dev.fsr4linux.client;

import dev.fsr4linux.core.FgStats;
import dev.fsr4linux.core.MetricStats;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;
import net.minecraft.client.Minecraft;
import net.minecraft.client.Screenshot;
import net.minecraft.client.gui.screens.TitleScreen;
import net.minecraft.world.Difficulty;
import net.minecraft.world.level.GameType;
import net.minecraft.world.level.LevelSettings;
import net.minecraft.world.level.WorldDataConfiguration;
import net.minecraft.world.level.levelgen.WorldOptions;
import net.minecraft.world.level.levelgen.presets.WorldPresets;

/**
 * Automated, reproducible in-game test driver. Active ONLY when FSR4_SCRIPT is set (never in normal play).
 * Opens/creates the isolated test world (fixed seed, peaceful, creative) and executes a ';'-separated script on the client tick:
 *   wait N | key toggle|version|quality | shot NAME | cmd COMMAND... | look YAW PITCH | turn DYAW_PER_TICK N | bench NAME N | log TEXT | fullscreen | quit
 * Everything it measures is written to <gameDir>/fsr4-test.log and <gameDir>/bench.csv.
 */
public final class TestHarness {
	private static final String LEVEL = "FSRTest";
	private static List<String[]> steps;
	private static int idx = -1, waitTicks, turnTicks, benchTicks, worldTicks;
	private static float turnRate;
	private static String benchName;
	private static final List<Float> benchFrames = new ArrayList<>();
	private static final List<Float> benchGpu = new ArrayList<>(), benchHelper = new ArrayList<>(), benchBridge = new ArrayList<>(), bSub = new ArrayList<>(), bWait = new ArrayList<>(), bIpc = new ArrayList<>(), bHRec = new ArrayList<>(), bHWait = new ArrayList<>();
	private static boolean worldRequested;
	private static FgStats.Window fgWindow;
	// deterministic per-FRAME camera (not per tick): yaw = yaw0 + omega * frameIndex, no interpolation; shots are taken at exact frame indices
	private static boolean sweeping, sweepTime; private static float sweepOmega, sweepYaw0, sweepPitch; private static long sweepFrame, sweepStartNs;
	private static double moveX, moveY, moveZ, posX0, posY0, posZ0; private static boolean moving;   // per-frame translation (blocks/frame) along world axes
	private static final java.util.List<Object[]> shotAt = new java.util.ArrayList<>();   // {name, frame}
	private static String burstName; private static int burstLeft, burstIdx;
	private static Path logFile, csv;

	public static boolean enabled() { return System.getenv("FSR4_SCRIPT") != null; }

	static void init(Minecraft mc) {
		steps = new ArrayList<>();
		for (String s : System.getenv("FSR4_SCRIPT").split(";")) { s = s.trim(); if (!s.isEmpty()) steps.add(s.split("\\s+", 2)); }
		logFile = mc.gameDirectory.toPath().resolve("fsr4-test.log"); csv = mc.gameDirectory.toPath().resolve("bench.csv");
		log("harness started, " + steps.size() + " steps");
	}

	static void log(String s) {
		Fsr4LinuxClient.LOGGER.info("[test] {}", s);
		try { Files.writeString(logFile, System.currentTimeMillis() + " " + s + "\n", StandardOpenOption.CREATE, StandardOpenOption.APPEND); } catch (IOException | RuntimeException ignored) { }
	}

	/** Called every frame from the render hook (stats for bench). */
	static void frame(float frameMs) {
		if (steps != null) perFrame(frameMs);
		if (benchName == null) return;
		FsrRuntime rt = FsrRuntime.get();
		if (benchName != null) { float[] g = rt.nativeGpuTimes(); if (g != null) benchTs.add(g); float[] fgT = rt.nativeFgGpuTimes(); if (fgT != null) benchFgTs.add(fgT); }
		boolean upscale = rt.isAllocated();
		benchFrames.add(frameMs); benchGpu.add(upscale ? rt.lastGpuDispatchMs : 0f); benchHelper.add(upscale ? rt.lastHelperMs : 0f); benchBridge.add(upscale ? rt.lastBridgeFrameMs : 0f);
		bSub.add(upscale ? rt.tSubmitMs : 0f); bWait.add(upscale ? rt.tGpuWaitMs : 0f); bIpc.add(upscale ? rt.tIpcMs : 0f);
		bHRec.add(upscale ? rt.tHelperRecMs : 0f); bHWait.add(upscale ? rt.tHelperWaitMs : 0f);
	}

	/** Called at the start of every rendered frame: the main target still holds the previous frame's final image. */
	private static void perFrame(float frameMs) {
		Minecraft mc = Minecraft.getInstance();
		if (mc.player == null || mc.level == null) return;
		if (motionActive) {
			if (motionFrame > 0) Screenshot.grab(mc.gameDirectory, mPrefix + "_" + String.format("%03d", motionFrame - 1) + ".png", mc.gameRenderer.mainRenderTarget(), 1, m -> { });
			if (motionFrame > 0 && MOTION_GEN) { var gt = FsrRuntime.get().generatedTarget(); if (gt != null) Screenshot.grab(mc.gameDirectory, mPrefix + "g_" + String.format("%03d", motionFrame - 1) + ".png", gt, 1, m -> { }); }
			if (motionFrame >= mFramesA + mFramesB) { motionActive = false; FsrRuntime.prepDumpDir = null; log("motion done after " + motionFrame + " frames"); }
			else {
				int f = motionFrame;
				if (System.getenv("FSR4_X3_CAPTURE") != null) FsrRuntime.captureTag = f;
				if (PLANES_DIR != null) { FsrRuntime.prepDumpDir = PLANES_DIR; FsrRuntime.prepDumpTag = f; }
				if (MV_DUMP_FRAME >= 0 && f == MV_DUMP_FRAME) { FsrRuntime.dumpNextPrep = true; log("MVDUMP requested at motion frame " + f); }
				if (mSheep != null) { double nx = mX0 + mVel * Math.min(f, mFramesA); mSheep.setPos(nx, mY, mZ); mSheep.xo = nx; mSheep.xOld = nx;   /* xo == x: rendered position is exactly nx whatever the partial tick (frozen world): motion is linear in the frame index */ }
				if (mStrafe != 0.0 && f >= mFramesA) { double nx = mPX + mStrafe * (f - mFramesA); mc.player.setPos(nx, mPY, mPZ); mc.player.xo = nx; mc.player.xOld = nx; mc.player.yo = mPY; mc.player.yOld = mPY; mc.player.zo = mPZ; mc.player.zOld = mPZ; }
				float yaw = mYaw0 + (f >= mFramesA ? mYawRate * (f - mFramesA) : 0f);
				mc.player.setYRot(yaw); mc.player.yRotO = yaw; float pitch = mPitch0 + (f >= mFramesA ? mPitchRate * (f - mFramesA) : 0f); mc.player.setXRot(pitch); mc.player.xRotO = pitch;
				motionFrame++;
			}
		}
		if (burstLeft > 0) { Screenshot.grab(mc.gameDirectory, burstName + "_" + String.format("%03d", burstIdx++) + ".png", mc.gameRenderer.mainRenderTarget(), 1, m -> { }); burstLeft--; }
		if (sweeping) {
			for (var it = shotAt.iterator(); it.hasNext(); ) { Object[] sa = it.next(); if ((long) sa[1] == sweepFrame) { Screenshot.grab(mc.gameDirectory, sa[0] + ".png", mc.gameRenderer.mainRenderTarget(), 1, m -> { }); it.remove(); } }
			float yaw = sweepYaw0 + (sweepTime ? sweepOmega * ((System.nanoTime() - sweepStartNs) / 1e9f) : sweepOmega * sweepFrame);
			sweepFrame++;
			mc.player.setYRot(yaw); mc.player.yRotO = yaw; mc.player.setXRot(sweepPitch); mc.player.xRotO = sweepPitch;
			if (moving) { double nx = posX0 + moveX * sweepFrame, ny = posY0 + moveY * sweepFrame, nz = posZ0 + moveZ * sweepFrame; mc.player.setPos(nx, ny, nz); mc.player.xo = nx; mc.player.yo = ny; mc.player.zo = nz; mc.player.xOld = nx; mc.player.yOld = ny; mc.player.zOld = nz; }
		}
	}

	private static void clickWidget(Minecraft mc, String arg) {
		var sc = mc.gui.screen();
		if (sc == null) { log("click: no screen"); return; }
		var kids = sc.children(); net.minecraft.client.gui.components.AbstractWidget hit = null; int i = 0;
		for (var ch : kids) {
			if (ch instanceof net.minecraft.client.gui.components.AbstractWidget w && (arg.matches("\\d+") ? Integer.parseInt(arg) == i : w.getMessage().getString().contains(arg))) { hit = w; break; }
			i++;
		}
		if (hit == null) { log("click: no widget '" + arg + "'"); return; }
		double cx = hit.getX() + hit.getWidth() / 2.0, cy = hit.getY() + hit.getHeight() / 2.0;
		// Vanilla widgets accept MouseButtonInfo(1, 0) as the primary click in this Minecraft version (measured: ctor value 0 is rejected by AbstractWidget.isValidClickButton).
		var ev = new net.minecraft.client.input.MouseButtonEvent(cx, cy, new net.minecraft.client.input.MouseButtonInfo(1, 0));
		boolean pressed = sc.mouseClicked(ev, false); sc.mouseReleased(ev);
		log("click '" + hit.getMessage().getString() + "' at " + (int) cx + "," + (int) cy + " handled=" + pressed);
	}

	static void tick(Minecraft mc) {
		if (steps == null) init(mc);
		if (mc.level == null || mc.player == null) {
			worldTicks = 0;
			if (!worldRequested && mc.gui.screen() instanceof TitleScreen) { worldRequested = true; openWorld(mc); }
			return;
		}
		if (++worldTicks < 80) return;   // let the spawn chunks load
		if (turnTicks > 0) { mc.player.setYRot(mc.player.getYRot() + turnRate); turnTicks--; }
		if (benchName != null) { if (--benchTicks <= 0) endBench(); else return; }
		if (motionActive) return;
		if (waitTicks > 0) { waitTicks--; return; }
		if (turnTicks > 0) return;
		if (++idx >= steps.size()) { log("script finished"); steps = List.of(); idx = 0; return; }
		String[] st = steps.get(idx); String op = st[0].toLowerCase(Locale.ROOT); String arg = st.length > 1 ? st[1] : "";
		log("step " + idx + ": " + op + " " + arg);
		switch (op) {
			case "wait" -> waitTicks = Integer.parseInt(arg);
			case "key" -> {
				switch (arg) { case "fg" -> Fsr4LinuxClient.press(4); case "toggle" -> Fsr4LinuxClient.press(1); case "version" -> Fsr4LinuxClient.press(2); case "quality" -> Fsr4LinuxClient.press(3); default -> log("unknown key " + arg); } }
			case "shot" -> { Screenshot.grab(mc.gameDirectory, arg + ".png", mc.gameRenderer.mainRenderTarget(), 1, m -> { }); waitTicks = 4; }
			case "sweep", "sweeptime" -> { String[] a = arg.split("\\s+"); sweepOmega = Float.parseFloat(a[0]); sweepYaw0 = mc.player.getYRot(); sweepPitch = mc.player.getXRot(); sweepFrame = 0; sweepStartNs = System.nanoTime(); sweepTime = op.equals("sweeptime"); sweeping = true; shotAt.clear(); }
			case "sweepstop" -> { sweeping = false; sweepTime = false; if (moving) { mc.player.setPos(posX0, posY0, posZ0); mc.player.xo = posX0; mc.player.yo = posY0; mc.player.zo = posZ0; mc.player.xOld = posX0; mc.player.yOld = posY0; mc.player.zOld = posZ0; } moving = false; }
			case "move" -> { String[] a = arg.split("\\s+"); moveX = Double.parseDouble(a[0]); moveY = Double.parseDouble(a[1]); moveZ = Double.parseDouble(a[2]); posX0 = mc.player.getX(); posY0 = mc.player.getY(); posZ0 = mc.player.getZ(); moving = true; }
			case "shotat" -> { String[] a = arg.split("\\s+"); shotAt.add(new Object[]{a[0], Long.parseLong(a[1])}); }
			case "burst" -> { String[] a = arg.split("\\s+"); burstName = a[0]; burstLeft = Integer.parseInt(a[1]); burstIdx = 0; }
			case "jflip" -> { FsrRuntime.jitterFlip = Integer.parseInt(arg.trim()); log("jitterFlip=" + FsrRuntime.jitterFlip); }
			case "mvscale" -> { String[] a = arg.split("\\s+"); FsrRuntime.mvScaleX = Float.parseFloat(a[0]); FsrRuntime.mvScaleY = Float.parseFloat(a[1]); log("mvScale=" + a[0] + "," + a[1]); }
			case "dump" -> { String[] a = arg.split("\\s+"); log("DUMP plane " + a[0] + ": " + FsrRuntime.get().debugDump(Integer.parseInt(a[0]), a.length > 1 ? mc.gameDirectory.toPath().resolve(a[1]).toString() : null)); }
			case "leaveworld" -> { worldRequested = false; mc.disconnect(new TitleScreen(), false); }   // back to the title screen; the harness re-opens the test world by itself and the script continues
			case "hidegui" -> { if (!mc.gui.hud.isHidden()) mc.gui.hud.toggle(); }
			case "clearchat" -> mc.gui.hud.getChat().clearMessages(false);
			case "menu" -> Fsr4LinuxClient.openSettings(mc.gui.screen());
			case "motion" -> { String[] a = arg.split("\\s+"); mFramesA = Integer.parseInt(a[0]); mFramesB = Integer.parseInt(a[1]); mVel = Double.parseDouble(a[2]); mYawRate = Float.parseFloat(a[3]); mPrefix = a[4]; mPitchRate = a.length > 5 ? Float.parseFloat(a[5]) : 0f; mStrafe = a.length > 6 ? Double.parseDouble(a[6]) : 0.0; mPX = mc.player.getX(); mPY = mc.player.getY(); mPZ = mc.player.getZ();
				mSheep = null; for (var e : mc.level.entitiesForRendering()) if (e.getClass().getSimpleName().equals("Sheep")) { mSheep = e; break; }
				if (mSheep == null) { log("motion: no sheep"); } else { mX0 = mSheep.getX(); mY = mSheep.getY(); mZ = mSheep.getZ(); }
				mYaw0 = mc.player.getYRot(); mPitch0 = mc.player.getXRot(); motionFrame = 0; motionActive = true; log("motion start sheep=" + (mSheep != null) + " x0=" + mX0); }
			case "click" -> clickWidget(mc, arg);   // real mouse press/release on the widget whose label contains arg (or index)
			case "widgets" -> { var sc = mc.gui.screen(); if (sc == null) log("widgets: no screen"); else { int i = 0; for (var ch : sc.children()) log("widget " + (i++) + ": " + (ch instanceof net.minecraft.client.gui.components.AbstractWidget w ? w.getMessage().getString() : ch.getClass().getSimpleName())); } }
			case "closemenu" -> { if (mc.gui.screen() instanceof FsrSettingsScreen) mc.gui.setScreen(null); }
			case "killhelper" -> { try { new ProcessBuilder("pkill", "-9", "-f", "fsr4_bridge[.]exe").start().waitFor(); log("helper killed (SIGKILL)"); } catch (Exception e) { log("killhelper failed: " + e); } }
			case "freezehelper" -> { try { new ProcessBuilder("pkill", "-STOP", "-f", "fsr4_bridge[.]exe").start().waitFor(); log("helper frozen (SIGSTOP)"); } catch (Exception e) { log("freezehelper failed: " + e); } }
			case "thawhelper" -> { try { new ProcessBuilder("pkill", "-CONT", "-f", "fsr4_bridge[.]exe").start().waitFor(); log("helper resumed (SIGCONT)"); } catch (Exception e) { log("thawhelper failed: " + e); } }
			case "shotpair" -> { var gt = FsrRuntime.get().generatedTarget(); if (gt == null) log("shotpair: no generated frame target"); else { Screenshot.grab(mc.gameDirectory, arg + "_real.png", mc.gameRenderer.mainRenderTarget(), 1, m -> { }); Screenshot.grab(mc.gameDirectory, arg + "_gen.png", gt, 1, m -> { }); waitTicks = 4; } }
			case "shotgen" -> { var gt = FsrRuntime.get().generatedTarget(); if (gt == null) log("shotgen: no generated frame target"); else { Screenshot.grab(mc.gameDirectory, arg + ".png", gt, 1, m -> { }); waitTicks = 4; } }
			case "cmd" -> mc.getConnection().sendCommand(arg.startsWith("/") ? arg.substring(1) : arg);
			case "look" -> { String[] a = arg.split("\\s+"); mc.player.setYRot(Float.parseFloat(a[0])); mc.player.setXRot(Float.parseFloat(a[1])); }
			case "turn" -> { String[] a = arg.split("\\s+"); turnRate = Float.parseFloat(a[0]); turnTicks = Integer.parseInt(a[1]); }
			case "bench" -> { probeFrom = PresentProbe.WANTED ? PresentProbe.mark() : -1; probeT0 = System.nanoTime(); String[] a = arg.split("\\s+"); benchName = a[0]; benchTicks = Integer.parseInt(a[1]); fgWindow = FsrRuntime.get().fg.begin(); fgSegStart = FsrRuntime.get().fgSeg.clone(); fgSegBStart = FsrRuntime.get().fgSegB.clone(); benchFrames.clear(); benchTs.clear(); benchFgTs.clear(); benchGpu.clear(); benchHelper.clear(); benchBridge.clear(); bSub.clear(); bWait.clear(); bIpc.clear(); bHRec.clear(); bHWait.clear(); }
			case "log" -> log(arg);
			case "state" -> { var c = Fsr4LinuxClient.controller(); log("STATE active=" + c.isActive() + " enabled=" + c.config().enabled + " backend=" + c.config().backend + " quality=" + c.config().quality + " running=" + c.runningLabel() + " executed=" + FsrRuntime.get().executedProvider() + " fg=" + FsrRuntime.get().fgStatus + " fgProvider=" + FsrRuntime.get().fgProviderReported + " internal=" + FsrRuntime.get().renderWidth() + "x" + FsrRuntime.get().renderHeight() + " out=" + FsrRuntime.get().outputWidth() + "x" + FsrRuntime.get().outputHeight()); }
			case "fly" -> { mc.player.getAbilities().mayfly = true; mc.player.getAbilities().flying = true; mc.player.onUpdateAbilities(); }
			case "fullscreen" -> { var o = mc.options.fullscreen(); o.set(!o.get()); mc.getWindow().setFullscreen(o.get()); }
			case "quit" -> { log("quit"); mc.stop(); }
			default -> log("unknown step " + op);
		}
	}

	private static int probeFrom = -1; private static long probeT0; private static double probeSeconds = 1;

	private static void endBench() {
		probeSeconds = Math.max(1e-3, (System.nanoTime() - probeT0) / 1e9);
		List<Float> f = new ArrayList<>(benchFrames); if (f.size() < 10) { log("bench " + benchName + ": too few frames (" + f.size() + ")"); benchName = null; return; }
		List<Float> sorted = new ArrayList<>(f); sorted.sort(Float::compare);
		double avg = f.stream().mapToDouble(x -> x).average().orElse(0);
		int n = sorted.size(); int worst = Math.max(1, n / 100); double low1 = sorted.subList(n - worst, n).stream().mapToDouble(x -> x).average().orElse(0);
		var c = Fsr4LinuxClient.controller();
		String line = String.format(Locale.ROOT, "%s,%s,%s,%s,frames=%d,avg_ms=%.3f,fps_avg=%.1f,p50_ms=%.3f,p99_ms=%.3f,fps_1pct_low=%.1f,gpu_dispatch_ms=%.3f,helper_ms=%.3f,bridge_render_thread_ms=%.3f,submit_ms=%.3f,gpuwait_ms=%.3f,ipc_ms=%.3f,helper_rec_ms=%.3f,helper_wait_ms=%.3f,executed=%s",
			benchName, c.config().enabled ? c.config().backend : "NATIVE", c.config().quality, FsrRuntime.get().isAllocated() ? FsrRuntime.get().renderWidth() + "x" + FsrRuntime.get().renderHeight() : "native",
			n, avg, 1000.0 / avg, sorted.get(n / 2), sorted.get((int) (n * 0.99)), 1000.0 / low1, mean(benchGpu), mean(benchHelper), mean(benchBridge), mean(bSub), mean(bWait), mean(bIpc), mean(bHRec), mean(bHWait), c.runningLabel());
		var fgs = FsrRuntime.get().fg.end(fgWindow);
		line += String.format(Locale.ROOT, ",fg=%s,fg_mode=%s,rendered_fps=%.1f,generated_fps=%.1f,presented_fps=%.1f,added_latency_ms=%.3f,fg_hook_cpu_ms=%.3f,fg_ipc_ms=%.3f,fg_gpu_ms=%.3f,pace_wait_ms=%.3f,present_iv_mean_ms=%.3f,present_iv_p99_ms=%.3f,present_iv_max_ms=%.3f,present_iv_std_ms=%.3f",
			FsrRuntime.get().fgActive(), FsrRuntime.get().fgActive() ? (FsrRuntime.get().fgOnly() ? "only" : "with_upscaler") : "off", fgs.renderedFps(), fgs.generatedFps(), fgs.presentedFps(), fgs.addedLatencyMs(), fgs.hookCpuMs(), fgs.ipcMs(), fgs.gpuFgMs(), fgs.paceWaitMs(),
			fgs.presentIntervalMeanMs(), fgs.presentIntervalP99Ms(), fgs.presentIntervalMaxMs(), fgs.presentIntervalStdMs());
		if (PresentProbe.WANTED && probeFrom >= 0) { double[] o = PresentProbe.stats(probeFrom, probeSeconds); line += String.format(Locale.ROOT, ",obs_images=%.0f,obs_fps=%.1f,obs_iv_mean_ms=%.3f,obs_iv_p99_ms=%.3f,obs_queue_lat_mean_ms=%.3f,obs_queue_lat_p99_ms=%.3f,obs_same_flip_share=%.3f,obs_timeouts=%.0f,obs_errors=%.0f", o[0], o[1], o[2], o[3], o[4], o[5], o[6], o[7], o[8]); }
		if (FsrRuntime.get().x3Active()) line += String.format(Locale.ROOT, ",x3_gpu_ms_last=%.3f,x3_mem_mib=%d,batch_present=%s,present_mode=%s,swapchain_images_req=%d,acquire_misses=%d", FsrRuntime.get().x3GpuMs(), FsrRuntime.get().x3Bytes() / 1048576, BatchPresent.ENABLED, BatchPresent.configuredMode, BatchPresent.IMAGES, BatchPresent.acquireMisses);
		{ double[] e = FsrRuntime.get().fgSeg; double nh = Math.max(1, e[5] - fgSegStart[5]); if (e[5] > fgSegStart[5]) line += String.format(Locale.ROOT, ",fgseg_input_ms=%.3f,fgseg_ipc_ms=%.3f,fgseg_outcopy_ms=%.3f,fgseg_blit_present_ms=%.3f,fgseg_acquire_ms=%.3f", (e[0]-fgSegStart[0])/nh, (e[1]-fgSegStart[1])/nh, (e[2]-fgSegStart[2])/nh, (e[3]-fgSegStart[3])/nh, (e[4]-fgSegStart[4])/nh); double[] b = FsrRuntime.get().fgSegB; if (e[5] > fgSegStart[5]) line += String.format(Locale.ROOT, ",fgseg_blit_ms=%.3f,fgseg_submit_ms=%.3f,fgseg_present_ms=%.3f", (b[0]-fgSegBStart[0])/nh, (b[1]-fgSegBStart[1])/nh, (b[2]-fgSegBStart[2])/nh); }
		if (!benchFgTs.isEmpty()) line += String.format(Locale.ROOT, ",fgts_in_ms=%.3f,fgts_chain_ms=%.3f,fgts_out_ms=%.3f,fgts_gap_ms=%.3f", benchFgTs.stream().mapToDouble(a -> a[0]).average().orElse(0), benchFgTs.stream().mapToDouble(a -> a[1]).average().orElse(0), benchFgTs.stream().mapToDouble(a -> a[2]).average().orElse(0), benchFgTs.stream().mapToDouble(a -> a[3]).average().orElse(0));
		if (!benchTs.isEmpty()) line += String.format(Locale.ROOT, ",ts_prep_ms=%.3f,ts_chain_ms=%.3f,ts_out_ms=%.3f,ts_other_ms=%.3f", tsMean(0), tsMean(1), tsMean(2), tsMean(3));
		line += metricLine("frame", benchFrames) + metricLine("dispatch", benchGpu) + metricLine("helper", benchHelper)
			+ metricLine("bridge", benchBridge) + metricLine("submit", bSub) + metricLine("gpuwait", bWait)
			+ metricLine("ipc", bIpc) + metricLine("helper_rec", bHRec) + metricLine("helper_wait", bHWait);
		writeRawFrames();
		try { Files.writeString(csv, line + "\n", StandardOpenOption.CREATE, StandardOpenOption.APPEND); } catch (IOException ignored) { }
		log("BENCH " + line);
		benchName = null;
	}

	// deterministic per-rendered-frame motion test: phase A moves the sheep at constant velocity with a static camera, phase B pans the camera with the sheep parked
	private static boolean motionActive; private static int motionFrame, mFramesA, mFramesB; private static double mVel, mX0, mY, mZ; private static double mStrafe, mPX, mPY, mPZ; private static float mPitchRate, mYawRate, mYaw0, mPitch0; private static String mPrefix = "mo";
	private static net.minecraft.world.entity.Entity mSheep;
	private static final String PLANES_DIR = System.getenv("FSR4_MOTION_PLANES");
	private static final boolean MOTION_GEN = System.getenv("MOTION_GEN") != null;
	private static final int MV_DUMP_FRAME = System.getenv("FSR4_MOTION_DUMP") != null ? Integer.parseInt(System.getenv("FSR4_MOTION_DUMP")) : -1;
	private static double[] fgSegStart = new double[6], fgSegBStart = new double[3];
	private static final List<float[]> benchTs = new ArrayList<>(), benchFgTs = new ArrayList<>();
	private static double tsMean(int i) { return benchTs.stream().mapToDouble(a -> a[i]).average().orElse(Double.NaN); }

	private static double mean(List<Float> l) { return l.stream().mapToDouble(x -> x).average().orElse(0); }
	private static String metricLine(String name, List<Float> samples) {
		MetricStats s = MetricStats.of(samples);
		return String.format(Locale.ROOT, ",%s_p50_ms=%.3f,%s_p95_ms=%.3f,%s_p99_ms=%.3f,%s_max_ms=%.3f",
			name, s.median(), name, s.p95(), name, s.p99(), name, s.max());
	}

	private static void writeRawFrames() {
		Path path = csv.resolveSibling("frametime-samples.csv");
		StringBuilder rows = new StringBuilder(benchFrames.size() * 96);
		if (!Files.exists(path)) rows.append("bench,index,frame_ms,dispatch_ms,helper_ms,bridge_ms,submit_ms,gpuwait_ms,ipc_ms,helper_rec_ms,helper_wait_ms\n");
		for (int i = 0; i < benchFrames.size(); i++) {
			rows.append(benchName).append(',').append(i).append(',').append(benchFrames.get(i)).append(',')
				.append(benchGpu.get(i)).append(',').append(benchHelper.get(i)).append(',').append(benchBridge.get(i)).append(',')
				.append(bSub.get(i)).append(',').append(bWait.get(i)).append(',').append(bIpc.get(i)).append(',')
				.append(bHRec.get(i)).append(',').append(bHWait.get(i)).append('\n');
		}
		try { Files.writeString(path, rows, StandardOpenOption.CREATE, StandardOpenOption.APPEND); }
		catch (IOException e) { log("cannot write raw frame samples: " + e); }
	}

	private static void openWorld(Minecraft mc) {
		File saves = new File(mc.gameDirectory, "saves/" + LEVEL);
		log("opening test world (exists=" + saves.exists() + ")");
		var flows = mc.createWorldOpenFlows();
		if (saves.exists()) { flows.openWorld(LEVEL, () -> { }); return; }
		LevelSettings ls = new LevelSettings(LEVEL, GameType.CREATIVE, new LevelSettings.DifficultySettings(Difficulty.PEACEFUL, false, true), true, WorldDataConfiguration.DEFAULT);
		flows.createFreshLevel(LEVEL, ls, new WorldOptions(Long.parseLong(System.getenv().getOrDefault("FSR4_SEED", "4815162342")), true, false), WorldPresets::createNormalWorldDimensions, mc.gui.screen());
	}
}
