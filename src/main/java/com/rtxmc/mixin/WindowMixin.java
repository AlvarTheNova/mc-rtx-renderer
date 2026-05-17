package com.rtxmc.mixin;

import com.rtxmc.RtxGlState;
import com.rtxmc.RtxMod;
import net.minecraft.client.util.Window;
import org.lwjgl.glfw.GLFW;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

/**
 * Step 1 of GL suppression: replace MC's main {@code glfwCreateWindow} call
 * so the resulting window has {@code GLFW_NO_API} (no GL context will ever
 * attach to it — VK owns the surface). Also creates a hidden 1×1 GL window
 * up-front so {@link GlBackendMixin} can bind a real GL context for MC's
 * {@code GL.createCapabilities()} and subsequent {@code RenderSystem} calls.
 *
 * Important note about 1.21.11: Mojang refactored {@code Window.<init>} so
 * it no longer calls {@code glfwMakeContextCurrent} or
 * {@code GL.createCapabilities} directly — those moved into the new
 * {@link net.minecraft.client.gl.GlBackend} (their preparation for non-GL
 * backends). The matching redirects therefore live in
 * {@link GlBackendMixin} and {@link RenderSystemMixin}, not here.
 */
@Mixin(Window.class)
public abstract class WindowMixin {

    @Redirect(method = "<init>",
              at = @At(value = "INVOKE",
                       target = "Lorg/lwjgl/glfw/GLFW;glfwCreateWindow(IILjava/lang/CharSequence;JJ)J"))
    private long rtxmc$createNoApiWindow(int width, int height, CharSequence title,
                                         long monitor, long share) {
        // 1. Create the hidden dummy GL window first. GlBackend.<init> will
        //    later bind this as the current GL context (via GlBackendMixin).
        GLFW.glfwDefaultWindowHints();
        GLFW.glfwWindowHint(GLFW.GLFW_VISIBLE,    GLFW.GLFW_FALSE);
        GLFW.glfwWindowHint(GLFW.GLFW_CLIENT_API, GLFW.GLFW_OPENGL_API);
        GLFW.glfwWindowHint(GLFW.GLFW_CONTEXT_VERSION_MAJOR, 3);
        GLFW.glfwWindowHint(GLFW.GLFW_CONTEXT_VERSION_MINOR, 2);
        GLFW.glfwWindowHint(GLFW.GLFW_OPENGL_PROFILE, GLFW.GLFW_OPENGL_CORE_PROFILE);
        GLFW.glfwWindowHint(GLFW.GLFW_OPENGL_FORWARD_COMPAT, GLFW.GLFW_TRUE);
        long dummy = GLFW.glfwCreateWindow(1, 1, "rtxmc-gl-sink", 0L, 0L);
        if (dummy == 0L) {
            RtxMod.LOG.error("rtxmc: failed to create dummy GL sink window — GlBackend will likely NPE");
        } else {
            RtxGlState.setDummyGlWindow(dummy);
            RtxMod.LOG.info("rtxmc: created hidden GL sink window 0x{}", Long.toHexString(dummy));
        }

        // 2. Now the real main window — VK only, no GL context attached.
        GLFW.glfwDefaultWindowHints();
        GLFW.glfwWindowHint(GLFW.GLFW_CLIENT_API, GLFW.GLFW_NO_API);
        GLFW.glfwWindowHint(GLFW.GLFW_VISIBLE,    GLFW.GLFW_TRUE);
        GLFW.glfwWindowHint(GLFW.GLFW_RESIZABLE,  GLFW.GLFW_TRUE);
        long mainWindow = GLFW.glfwCreateWindow(width, height, title, monitor, share);
        RtxMod.LOG.info("rtxmc: created main (NO_API) window {}x{} → 0x{}",
                width, height, Long.toHexString(mainWindow));
        return mainWindow;
    }
}
