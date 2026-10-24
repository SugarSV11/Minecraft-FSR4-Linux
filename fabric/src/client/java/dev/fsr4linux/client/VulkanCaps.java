package dev.fsr4linux.client;

import dev.fsr4linux.core.GpuCompatibility;

/** What the mixin on VulkanBackend.createDevice managed to enable on Minecraft's VkDevice. */
public final class VulkanCaps {
	public static volatile GpuCompatibility gpu = new GpuCompatibility(false, 0, 0, 0, 0, false, false, 0);
	public static volatile boolean deviceProbed;
	public static volatile int deviceId;
	public static volatile String deviceName = "unknown"; // diagnostics only; policy uses Vulkan IDs, type, formats and the SDK
	public static volatile boolean externalMemoryEnabled = false;
	public static volatile boolean externalSemaphoreEnabled = false;   // VK_KHR_external_semaphore(_fd): GPU-to-GPU sync with the helper
	public static volatile boolean int64Atomics = false;   // shaderBufferInt64Atomics + shaderInt64 enabled on the device (single-pass FG x3/x4 shader)
	public static volatile boolean presentProbeEnabled = false;   // VK_KHR_present_id + present_wait enabled on the device (diagnostic probe, FSR4_PRESENT_PROBE=1)
	public static volatile String reason = "device not created yet";
	private VulkanCaps() { }
}
