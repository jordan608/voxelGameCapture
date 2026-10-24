package com.voxon.capturerig.mixin;

import com.mojang.blaze3d.framegraph.FrameGraphBuilder;
import com.mojang.renderpearl.api.buffers.GpuBufferSlice;
import com.voxon.capturerig.FaceRig;
import net.minecraft.client.renderer.LevelRenderer;
import net.minecraft.client.renderer.state.level.CameraRenderState;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** The sections the game holds are centred on the camera's, and the see-through ones sorted from where it stands. A
 * face's camera stands up to tens of metres off: it moves neither, so they stay the player's. */
@Mixin(LevelRenderer.class)
public abstract class LevelRendererMixin {
	@Inject(method = "repositionCamera(Lnet/minecraft/client/renderer/state/level/CameraRenderState;)V", at = @At("HEAD"), cancellable = true)
	private void voxon$sectionsStayOnThePlayer(CameraRenderState camera, CallbackInfo ci) {
		if (FaceRig.active) ci.cancel();
	}

	@Inject(method = "scheduleTranslucentSectionResort", at = @At("HEAD"), cancellable = true)
	private void voxon$sortStaysThePlayers(Vec3 cameraPos, CallbackInfo ci) {
		if (FaceRig.active) ci.cancel();
	}

	/** No sky in a face: it has no depth, so it makes no voxels. And the game makes its sky renderer the first time it
	 * draws a sky, bound to the target drawn into then - the screen's, and not a face's, which is replaced when the
	 * faces' size changes (the sky renderer then drew into a texture that was gone, and the game stopped). */
	@Inject(method = "addSkyPass", at = @At("HEAD"), cancellable = true)
	private void voxon$noSkyInFaces(FrameGraphBuilder frame, CameraRenderState cameraState, GpuBufferSlice skyFog, CallbackInfo ci) {
		if (FaceRig.active) ci.cancel();
	}
}
