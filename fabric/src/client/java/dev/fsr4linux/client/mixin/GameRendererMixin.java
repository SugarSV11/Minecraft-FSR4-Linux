package dev.fsr4linux.client.mixin;

import dev.fsr4linux.client.Fsr4LinuxClient;
import net.minecraft.client.renderer.GameRenderer;
import org.joml.Matrix4f;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.ModifyArg;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * Splits the frame into: world at internal resolution (into a private target, with jitter) -> official FSR upscale into the real
 * main target -> HUD/GUI rendered afterwards at native resolution (unchanged vanilla code path).
 */
@Mixin(GameRenderer.class)
public abstract class GameRendererMixin {
	@Inject(method = "render", at = @At("HEAD"))
	private void fsr4linux$frameStart(CallbackInfo ci) {
		Fsr4LinuxClient.onFrameStart((GameRenderer) (Object) this);
	}

	@Inject(method = "render", at = @At(value = "INVOKE", target = "Lnet/minecraft/client/renderer/GameRenderer;preparePostEffects(Ljava/util/List;)V", ordinal = 0))
	private void fsr4linux$beginWorld(CallbackInfo ci) {
		Fsr4LinuxClient.beginWorld((GameRenderer) (Object) this);
	}

	@Inject(method = "render", at = @At(value = "INVOKE", target = "Lnet/minecraft/client/renderer/GameRenderer;applyPostEffects()V", shift = At.Shift.AFTER))
	private void fsr4linux$endWorld(CallbackInfo ci) {
		Fsr4LinuxClient.endWorld((GameRenderer) (Object) this);
	}

	/** Last moment the world depth buffer exists: render3dHud clears it before drawing the first-person hand. */
	@Inject(method = "renderLevel",
		at = @At(value = "INVOKE", target = "Lnet/minecraft/client/renderer/GameRenderer;render3dHud(Lnet/minecraft/client/renderer/state/level/CameraRenderState;Lnet/minecraft/client/renderer/state/level/PlayerRenderState;Lnet/minecraft/client/renderer/state/OptionsRenderState;Z)V"))
	private void fsr4linux$worldPrep(CallbackInfo ci) {
		Fsr4LinuxClient.worldPrep((GameRenderer) (Object) this);
	}

	@ModifyArg(method = "renderLevel",
		at = @At(value = "INVOKE", target = "Lnet/minecraft/client/renderer/ProjectionMatrixBuffer;getBuffer(Lorg/joml/Matrix4f;)Lcom/mojang/renderpearl/api/buffers/GpuBufferSlice;", ordinal = 0),
		index = 0)
	private Matrix4f fsr4linux$jitter(Matrix4f projection) {
		return Fsr4LinuxClient.jitter(projection);
	}
}
