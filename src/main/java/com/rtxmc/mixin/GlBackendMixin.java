package com.rtxmc.mixin;

import com.rtxmc.RtxGlState;
import com.rtxmc.RtxMod;
import net.minecraft.client.gl.GlBackend;
import org.lwjgl.glfw.GLFW;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

/**
 * Step 2 of GL suppression: when MC's {@link GlBackend} constructor tries to
 * make the main window's GL context current — which is impossible because
 * {@link WindowMixin} forced the main window to {@code GLFW_NO_API} — we
 * redirect it to bind our hidden dummy GL window instead.
 *
 * After this redirect runs, MC's subsequent {@code GL.createCapabilities()}
 * call (the very next statement in {@code GlBackend.<init>}) loads function
 * pointers against the dummy context. Every {@code RenderSystem.*} /
 * {@code GlStateManager.*} call vanilla makes thereafter is a real GL call
 * landing in the dummy's 1×1 invisible framebuffer. See DESIGN.md §3.7.
 */
@Mixin(GlBackend.class)
public abstract class GlBackendMixin {

    @Redirect(method = "<init>",
              at = @At(value = "INVOKE",
                       target = "Lorg/lwjgl/glfw/GLFW;glfwMakeContextCurrent(J)V"))
    private void rtxmc$bindDummyContext(long ignoredMainWindow) {
        long dummy = RtxGlState.dummyGlWindow();
        if (dummy != 0L) {
            GLFW.glfwMakeContextCurrent(dummy);
            RtxMod.LOG.info("rtxmc: GlBackend bound to dummy GL ctx (was about to bind 0x{})",
                    Long.toHexString(ignoredMainWindow));
        } else {
            // Fall back to vanilla behavior — will likely crash on NO_API
            // window, but at least the failure path matches MC's expectations.
            RtxMod.LOG.warn("rtxmc: no dummy GL window available, falling through to vanilla bind");
            GLFW.glfwMakeContextCurrent(ignoredMainWindow);
        }
    }
}
