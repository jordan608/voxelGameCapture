package com.voxon.capturerig.mixin;

import com.voxon.capturerig.FaceRig;
import java.util.List;
import net.minecraft.client.renderer.SectionOcclusionGraph;
import net.minecraft.client.renderer.ViewArea;
import net.minecraft.client.renderer.chunk.SectionRenderDispatcher;
import net.minecraft.client.renderer.culling.Frustum;
import net.minecraft.client.renderer.state.level.CameraRenderState;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** Which chunk sections a camera draws: the game works that out in the background, from whichever camera asked last,
 * and keeps it - one graph for every camera. The faces' cameras would each start it over from where they stand, and
 * a face would draw the sections found for another camera (often the player's, culled to the player's view: the
 * ground round the player missing from the volume). So a face neither starts the graph over nor reads it: it draws
 * every section its own frustum holds, and the graph stays the player's. */
@Mixin(SectionOcclusionGraph.class)
public abstract class SectionOcclusionGraphMixin {
	@Shadow private ViewArea viewArea;

	@Inject(method = "invalidateIfNeeded", at = @At("HEAD"), cancellable = true)
	private void voxon$notForFaces(CameraRenderState camera, int fov, CallbackInfo ci) {
		if (FaceRig.active) ci.cancel();
	}

	@Inject(method = "scheduleFullUpdate", at = @At("HEAD"), cancellable = true)
	private void voxon$noFullUpdateForFaces(CameraRenderState camera, CallbackInfo ci) {
		if (FaceRig.active) ci.cancel();
	}

	@Inject(method = "addSectionsInFrustum", at = @At("HEAD"), cancellable = true)
	private void voxon$everySectionInTheFace(Frustum frustum, List<SectionRenderDispatcher.RenderSection> visible,
		List<SectionRenderDispatcher.RenderSection> nearby, CallbackInfo ci) {
		if (!FaceRig.active || this.viewArea == null) return;
		for (SectionRenderDispatcher.RenderSection section : ((ViewAreaAccessor)this.viewArea).voxon$sections())
			if (section != null && frustum.isVisible(section.getBoundingBox())) visible.add(section);
		ci.cancel();
	}
}
