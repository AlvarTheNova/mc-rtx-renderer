package com.rtxmc.mixin;

import com.rtxmc.RtxMod;
import net.minecraft.client.render.Camera;
import net.minecraft.client.render.GameRenderer;
import net.minecraft.client.render.LightmapTextureManager;
import net.minecraft.client.render.RenderTickCounter;
import net.minecraft.client.render.WorldRenderer;
import org.joml.Matrix4f;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * Intercept the world-render entry point and dispatch to the Vulkan path.
 * In Phase 1 we still let vanilla run so we can A/B compare; flip
 * {@code CANCEL_VANILLA} once VK output is at parity.
 *
 * Method signature here targets 1.21.5 Yarn names; bump when MC updates.
 */
@Mixin(WorldRenderer.class)
public abstract class LevelRendererMixin {
    // Phase 1.2: GL is now redirected into a hidden dummy context, so vanilla
    // world rendering just paints to an invisible 1×1 framebuffer. Cancel it
    // to save the CPU/GPU work.
    private static final boolean CANCEL_VANILLA = true;

    @Inject(
            method = "render(Lnet/minecraft/client/render/RenderTickCounter;ZLnet/minecraft/client/render/Camera;Lnet/minecraft/client/render/GameRenderer;Lnet/minecraft/client/render/LightmapTextureManager;Lorg/joml/Matrix4f;Lorg/joml/Matrix4f;)V",
            at = @At("HEAD"),
            cancellable = true
    )
    private void rtxmc$renderLevel(
            RenderTickCounter tick,
            boolean renderBlockOutline,
            Camera camera,
            GameRenderer gameRenderer,
            LightmapTextureManager lightmap,
            Matrix4f positionMatrix,
            Matrix4f projectionMatrix,
            CallbackInfo ci
    ) {
        try {
            RtxMod.renderer().renderFrame(camera, positionMatrix, projectionMatrix, tick.getTickDelta(false));
        } catch (Throwable t) {
            RtxMod.LOG.error("rtxmc: renderFrame threw, falling back to vanilla", t);
            return;
        }
        if (CANCEL_VANILLA) {
            ci.cancel();
        }
    }
}
