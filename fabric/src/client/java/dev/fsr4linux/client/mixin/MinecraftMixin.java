package dev.fsr4linux.client.mixin;

import com.mojang.renderpearl.api.device.GpuSurface;
import dev.fsr4linux.client.Fsr4LinuxClient;
import dev.fsr4linux.client.FsrRuntime;
import net.minecraft.client.Minecraft;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.Redirect;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * Frame Generation presents TWO images per rendered frame: the generated one first, then the real one. Minecraft's own blit + present of the real frame
 * happens right after the first hook; the second hook (after present) only timestamps it for the latency / pacing statistics.
 */
@Mixin(Minecraft.class)
public abstract class MinecraftMixin {
	@Shadow @Final private GpuSurface windowSurface;

	/** Minecraft closes the Vulkan device here; release the bridge's child objects first. */
	@Inject(method = "close", at = @At("HEAD"))
	private void fsr4linux$beforeDeviceClose(CallbackInfo ci) {
		Fsr4LinuxClient.LOGGER.info("Releasing FSR resources before Minecraft closes the Vulkan device");
		FsrRuntime.get().shutdown();
	}

	@Inject(method = "renderFrame",
		at = @At(value = "INVOKE_STRING", target = "Lnet/minecraft/util/profiling/ProfilerFiller;push(Ljava/lang/String;)V", args = "ldc=swapchainBlit"))
	private void fsr4linux$beforeBlit(boolean advanceGameTime, CallbackInfo ci) {
		Fsr4LinuxClient.presentHook((Minecraft) (Object) this, this.windowSurface);
	}

	@Inject(method = "renderFrame",
		at = @At(value = "INVOKE_STRING", target = "Lnet/minecraft/util/profiling/ProfilerFiller;popPush(Ljava/lang/String;)V", args = "ldc=endFrame"))
	private void fsr4linux$afterPresent(boolean advanceGameTime, CallbackInfo ci) {
		Fsr4LinuxClient.afterRealPresent();
	}

	/** Batched FG presentation: the generated images (blitted inside the single submit) are presented right before the real one. */
	@Redirect(method = "renderFrame", at = @At(value = "INVOKE", target = "Lcom/mojang/renderpearl/api/device/GpuSurface;present()V"))
	private void fsr4linux$present(GpuSurface surface) {
		Fsr4LinuxClient.presentReal(surface);
	}
}
