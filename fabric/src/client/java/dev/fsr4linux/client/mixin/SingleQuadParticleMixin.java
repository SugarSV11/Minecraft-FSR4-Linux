package dev.fsr4linux.client.mixin;

import dev.fsr4linux.client.ParticleMotion;
import net.minecraft.client.particle.SingleQuadParticle;
import net.minecraft.client.renderer.state.level.QuadParticleRenderState;
import org.joml.Quaternionf;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** Records every extracted quad particle (position, size, velocity) for the particle motion-vector pass. */
@Mixin(SingleQuadParticle.class)
public abstract class SingleQuadParticleMixin {
	@Inject(method = "extractRotatedQuad(Lnet/minecraft/client/renderer/state/level/QuadParticleRenderState;Lorg/joml/Quaternionf;FFFF)V", at = @At("HEAD"))
	private void fsr4linux$particle(QuadParticleRenderState state, Quaternionf rotation, float x, float y, float z, float partialTickTime, CallbackInfo ci) {
		ParticleMotion.add((SingleQuadParticle) (Object) this, partialTickTime, x, y, z);
	}
}
