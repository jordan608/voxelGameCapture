package com.voxon.capturerig.mixin;

import com.mojang.blaze3d.pipeline.RenderTarget;
import com.voxon.capturerig.FaceRig;
import net.minecraft.client.TextureFilteringMethod;
import net.minecraft.client.renderer.GameRenderer;
import net.minecraft.client.renderer.GlobalSettingsUniform;
import net.minecraft.client.renderer.state.GameRenderState;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** While a face is drawn, the level renderer draws into the face's target in place of the screen's. */
@Mixin(GameRenderer.class)
public abstract class GameRendererMixin {
	@Shadow @Final private GlobalSettingsUniform globalSettingsUniform;
	@Shadow @Final private GameRenderState gameRenderState;

	@Inject(method = "mainRenderTarget", at = @At("HEAD"), cancellable = true)
	private void voxon$faceTarget(CallbackInfoReturnable<RenderTarget> cir) {
		if (FaceRig.active) cir.setReturnValue(FaceRig.target);
	}

	/** The terrain's shaders place it by the camera's position in the global settings block, which the game writes once
	 * a frame, for the player's camera, before it draws the level: drawn without it, a face's terrain stood where the
	 * player's eye is while its entities stood where the face's camera is. Each face writes its own camera and size;
	 * the player's frame writes the player's again. */
	@Inject(method = "renderLevel", at = @At("HEAD"))
	private void voxon$faceCamera(CallbackInfo ci) {
		if (!FaceRig.active) return;
		GameRenderState s = this.gameRenderState;
		this.globalSettingsUniform.update(FaceRig.target.width, FaceRig.target.height, s.optionsRenderState.glintStrength,
			s.levelRenderState.gameTime, s.levelRenderState.worldPartialTicks, s.optionsRenderState.menuBackgroundBlurriness,
			s.levelRenderState.cameraRenderState.pos, s.optionsRenderState.textureFiltering == TextureFilteringMethod.RGSS);
	}
}
