package dev.fsr4linux.client.mixin;

import dev.fsr4linux.client.ParticleMotion;
import net.minecraft.client.particle.ParticleEngine;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** A new particle extraction starts: forget last frame's list. */
@Mixin(ParticleEngine.class)
public abstract class ParticleEngineMixin {
	@Inject(method = "extract", at = @At("HEAD"))
	private void fsr4linux$begin(CallbackInfo ci) { ParticleMotion.begin(); }
}
