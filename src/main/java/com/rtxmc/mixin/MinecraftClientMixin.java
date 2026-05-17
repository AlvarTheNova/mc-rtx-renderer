package com.rtxmc.mixin;

import com.rtxmc.RtxMod;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.util.Window;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * Hooks just after the GLFW window exists so we can create a VK_KHR_surface
 * on its HWND. Vanilla still creates a GL context (we tolerate it for now —
 * Phase 1 will suppress it via an earlier hook into Window.<init>).
 */
@Mixin(MinecraftClient.class)
public abstract class MinecraftClientMixin {

    @Inject(method = "<init>", at = @At("TAIL"))
    private void rtxmc$initRenderer(CallbackInfo ci) {
        MinecraftClient mc = (MinecraftClient) (Object) this;
        Window w = mc.getWindow();
        long hwnd = w.getHandle(); // GLFW handle; native side calls glfwGetWin32Window
        int width = w.getFramebufferWidth();
        int height = w.getFramebufferHeight();
        RtxMod.LOG.info("rtxmc: initializing native renderer on GLFW window {} ({}x{})",
                Long.toHexString(hwnd), width, height);
        RtxMod.renderer().init(hwnd, width, height);
    }
}
