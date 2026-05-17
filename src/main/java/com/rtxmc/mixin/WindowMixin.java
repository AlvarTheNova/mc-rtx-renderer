package com.rtxmc.mixin;

import com.rtxmc.RtxMod;
import net.minecraft.client.util.Window;
import org.lwjgl.glfw.GLFW;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

/**
 * GL suppression. The reason this works at all:
 *
 *   - We force the main MC window to be GLFW_NO_API — no GL context attached.
 *   - We create a hidden 1×1 dummy GL window so MC's {@code GL.createCapabilities()}
 *     call (and every {@code RenderSystem.*} / {@code GlStateManager.*} call thereafter)
 *     has a real context to bind to. MC happily renders to that invisible 1×1
 *     framebuffer; nobody ever sees the result.
 *   - {@code Window.swapBuffers} would call {@code glfwSwapBuffers} on a NO_API
 *     window (undefined behavior at best). We no-op it. Our VK present runs from
 *     {@link LevelRendererMixin}.
 *
 * Trade-offs documented in DESIGN.md §3.7. Brittleness: this mixin targets
 * specific call sites by descriptor — if Mojang rearranges {@code Window.<init>},
 * `defaultRequire: 1` in mixins.json will fail loudly at class transform time.
 */
@Mixin(Window.class)
public abstract class WindowMixin {

    /** Hidden GL context that absorbs all of MC's GL calls. */
    private static long rtxmc$dummyGlWindow = 0L;

    @Redirect(method = "<init>",
              at = @At(value = "INVOKE",
                       target = "Lorg/lwjgl/glfw/GLFW;glfwCreateWindow(IILjava/lang/CharSequence;JJ)J"))
    private long rtxmc$createNoApiWindow(int width, int height, CharSequence title,
                                         long monitor, long share) {
        // 1. Create the hidden dummy GL window first so the context exists
        //    by the time MC's GL.createCapabilities() runs against it.
        GLFW.glfwDefaultWindowHints();
        GLFW.glfwWindowHint(GLFW.GLFW_VISIBLE,    GLFW.GLFW_FALSE);
        GLFW.glfwWindowHint(GLFW.GLFW_CLIENT_API, GLFW.GLFW_OPENGL_API);
        GLFW.glfwWindowHint(GLFW.GLFW_CONTEXT_VERSION_MAJOR, 3);
        GLFW.glfwWindowHint(GLFW.GLFW_CONTEXT_VERSION_MINOR, 2);
        GLFW.glfwWindowHint(GLFW.GLFW_OPENGL_PROFILE, GLFW.GLFW_OPENGL_CORE_PROFILE);
        GLFW.glfwWindowHint(GLFW.GLFW_OPENGL_FORWARD_COMPAT, GLFW.GLFW_TRUE);
        rtxmc$dummyGlWindow = GLFW.glfwCreateWindow(1, 1, "rtxmc-gl-sink", 0L, 0L);
        if (rtxmc$dummyGlWindow == 0L) {
            RtxMod.LOG.error("rtxmc: failed to create dummy GL sink window — MC will likely NPE on first GL call");
        } else {
            RtxMod.LOG.info("rtxmc: created hidden GL sink window 0x{}", Long.toHexString(rtxmc$dummyGlWindow));
        }

        // 2. Now create the real MC window with no GL attached — VK will own it.
        GLFW.glfwDefaultWindowHints();
        GLFW.glfwWindowHint(GLFW.GLFW_CLIENT_API, GLFW.GLFW_NO_API);
        GLFW.glfwWindowHint(GLFW.GLFW_VISIBLE,    GLFW.GLFW_TRUE);
        GLFW.glfwWindowHint(GLFW.GLFW_RESIZABLE,  GLFW.GLFW_TRUE);
        long mainWindow = GLFW.glfwCreateWindow(width, height, title, monitor, share);
        RtxMod.LOG.info("rtxmc: created main (NO_API) window {}x{} → 0x{}",
                width, height, Long.toHexString(mainWindow));
        return mainWindow;
    }

    @Redirect(method = "<init>",
              at = @At(value = "INVOKE",
                       target = "Lorg/lwjgl/glfw/GLFW;glfwMakeContextCurrent(J)V"))
    private void rtxmc$redirectMakeContextCurrent(long ignoredMainWindow) {
        // MC tries to make its main window's context current — but the main
        // window has NO_API so there's no context. Bind the dummy instead so
        // GL.createCapabilities() and all subsequent GL calls work.
        if (rtxmc$dummyGlWindow != 0L) {
            GLFW.glfwMakeContextCurrent(rtxmc$dummyGlWindow);
        }
    }

    @Redirect(method = "swapBuffers",
              at = @At(value = "INVOKE",
                       target = "Lorg/lwjgl/glfw/GLFW;glfwSwapBuffers(J)V"))
    private void rtxmc$suppressGlSwap(long ignoredWindow) {
        // VK present runs from LevelRendererMixin every game frame. Dropping
        // the GL swap is what finally lets VK output reach the screen.
    }
}
