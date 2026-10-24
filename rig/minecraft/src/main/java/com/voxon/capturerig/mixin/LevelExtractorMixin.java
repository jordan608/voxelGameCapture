package com.voxon.capturerig.mixin;

import com.voxon.capturerig.FaceRig;
import net.minecraft.client.SectionUpdateTracker;
import net.minecraft.client.renderer.SectionOcclusionGraph;
import net.minecraft.client.renderer.extract.LevelExtractor;
import net.minecraft.core.SectionPos;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

/** The game picks a camera's sections again only when it turns 2 degrees or the graph has news. A face picks its own
 * every time (SectionOcclusionGraphMixin), and the player's frame after the faces picks the player's again. The game's
 * record of which sections need building stays centred on the player. */
@Mixin(LevelExtractor.class)
public abstract class LevelExtractorMixin {
	private static final String EXTRACT = "extract(Lnet/minecraft/client/DeltaTracker;Lnet/minecraft/client/Camera;F)V";

	@Redirect(method = EXTRACT, at = @At(value = "INVOKE", target = "Lnet/minecraft/client/renderer/SectionOcclusionGraph;consumeFrustumUpdate()Z"))
	private boolean voxon$sectionsForEachCamera(SectionOcclusionGraph graph) {
		if (FaceRig.active) return true;
		boolean afterFaces = FaceRig.consumeFacesDrawn();
		return graph.consumeFrustumUpdate() || afterFaces;
	}

	@Redirect(method = EXTRACT, at = @At(value = "INVOKE", target = "Lnet/minecraft/client/SectionUpdateTracker;repositionCamera(Lnet/minecraft/core/SectionPos;)V"))
	private void voxon$trackerStaysOnThePlayer(SectionUpdateTracker tracker, SectionPos cameraSection) {
		if (!FaceRig.active) tracker.repositionCamera(cameraSection);
	}
}
