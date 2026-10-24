package dev.fsr4linux.client.mixin;

import com.mojang.blaze3d.pipeline.RenderTarget;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.SkyRenderer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

/**
 * SkyRenderer keeps the RenderTarget it was constructed with. While the world phase renders into the internal-resolution target
 * the sky must follow the CURRENT main target instead, otherwise it draws into the wrong (or a destroyed) target.
 */
@Mixin(SkyRenderer.class)
public abstract class SkyRendererMixin {
	@Redirect(method = "render", at = @At(value = "FIELD", target = "Lnet/minecraft/client/renderer/SkyRenderer;renderTarget:Lcom/mojang/blaze3d/pipeline/RenderTarget;"))
	private RenderTarget fsr4linux$currentTarget(SkyRenderer self) {
		return Minecraft.getInstance().gameRenderer.mainRenderTarget();
	}
}
