package com.voxon.capturerig.mixin;

import com.voxon.capturerig.FaceRig;
import net.minecraft.client.DeltaTracker;
import net.minecraft.client.Minecraft;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** The faces, drawn before the player's own frame is updated, extracted and rendered. */
@Mixin(Minecraft.class)
public abstract class MinecraftMixin {
	@Shadow @Final private DeltaTracker.Timer deltaTracker;

	@Inject(method = "renderFrame", at = @At(value = "INVOKE",
		target = "Lnet/minecraft/client/renderer/GameRenderer;update(Lnet/minecraft/client/DeltaTracker;)V"))
	private void voxon$faces(boolean advanceGameTime, CallbackInfo ci) {
		if (advanceGameTime) FaceRig.beforeFrame((Minecraft)(Object)this, this.deltaTracker);
	}
}
