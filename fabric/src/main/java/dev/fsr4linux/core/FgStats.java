package dev.fsr4linux.core;

import java.util.Arrays;

/**
 * Frame accounting for Frame Generation. THREE different quantities are counted separately and never mixed:
 *  - rendered:  real frames produced by the game (one per Minecraft frame),
 *  - generated: frames produced by the AMD FG provider AND handed to the swapchain,
 *  - presented: vkQueuePresent calls (generated + real). This is what Minecraft submits; the monitor shows at most its refresh rate.
 * All counters are written by the render thread only; readers use snapshots (Window).
 */
public final class FgStats {
	private static final int RING = 1 << 17, MASK = RING - 1;
	private final long[] presentNs = new long[RING];
	public volatile long realFrames, generatedPresented, presentedTotal, realPresented;
	public volatile double latencySumMs, hookCpuSumMs, ipcSumMs, gpuFgSumMs, paceWaitSumMs, genCopySumMs;
	public volatile long latencyN, hookN, gpuN;

	public void onPresent(long ns) { presentNs[(int) (presentedTotal & MASK)] = ns; presentedTotal++; }

	public record Window(long startNs, long real, long gen, long presented, long realPresented, long presentIdx, double lat, long latN, double hook, long hookN, double ipc, double gpu, long gpuN, double pace) { }

	public Window begin() { return new Window(System.nanoTime(), realFrames, generatedPresented, presentedTotal, realPresented, presentedTotal, latencySumMs, latencyN, hookCpuSumMs, hookN, ipcSumMs, gpuFgSumMs, gpuN, paceWaitSumMs); }

	public record Summary(double seconds, double renderedFps, double generatedFps, double presentedFps, double addedLatencyMs, double hookCpuMs, double ipcMs, double gpuFgMs, double paceWaitMs,
	                      double presentIntervalMeanMs, double presentIntervalP99Ms, double presentIntervalMaxMs, double presentIntervalStdMs) { }

	public Summary end(Window w) {
		double sec = (System.nanoTime() - w.startNs()) / 1e9;
		long n = presentedTotal - w.presentIdx();
		double[] iv = new double[(int) Math.max(0, Math.min(n - 1, RING - 2))];
		for (int i = 0; i < iv.length; i++) iv[i] = (presentNs[(int) ((w.presentIdx() + i + 1) & MASK)] - presentNs[(int) ((w.presentIdx() + i) & MASK)]) / 1e6;
		double mean = 0, std = 0, p99 = 0, max = 0;
		if (iv.length > 10) {
			for (double v : iv) { mean += v; max = Math.max(max, v); }
			mean /= iv.length; for (double v : iv) std += (v - mean) * (v - mean); std = Math.sqrt(std / iv.length);
			double[] s = iv.clone(); Arrays.sort(s); p99 = s[(int) (s.length * 0.99)];
		}
		long hn = Math.max(1, hookN - w.hookN()), ln = Math.max(1, latencyN - w.latN()), gn = Math.max(1, gpuN - w.gpuN());
		return new Summary(sec, (realFrames - w.real()) / sec, (generatedPresented - w.gen()) / sec, (presentedTotal - w.presented()) / sec,
			(latencySumMs - w.lat()) / ln, (hookCpuSumMs - w.hook()) / hn, (ipcSumMs - w.ipc()) / hn, (gpuFgSumMs - w.gpu()) / gn, (paceWaitSumMs - w.pace()) / hn, mean, p99, max, std);
	}
}
