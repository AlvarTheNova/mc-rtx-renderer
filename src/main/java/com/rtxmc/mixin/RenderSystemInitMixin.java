package com.rtxmc.mixin;

import com.mojang.blaze3d.systems.GpuDevice;
import com.mojang.blaze3d.systems.RenderSystem;
import com.rtxmc.RtxMod;
import com.rtxmc.gpu.VkBackend;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * Phase 1.6.1a — substitute our {@link VkBackend} wrapper for Mojang's
 * freshly-constructed GlBackend at the tail of {@link RenderSystem#initRenderer}.
 *
 * <p>1.6.1a is the diagnostic phase: VkBackend delegates everything to the
 * wrapped GlBackend while logging the first few invocations per method, so
 * we learn the priority order MC actually calls things in. 1.6.1b+ replaces
 * delegations with real Vulkan implementations.
 *
 * <p>Why TAIL and not @Redirect on {@code new GlBackend(...)}: the
 * construction site uses several config args we don't want to reconstruct;
 * just letting Mojang build their GL backend normally and wrapping it
 * after-the-fact is cleaner.
 */
@Mixin(RenderSystem.class)
public abstract class RenderSystemInitMixin {

    @Inject(method = "initRenderer", at = @At("TAIL"))
    private static void rtxmc$wrapDevice(long windowHandle, int debugVerbosity,
                                          boolean syncCpuDebug, Object shaderSourceGetter,
                                          boolean renderDoc, CallbackInfo ci) {
        GpuDevice current = RenderSystemDeviceAccessor.rtxmc$getDevice();
        if (current == null) {
            RtxMod.LOG.warn("rtxmc: RenderSystem.DEVICE was null at initRenderer tail; cannot wrap");
            return;
        }
        if (current instanceof VkBackend) {
            return; // idempotent
        }
        VkBackend wrapper = new VkBackend(current);
        RenderSystemDeviceAccessor.rtxmc$setDevice(wrapper);
    }
}
