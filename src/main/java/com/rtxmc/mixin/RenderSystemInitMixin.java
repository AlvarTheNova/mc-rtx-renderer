package com.rtxmc.mixin;

import com.mojang.blaze3d.systems.GpuDevice;
import com.mojang.blaze3d.systems.RenderSystem;
import com.rtxmc.RtxMod;
import com.rtxmc.gpu.VkBackend;
import net.minecraft.client.gl.ShaderSourceGetter;
import org.lwjgl.glfw.GLFW;
import org.lwjgl.glfw.GLFWNativeWin32;
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

    /**
     * Phase 1.6.1c — bring up the native Vulkan context BEFORE Mojang's
     * initRenderer creates the first GpuBuffer / GpuTexture for the GUI
     * pipeline. Without this, VkBackend's shadow-exec (and eventually its
     * real impl) would see {@code ctx().device == VK_NULL_HANDLE} and bail
     * for everything created during initRenderer.
     *
     * <p>{@code VulkanRenderer.init()} is idempotent — the old
     * {@code MinecraftClientMixin.<init>$TAIL} call still fires later but
     * is a no-op now.
     */
    @Inject(method = "initRenderer", at = @At("HEAD"))
    private static void rtxmc$initBeforeBackend(long windowHandle, int debugVerbosity,
                                                  boolean syncCpuDebug, ShaderSourceGetter shaderSourceGetter,
                                                  boolean renderDoc, CallbackInfo ci) {
        long hwnd = GLFWNativeWin32.glfwGetWin32Window(windowHandle);
        if (hwnd == 0L) {
            RtxMod.LOG.error("rtxmc: glfwGetWin32Window returned NULL — Windows-only build");
            return;
        }
        int[] w = new int[1], h = new int[1];
        GLFW.glfwGetFramebufferSize(windowHandle, w, h);
        RtxMod.LOG.info("rtxmc: initializing native renderer EARLY (initRenderer HEAD) glfw={} hwnd={} size={}x{}",
                Long.toHexString(windowHandle), Long.toHexString(hwnd), w[0], h[0]);
        RtxMod.renderer().init(hwnd, w[0], h[0]);
    }

    @Inject(method = "initRenderer", at = @At("TAIL"))
    private static void rtxmc$wrapDevice(long windowHandle, int debugVerbosity,
                                          boolean syncCpuDebug, ShaderSourceGetter shaderSourceGetter,
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
