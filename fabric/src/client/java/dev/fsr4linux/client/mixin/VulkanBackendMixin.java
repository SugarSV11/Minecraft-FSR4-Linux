package dev.fsr4linux.client.mixin;

import com.mojang.renderpearl.backend.vulkan.VulkanBackend;
import com.mojang.renderpearl.backend.vulkan.VulkanPhysicalDevice;
import com.mojang.renderpearl.backend.vulkan.init.FeatureSet;
import dev.fsr4linux.client.Fsr4LinuxClient;
import dev.fsr4linux.client.VulkanCaps;
import dev.fsr4linux.core.GpuCompatibility;
import java.nio.IntBuffer;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import org.lwjgl.system.MemoryStack;
import org.lwjgl.vulkan.VK10;
import org.lwjgl.vulkan.VkExtensionProperties;
import org.lwjgl.vulkan.VkFormatProperties;
import org.lwjgl.vulkan.VkPhysicalDeviceProperties;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.ModifyVariable;

/** Adds the external-memory device extensions to Minecraft's VkDevice (only when the GPU/driver supports all of them). */
@Mixin(VulkanBackend.class)
public abstract class VulkanBackendMixin {
	private static boolean formatSupports(VulkanPhysicalDevice physical, int format, int flags, MemoryStack stack) {
		VkFormatProperties p = VkFormatProperties.malloc(stack);
		VK10.vkGetPhysicalDeviceFormatProperties(physical.vkPhysicalDevice(), format, p);
		return (p.optimalTilingFeatures() & flags) == flags;
	}

