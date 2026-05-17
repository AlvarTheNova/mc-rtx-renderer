package com.rtxmc.mixin;

import com.rtxmc.RtxMod;
import net.minecraft.client.render.GameRenderer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * Resize plumbing: when the framebuffer changes size we need to rebuild the
 * VK swapchain and DLSS render targets.
 */
@Mixin(GameRenderer.class)
public abstract class GameRendererMixin {

    @Inject(method = "onResized(II)V", at = @At("TAIL"))
    private void rtxmc$onResized(int width, int height, CallbackInfo ci) {
        if (RtxMod.renderer() != null) {
            RtxMod.renderer().resize(width, height);
        }
    }
}
