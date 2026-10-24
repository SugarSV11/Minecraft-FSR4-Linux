package dev.fsr4linux.client;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import org.lwjgl.system.MemoryStack;
import org.lwjgl.vulkan.KHRPresentWait;
import org.lwjgl.vulkan.KHRSwapchain;
import org.lwjgl.vulkan.VkDevice;
import org.lwjgl.vulkan.VkPresentIdKHR;
import org.lwjgl.vulkan.VkPresentInfoKHR;
import org.lwjgl.vulkan.VkQueue;

/**
 * DIAGNOSTIC (FSR4_PRESENT_PROBE=1, off by default): attaches an increasing VK_KHR_present_id to every vkQueuePresentKHR and waits for each id on a separate thread with
 * vkWaitForPresentKHR. When the wait returns, the compositor/presentation engine has presented that image: the interval between consecutive returns is the OBSERVABLE frame cadence
 * and (return - present call) is the queue latency of that image. This is the only measurement of "frames actually shown" available without capture hardware; its limits are listed in
 * docs/PRESENTATION_ARCHITECTURE.md (it measures what the presentation engine reports, not the panel).
 */
public final class PresentProbe {
	public static final boolean WANTED = System.getenv("FSR4_PRESENT_PROBE") != null;
	private record Item(long id, long swapchain, long tCall) { }
	private static final LinkedBlockingQueue<Item> queue = new LinkedBlockingQueue<>();
	private static long nextId = 1;
	private static volatile VkDevice device;
	private static Thread thread;
	private static final Object lock = new Object();
	private static final ArrayList<double[]> done = new ArrayList<>();   // {tDone ms, latency ms}
	private static volatile long timeouts, errors;

	private PresentProbe() { }

	/** Replaces Minecraft's vkQueuePresentKHR call (mixin @Redirect): same call plus a present id. */
	public static int present(VkQueue q, VkPresentInfoKHR info, long swapchain, VkDevice dev) {
		if (!VulkanCaps.presentProbeEnabled) return KHRSwapchain.vkQueuePresentKHR(q, info);
		device = dev;
		try (MemoryStack st = MemoryStack.stackPush()) {
			long id = nextId++;
			VkPresentIdKHR pid = VkPresentIdKHR.calloc(st).sType$Default().swapchainCount(1).pPresentIds(st.longs(id));
			info.pNext(pid.address());
			long t = System.nanoTime();
			int r = KHRSwapchain.vkQueuePresentKHR(q, info);
			info.pNext(0L);
			if (r == 0 || r == 1000001003) { queue.add(new Item(id, swapchain, t)); ensureThread(); }
			return r;
		}
	}

	private static void ensureThread() {
		if (thread != null) return;
		thread = new Thread(() -> {
			while (true) {
				try {
					Item it = queue.poll(1, TimeUnit.SECONDS); if (it == null) continue;
					int r = KHRPresentWait.vkWaitForPresentKHR(device, it.swapchain, it.id, 200_000_000L);
					long now = System.nanoTime();
					if (r == 0) synchronized (lock) { done.add(new double[]{now / 1e6, (now - it.tCall) / 1e6}); }
					else if (r == 2) timeouts++; else errors++;   // 2 = VK_TIMEOUT; others: swapchain out of date / destroyed while waiting
				} catch (InterruptedException e) { return; } catch (Throwable t) { errors++; }
			}
		}, "fsr4linux-present-probe");
		thread.setDaemon(true); thread.start();
	}

	/** Start of a measurement window. */
	public static int mark() { synchronized (lock) { return done.size(); } }

	/** Statistics of the images displayed since {@code mark}: {count, observed FPS, mean interval ms, p99 interval ms, mean latency ms, p99 latency ms, share of intervals < 1 ms (same-flip images)}. */
	public static double[] stats(int from, double seconds) {
		List<double[]> w; synchronized (lock) { if (from >= done.size()) return new double[]{0, 0, 0, 0, 0, 0, 0, timeouts, errors}; w = new ArrayList<>(done.subList(from, done.size())); }
		int n = w.size(); double[] iv = new double[Math.max(0, n - 1)], lat = new double[n];
		for (int i = 0; i < n; i++) { lat[i] = w.get(i)[1]; if (i > 0) iv[i - 1] = w.get(i)[0] - w.get(i - 1)[0]; }
		java.util.Arrays.sort(lat); java.util.Arrays.sort(iv);
		double ivMean = iv.length == 0 ? 0 : java.util.Arrays.stream(iv).average().orElse(0), latMean = java.util.Arrays.stream(lat).average().orElse(0);
		long same = java.util.Arrays.stream(iv).filter(x -> x < 1.0).count();
		return new double[]{n, n / seconds, ivMean, iv.length == 0 ? 0 : iv[(int) (iv.length * 0.99)], latMean, lat.length == 0 ? 0 : lat[(int) (lat.length * 0.99)], iv.length == 0 ? 0 : (double) same / iv.length, timeouts, errors};
	}
}
