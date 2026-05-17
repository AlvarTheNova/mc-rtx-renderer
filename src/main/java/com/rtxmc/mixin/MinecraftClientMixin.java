package com.rtxmc.mixin;

import com.rtxmc.RtxMod;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.util.Window;
import org.lwjgl.glfw.GLFWNativeWin32;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * Hooks just after the GLFW window exists so we can create a VK_KHR_surface
 * on its HWND.
 *
 * Vanilla still creates a GL context here — Phase 1.2 suppresses it via an
 * earlier hook into Window's GLFW window-hint setup.
 */
@Mixin(MinecraftClient.class)
public abstract class MinecraftClientMixin {

    @Inject(method = "<init>", at = @At("TAIL"))
    private void rtxmc$initRenderer(CallbackInfo ci) {
        MinecraftClient mc = (MinecraftClient) (Object) this;
        Window w = mc.getWindow();
        long glfwWindow = w.getHandle();
        long hwnd = GLFWNativeWin32.glfwGetWin32Window(glfwWindow);
        int width = w.getFramebufferWidth();
        int height = w.getFramebufferHeight();
        if (hwnd == 0L) {
            RtxMod.LOG.error("rtxmc: glfwGetWin32Window returned NULL — Windows-only build, cannot proceed");
            return;
        }
        RtxMod.LOG.info("rtxmc: initializing native renderer (glfw={} hwnd={} size={}x{})",
                Long.toHexString(glfwWindow), Long.toHexString(hwnd), width, height);
        RtxMod.renderer().init(hwnd, width, height);
    }
}