	@ModifyVariable(
		method = "createDevice(Lcom/mojang/renderpearl/backend/vulkan/init/FeatureSet;Lcom/mojang/renderpearl/backend/vulkan/VulkanPhysicalDevice;)Lorg/lwjgl/vulkan/VkDevice;",
		at = @At("HEAD"), argsOnly = true, ordinal = 0)
	private static FeatureSet fsr4linux$addExternalMemory(FeatureSet original, FeatureSet unused, VulkanPhysicalDevice physical) {
		List<String> wanted = List.of("VK_KHR_external_memory", "VK_KHR_external_memory_fd", "VK_EXT_external_memory_dma_buf");
		Set<String> available = new HashSet<>();
		try (MemoryStack stack = MemoryStack.stackPush()) {
			IntBuffer count = stack.mallocInt(1);
			VK10.vkEnumerateDeviceExtensionProperties(physical.vkPhysicalDevice(), (CharSequence) null, count, null);
			VkExtensionProperties.Buffer props = VkExtensionProperties.malloc(count.get(0), stack);
			VK10.vkEnumerateDeviceExtensionProperties(physical.vkPhysicalDevice(), (CharSequence) null, count, props);
			for (VkExtensionProperties p : props) available.add(p.extensionNameString());
			VkPhysicalDeviceProperties p = VkPhysicalDeviceProperties.malloc(stack);
			VK10.vkGetPhysicalDeviceProperties(physical.vkPhysicalDevice(), p);
			int sampledStorage = VK10.VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK10.VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
			boolean formats = formatSupports(physical, VK10.VK_FORMAT_R8G8B8A8_UNORM, sampledStorage, stack)
				&& formatSupports(physical, VK10.VK_FORMAT_R32_SFLOAT, sampledStorage, stack)
				&& formatSupports(physical, VK10.VK_FORMAT_R16G16_SFLOAT, sampledStorage, stack);
			boolean linux = System.getProperty("os.name", "").toLowerCase(Locale.ROOT).contains("linux");
			VulkanCaps.gpu = new GpuCompatibility(linux, p.vendorID(), p.deviceType(), p.apiVersion() >>> 22,
				(p.apiVersion() >>> 12) & 0x3ff, available.containsAll(wanted), formats,
				GpuCompatibility.gfxIpFromDriverName(p.deviceNameString()));
			VulkanCaps.deviceId = p.deviceID(); VulkanCaps.deviceName = p.deviceNameString();
			VulkanCaps.deviceProbed = true;
			Fsr4LinuxClient.LOGGER.info("Selected Vulkan GPU: {} vendor=0x{} device=0x{} type={} api={}.{} gfxIp={} bridgeCapable={}",
				VulkanCaps.deviceName, Integer.toHexString(p.vendorID()), Integer.toHexString(p.deviceID()), p.deviceType(),
				VulkanCaps.gpu.apiMajor(), VulkanCaps.gpu.apiMinor(), VulkanCaps.gpu.gfxIp(), VulkanCaps.gpu.bridgeCapable());
		}
		if (!VulkanCaps.gpu.bridgeCapable()) {
			VulkanCaps.externalMemoryEnabled = false;
			VulkanCaps.reason = VulkanCaps.gpu.reason();
			Fsr4LinuxClient.LOGGER.warn("FSR bridge unavailable: {}", VulkanCaps.reason);
			return original;
		}
		List<String> missing = wanted.stream().filter(e -> !available.contains(e)).toList();
		if (!missing.isEmpty()) {
			VulkanCaps.externalMemoryEnabled = false;
			VulkanCaps.reason = "driver lacks " + missing;
			Fsr4LinuxClient.LOGGER.warn("FSR bridge unavailable: Vulkan device lacks {}", missing);
			return original;
		}
		Set<String> ext = new HashSet<>(original.extensions());
		Set<com.mojang.renderpearl.backend.vulkan.init.VulkanFeature> feats = new HashSet<>(original.features());
		if ("1".equals(System.getenv("FSR4_INT64"))) {   // OPT-IN (no measurable gain in game, session 10): FG x3/x4 single-pass shader with 64-bit atomics (falls back to the 2-pass shader when the GPU lacks them)
			var i64 = new FeatureSet("fsr4linux int64 atomics", Set.of(), Set.of(
				new com.mojang.renderpearl.backend.vulkan.init.VulkanFeature(com.mojang.renderpearl.backend.vulkan.VulkanFeatureSets.VK12_FEATURES_STRUCT, "shaderBufferInt64Atomics"),
				new com.mojang.renderpearl.backend.vulkan.init.VulkanFeature(com.mojang.renderpearl.backend.vulkan.VulkanFeatureSets.VK10_FEATURES_STRUCT, "shaderInt64")));
			boolean ok; try { ok = i64.isSupported(physical.vkPhysicalDevice(), available); } catch (Exception e) { ok = false; }
			if (ok) { feats.addAll(i64.features()); VulkanCaps.int64Atomics = true; }
		}
		if (System.getenv("FSR4_PRESENT_PROBE") != null) {   // diagnostic only: lets a probe thread learn when the compositor really displayed each presented image
			var probe = new FeatureSet("fsr4linux present probe", Set.of("VK_KHR_present_id", "VK_KHR_present_wait"), Set.of(
				new com.mojang.renderpearl.backend.vulkan.init.VulkanFeature(new com.mojang.renderpearl.backend.vulkan.init.VulkanPNextStruct(org.lwjgl.vulkan.VkPhysicalDevicePresentIdFeaturesKHR.class), "presentId"),
				new com.mojang.renderpearl.backend.vulkan.init.VulkanFeature(new com.mojang.renderpearl.backend.vulkan.init.VulkanPNextStruct(org.lwjgl.vulkan.VkPhysicalDevicePresentWaitFeaturesKHR.class), "presentWait")));
			boolean probeOk; try { probeOk = probe.isSupported(physical.vkPhysicalDevice(), available); } catch (Exception e) { probeOk = false; }
			if (probeOk) { ext.addAll(probe.extensions()); feats.addAll(probe.features()); VulkanCaps.presentProbeEnabled = true; Fsr4LinuxClient.LOGGER.info("present probe: VK_KHR_present_id/present_wait enabled"); }
			else Fsr4LinuxClient.LOGGER.warn("present probe requested but present_id/present_wait are not supported");
		}
		ext.addAll(wanted);
		List<String> sem = List.of("VK_KHR_external_semaphore", "VK_KHR_external_semaphore_fd");
		VulkanCaps.externalSemaphoreEnabled = available.containsAll(sem);   // optional: without it the bridge keeps the CPU-wait synchronisation
		if (VulkanCaps.externalSemaphoreEnabled) ext.addAll(sem);
		VulkanCaps.externalMemoryEnabled = true;
		VulkanCaps.reason = "";
		Fsr4LinuxClient.LOGGER.info("Enabled Vulkan device extensions for the FSR bridge: {}", wanted);
		return new FeatureSet(original.name(), ext, feats, original.condition());
	}
}
