package dev.fsr4linux.client;

import com.mojang.renderpearl.api.device.GpuSurface;
import com.mojang.renderpearl.api.textures.GpuTextureView;
import com.mojang.renderpearl.backend.vulkan.VulkanCommandEncoder;
import com.mojang.renderpearl.backend.vulkan.VulkanGpuSurface;
import com.mojang.renderpearl.frontend.FrontendGpuSurface;
import java.nio.IntBuffer;
import java.nio.LongBuffer;
import org.lwjgl.system.MemoryStack;
import org.lwjgl.vulkan.KHRSurface;
import org.lwjgl.vulkan.KHRSwapchain;
import org.lwjgl.vulkan.VK12;
import org.lwjgl.vulkan.VkDevice;
import org.lwjgl.vulkan.VkSemaphoreCreateInfo;
import org.lwjgl.vulkan.VkSurfaceCapabilitiesKHR;

/**
 * Batched presentation for FG x3. Minecraft's VulkanCommandEncoder.submit() blocks the CPU until the submit from two submits ago has finished (MAX_SUBMITS_IN_FLIGHT = 2);
 * the per-image submit of the old x3 path (3 submits per rendered frame) therefore serialised CPU and GPU (GPU busy 60 %). Here ALL the GPU work of a frame (world, both generated
 * frames, their swapchain blits and the real blit) goes out in the single submit Minecraft already does; the generated images are acquired with our own semaphores (Minecraft's
 * ring has only 2) and the three vkQueuePresentKHR calls are issued right after that submit, in display order (generated 1, generated 2, real).
 * Everything runs on the render thread; no VkQueue is touched from another thread.
 */
public final class BatchPresent {
	/** FSR4_BATCH_PRESENT=0 restores the previous one-submit-per-image path. */
	public static final boolean ENABLED = !"0".equals(System.getenv("FSR4_BATCH_PRESENT"));
	public static final int FIFO_EXTRA = Integer.parseInt(System.getenv().getOrDefault("FSR4_FIFO_EXTRA", "0"));
	public static final int IMAGES = Integer.parseInt(System.getenv().getOrDefault("FSR4_SWAPCHAIN_IMAGES", "10"));
	private static final int RING = 16;
	private static final long ACQUIRE_TIMEOUT_NS = 50_000_000L;   // never stall the render thread for long on behalf of an extra frame

	private static final long[] acq = new long[RING];
	private static int ring;
	private static long semDevice;
	private static final int[] pending = new int[4];
	public static volatile int maxExtra = 2;
	private static int pendingN;
	public static int acquireMisses;
	public static volatile GpuSurface.PresentMode configuredMode = GpuSurface.PresentMode.IMMEDIATE;

	private BatchPresent() { }

	private static VulkanGpuSurface backend(GpuSurface s) { return s instanceof FrontendGpuSurface f && f.backend instanceof VulkanGpuSurface v ? v : null; }

	/** FIFO (VSync): the display, not the GPU, limits the rate, and every extra swapchain image is one more vblank of queue latency (measured: batch 54.5 ms vs 33.4 ms with Minecraft's 3 images at 100 Hz, x3) -> the one-submit-per-image path stays in use there. */
	public static boolean fifo() { return configuredMode == GpuSurface.PresentMode.FIFO || configuredMode == GpuSurface.PresentMode.FIFO_RELAXED; }

	public static boolean supported(GpuSurface s) { return ENABLED && !fifo() && backend(s) != null; }

	private static void ensureSemaphores(VkDevice d) {
		if (semDevice == d.address()) return;
		try (MemoryStack st = MemoryStack.stackPush()) {
			VkSemaphoreCreateInfo ci = VkSemaphoreCreateInfo.calloc(st).sType$Default();
			LongBuffer p = st.callocLong(1);
			for (int i = 0; i < RING; i++) { if (VK12.vkCreateSemaphore(d, ci, null, p) != 0) throw new IllegalStateException("vkCreateSemaphore failed"); acq[i] = p.get(0); }
		}
		semDevice = d.address();
	}

