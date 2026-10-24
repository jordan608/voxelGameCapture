package com.voxon.capturerig.mixin;

import com.mojang.blaze3d.systems.RenderSystem;
import com.voxon.capturerig.FaceRig;
import net.minecraft.client.Camera;
import net.minecraft.client.DeltaTracker;
import net.minecraft.client.renderer.culling.Frustum;
import net.minecraft.world.phys.Vec3;
import org.joml.Matrix4f;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** While a face is drawn, the game's camera stands where the face's does, turned as it is, detached from the player (so
 * the player is drawn), with the face's lens and its near plane on the volume's face. */
@Mixin(Camera.class)
public abstract class CameraMixin {
	@Shadow private boolean detached;
	@Shadow private float depthFar;

	@Shadow protected abstract void setRotation(float yRot, float xRot);

	@Shadow protected abstract void setPosition(Vec3 position);

	@Shadow private void setupPerspective(float zNear, float zFar, float fov, float width, float height) { }

	@Inject(method = "alignWithEntity", at = @At("TAIL"))
	private void voxon$facePose(float partialTicks, CallbackInfo ci) {
		if (!FaceRig.active) return;
		this.setRotation(FaceRig.yaw, FaceRig.pitch);
		this.setPosition(FaceRig.pos);
		this.detached = true;
	}

	@Shadow private Frustum cullFrustum;
	@Shadow private Vec3 position;

	@Shadow public abstract Matrix4f getViewRotationMatrix(Matrix4f dest);

	@Inject(method = "update", at = @At("TAIL"))
	private void voxon$faceLens(DeltaTracker deltaTracker, CallbackInfo ci) {
		if (!FaceRig.active) return;
		float aspect = (float)FaceRig.target.width / FaceRig.target.height;
		this.setupPerspective(FaceRig.near, FaceRig.reach, FaceRig.fovV, FaceRig.target.width, FaceRig.target.height);
		// Culled to the face's own lens, from its near plane to its far one: what lies beyond the volume is not drawn.
		Matrix4f cull = new Matrix4f().perspective((float)Math.toRadians(FaceRig.fovV), aspect, FaceRig.near, FaceRig.reach,
			RenderSystem.getDevice().getDeviceInfo().isZZeroToOne());
		this.cullFrustum = new Frustum(this.getViewRotationMatrix(new Matrix4f()), cull);
		this.cullFrustum.prepare(this.position.x(), this.position.y(), this.position.z());
	}
}
