package com.voxon.capturerig.mixin;

import com.mojang.renderpearl.api.buffers.GpuBuffer;
import com.mojang.renderpearl.api.textures.GpuTexture;
import com.mojang.renderpearl.backend.opengl.GlStateManager;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** OpenGL: reading a depth texture back sets the copy framebuffer's read buffer to none, and the game never sets it
 * back, so the next colour read-back fails ("No color buffer", GL error 1282). The game itself reads no depth back;
 * the rig reads both, every capture. Its colour read buffer, set again before each colour read. */
@Mixin(targets = "com.mojang.renderpearl.backend.opengl.GlCommandEncoder")
public abstract class GlCommandEncoderMixin {
	@Inject(method = "copyTextureToBuffer(Lcom/mojang/renderpearl/api/textures/GpuTexture;Lcom/mojang/renderpearl/api/buffers/GpuBuffer;JLjava/lang/Runnable;IIIII)V",
		at = @At(value = "INVOKE", target = "Lcom/mojang/renderpearl/backend/opengl/GlStateManager;_readPixels(IIIIIIJ)V"))
	private void voxon$colourReadBuffer(GpuTexture source, GpuBuffer destination, long offset, Runnable callback, int mipLevel,
		int x, int y, int width, int height, CallbackInfo ci) {
		if (!source.getFormat().hasDepthAspect()) GlStateManager._glReadBuffer(0x8CE0);    // GL_COLOR_ATTACHMENT0
	}
}
