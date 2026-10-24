package dev.fsr4linux.client.mixin;

import com.mojang.renderpearl.backend.vulkan.VulkanGpuSurface;
import dev.fsr4linux.client.BatchPresent;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.ModifyArg;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import com.mojang.renderpearl.api.device.GpuSurface;
import org.lwjgl.vulkan.KHRSwapchain;
import org.lwjgl.vulkan.VkPresentInfoKHR;
import org.lwjgl.vulkan.VkQueue;
import org.spongepowered.asm.mixin.injection.Redirect;
import dev.fsr4linux.client.PresentProbe;

/** More swapchain images for batched FG presentation (see {@link BatchPresent}). */
@Mixin(VulkanGpuSurface.class)
public abstract class VulkanGpuSurfaceMixin {
	/** Diagnostic: attach a present id to every present (see PresentProbe). A plain pass-through unless FSR4_PRESENT_PROBE is set. */
	@Redirect(method = "present", at = @At(value = "INVOKE", target = "Lorg/lwjgl/vulkan/KHRSwapchain;vkQueuePresentKHR(Lorg/lwjgl/vulkan/VkQueue;Lorg/lwjgl/vulkan/VkPresentInfoKHR;)I"))
	private int fsr4linux$present(VkQueue queue, VkPresentInfoKHR info) {
		VulkanGpuSurface self = (VulkanGpuSurface) (Object) this;
		return PresentProbe.present(queue, info, self.swapchain, self.device.vkDevice());
	}

	@Inject(method = "configure", at = @At("HEAD"))
	private void fsr4linux$mode(GpuSurface.Configuration config, CallbackInfo ci) { BatchPresent.configuredMode = config.presentMode(); }

	@ModifyArg(method = "configure", at = @At(value = "INVOKE", target = "Lorg/lwjgl/vulkan/VkSwapchainCreateInfoKHR;minImageCount(I)Lorg/lwjgl/vulkan/VkSwapchainCreateInfoKHR;"), index = 0)
	private int fsr4linux$imageCount(int original) { return BatchPresent.imageCount(original, (VulkanGpuSurface) (Object) this); }
}
