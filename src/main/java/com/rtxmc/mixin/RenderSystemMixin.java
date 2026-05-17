package com.rtxmc.mixin;

import com.mojang.blaze3d.systems.RenderSystem;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

/**
 * Step 3 of GL suppression: kill the {@code glfwSwapBuffers} call inside
 * {@code RenderSystem.flipFrame}. In 1.21.11 the swap moved out of
 * {@code Window.swapBuffers} into {@code RenderSystem.flipFrame}, which calls
 * {@code glfwSwapBuffers} on the main window's handle.
 *
 * Our main window is {@code GLFW_NO_API}, so {@code glfwSwapBuffers} on it
 * would emit a GLFW error (the GL swap chain doesn't exist). VK present runs
 * from {@code LevelRendererMixin} every game frame — that's the only present
 * path now.
 */
@Mixin(RenderSystem.class)
public abstract class RenderSystemMixin {

    @Redirect(method = "flipFrame",
              at = @At(value = "INVOKE",
                       target = "Lorg/lwjgl/glfw/GLFW;glfwSwapBuffers(J)V"))
    private static void rtxmc$suppressGlSwap(long ignoredHandle) {
        // Intentionally empty. VK owns presentation.
    }
}
