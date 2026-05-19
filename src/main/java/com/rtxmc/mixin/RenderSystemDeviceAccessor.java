package com.rtxmc.mixin;

import com.mojang.blaze3d.systems.GpuDevice;
import com.mojang.blaze3d.systems.RenderSystem;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

/**
 * Phase 1.6.1a — widen access to {@code RenderSystem.DEVICE} so we can read
 * Mojang's freshly-constructed GlBackend after {@code initRenderer} runs
 * and substitute our wrapper {@code VkBackend}.
 *
 * <p>Mixin lets us hit a private static field via {@code @Accessor}; the
 * generated accessor methods are called on a {@code null} receiver (this
 * works because the field is static — Mixin emits a static-call shim).
 */
@Mixin(RenderSystem.class)
public interface RenderSystemDeviceAccessor {

    @Accessor("DEVICE")
    static GpuDevice rtxmc$getDevice() {
        throw new AssertionError(); // replaced by mixin codegen
    }

    @Accessor("DEVICE")
    static void rtxmc$setDevice(GpuDevice device) {
        throw new AssertionError(); // replaced by mixin codegen
    }
}