	/**
	 * Acquires a swapchain image for a generated frame and records its blit (from {@code view}) into the encoder's pending submission. Returns false (nothing acquired, nothing recorded)
	 * when no image is available quickly or the swapchain is out of date; the caller then simply presents fewer images this frame.
	 */
	public static boolean acquireAndBlit(GpuSurface surface, VulkanCommandEncoder enc, GpuTextureView view) {
		VulkanGpuSurface be = backend(surface);
		if (be == null || be.swapchainOutOfDate || pendingN >= maxExtra) return false;
		VkDevice dev = be.device.vkDevice();
		ensureSemaphores(dev);
		long sem = acq[ring++ % RING];
		int image;
		try (MemoryStack st = MemoryStack.stackPush()) {
			IntBuffer ip = st.ints(-1);
			int r = KHRSwapchain.vkAcquireNextImageKHR(dev, be.swapchain, ACQUIRE_TIMEOUT_NS, sem, 0L, ip);
			if (r != 0 && r != 1000001003) { acquireMisses++; return false; }   // VK_SUCCESS / VK_SUBOPTIMAL_KHR acquired; TIMEOUT/NOT_READY/OUT_OF_DATE did not
			image = ip.get(0);
		}
		long savedSem = be.acquireSemaphores[0]; int savedCur = be.currentAcquireSemaphore, savedImg = be.currentImageIndex;
		try {
			be.acquireSemaphores[0] = sem; be.currentAcquireSemaphore = 0; be.currentImageIndex = image;
			be.blitFromTexture(enc, view);   // waits our semaphore, blits (flip included), signals presentSemaphores[image]
			pending[pendingN++] = image;
		} finally { be.acquireSemaphores[0] = savedSem; be.currentAcquireSemaphore = savedCur; be.currentImageIndex = savedImg; }
		return true;
	}

	/** Destroys our acquire semaphores (call with the GPU idle). */
	public static void release(VkDevice d) {
		if (semDevice == 0 || d == null || d.address() != semDevice) return;
		for (int i = 0; i < RING; i++) { VK12.vkDestroySemaphore(d, acq[i], null); acq[i] = 0; }
		semDevice = 0; ring = 0; pendingN = 0;
	}

	public static boolean hasSemaphores() { return semDevice != 0; }

	public static boolean hasPending() { return pendingN > 0; }

	/** Called in place of Minecraft's present of the real frame, right after its submit: generated images first (in order), then the real one. Returns how many extra images were queued. */
	public static int presentAll(GpuSurface surface) {
		VulkanGpuSurface be = backend(surface);
		int n = pendingN, real = be == null ? -1 : be.currentImageIndex;
		if (be != null) {
			boolean ood = false;
			for (int i = 0; i < n; i++) {
				be.currentImageIndex = pending[i];
				be.swapchainOutOfDate = false;   // a previous present of this batch may have flagged it; still present the rest (and the real frame) so no acquired image is leaked
				be.present();
				ood |= be.swapchainOutOfDate;
			}
			be.currentImageIndex = real;
			be.swapchainOutOfDate = false;
			pendingN = 0;
			surface.present();
			be.swapchainOutOfDate |= ood;
		} else { pendingN = 0; surface.present(); }
		return n;
	}

	/** Drop bookkeeping without presenting (only used when the frame is abandoned before the submit; the images stay acquired until the swapchain is recreated). */
	public static void abandon() { pendingN = 0; }

	/** Swapchain image count: at least {@link #IMAGES} (clamped to the surface limit) so three presents per frame never wait for the presentation engine. */
	public static int imageCount(int original, VulkanGpuSurface be) {
		if (!ENABLED) return original;
		// FIFO queues every present until a vblank: each extra image is one more vblank of input latency, so only the images the extra presents need are added; IMMEDIATE/MAILBOX never queue.
		int want = fifo() ? original + FIFO_EXTRA : Math.max(original, IMAGES);
		try (MemoryStack st = MemoryStack.stackPush()) {
			VkSurfaceCapabilitiesKHR caps = VkSurfaceCapabilitiesKHR.calloc(st);
			if (KHRSurface.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(be.device.vkDevice().getPhysicalDevice(), be.surface, caps) == 0 && caps.maxImageCount() > 0) want = Math.min(want, caps.maxImageCount());
		}
		return Math.max(original, want);
	}
}
