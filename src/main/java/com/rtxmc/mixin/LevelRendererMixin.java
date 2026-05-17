package com.rtxmc.mixin;

import com.mojang.blaze3d.buffers.GpuBufferSlice;
import com.rtxmc.RtxMod;
import net.minecraft.client.render.Camera;
import net.minecraft.client.render.RenderTickCounter;
import net.minecraft.client.render.WorldRenderer;
import net.minecraft.client.util.ObjectAllocator;
import org.joml.Matrix4f;
import org.joml.Vector4f;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * Intercept the world-render entry point and dispatch to the Vulkan path.
 *
 * Method signature targets Yarn 1.21.11+build.5:
 * {@code render(ObjectAllocator, RenderTickCounter, boolean, Camera,
 *               Matrix4f, Matrix4f, Matrix4f, GpuBufferSlice, Vector4f, boolean)}.
 * The render path was substantially refactored vs. 1.21.5 — three matrices
 * now (view, projection, plus a third for ???) and a GpuBufferSlice/Vector4f
 * carrying frustum + fog state. We only need positionMatrix + projectionMatrix
 * for the VK side.
 */
@Mixin(WorldRenderer.class)
public abstract class LevelRendererMixin {
    // Phase 1.2: GL is now redirected into a hidden dummy context, so vanilla
    // world rendering just paints to an invisible 1×1 framebuffer. Cancel it
    // to save the CPU/GPU work.
    private static final boolean CANCEL_VANILLA = true;

    @Inject(
            method = "render(Lnet/minecraft/client/util/ObjectAllocator;Lnet/minecraft/client/render/RenderTickCounter;ZLnet/minecraft/client/render/Camera;Lorg/joml/Matrix4f;Lorg/joml/Matrix4f;Lorg/joml/Matrix4f;Lcom/mojang/blaze3d/buffers/GpuBufferSlice;Lorg/joml/Vector4f;Z)V",
            at = @At("HEAD"),
            cancellable = true
    )
    private void rtxmc$renderLevel(
            ObjectAllocator allocator,
            RenderTickCounter tick,
            boolean renderBlockOutline,
            Camera camera,
            Matrix4f positionMatrix,
            Matrix4f projectionMatrix,
            Matrix4f unknownMatrix,
            GpuBufferSlice frustumOrSettings,
            Vector4f fogColor,
            boolean renderSky,
            CallbackInfo ci
    ) {
        try {
            RtxMod.renderer().renderFrame(camera, positionMatrix, projectionMatrix,
                    tick.getTickProgress(false));
        } catch (Throwable t) {
            RtxMod.LOG.error("rtxmc: renderFrame threw, falling back to vanilla", t);
            return;
        }
        if (CANCEL_VANILLA) {
            ci.cancel();
        }
    }
}
