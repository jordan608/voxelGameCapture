package com.voxon.capturerig.mixin;

import net.minecraft.client.RotatingSectionStorage;
import net.minecraft.client.renderer.ViewArea;
import net.minecraft.client.renderer.chunk.SectionRenderDispatcher;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

/** Every chunk section the game holds round the player, for a face to pick its own from (SectionOcclusionGraphMixin). */
@Mixin(ViewArea.class)
public interface ViewAreaAccessor {
	@Accessor("sections")
	RotatingSectionStorage<SectionRenderDispatcher.RenderSection> voxon$sections();
}
