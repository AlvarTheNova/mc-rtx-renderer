package com.rtxmc.render;

import com.rtxmc.RtxMod;
import net.minecraft.client.render.Camera;
import org.joml.Matrix4f;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * Java-side facade for the native renderer. Owns no Vulkan state itself —
 * everything lives behind {@link NativeBridge}. This class's job is to:
 *   1. Marshal per-frame parameters into a single off-heap buffer
 *   2. Make exactly one JNI call per frame ({@link NativeBridge#renderFrame})
 *   3. Hand world-data deltas to {@link VoxelBvhUploader}
 */
public final class VulkanRenderer {

    /** Per-frame params packed into off-heap memory for zero-copy JNI. */
    private final ByteBuffer frameParams = ByteBuffer
            .allocateDirect(256)
            .order(ByteOrder.nativeOrder());

    private final VoxelBvhUploader bvhUploader = new VoxelBvhUploader();
    private boolean initialized = false;
    private int framebufferWidth;
    private int framebufferHeight;

    public void init(long glfwWindowHandle, int width, int height) {
        if (initialized) return;
        this.framebufferWidth = width;
        this.framebufferHeight = height;
        int result = NativeBridge.init(glfwWindowHandle, width, height);
        if (result != 0) {
            RtxMod.LOG.error("rtxmc native init failed: code {}", result);
            return;
        }
        initialized = true;
        RtxMod.LOG.info("rtxmc native renderer initialized");
    }

    public void resize(int width, int height) {
        if (!initialized) return;
        if (width == framebufferWidth && height == framebufferHeight) return;
        this.framebufferWidth = width;
        this.framebufferHeight = height;
        NativeBridge.resize(width, height);
    }

    public void renderFrame(Camera camera, Matrix4f view, Matrix4f proj, float tickDelta) {
        if (!initialized) return;

        bvhUploader.flushDirtyChunks();

        frameParams.clear();
        // Camera pose. Note: Camera.getPos() was removed in 1.21.11 Yarn;
        // the `pos` field is now accessed directly (widened via access widener).
        var pos = camera.pos;
        frameParams.putDouble(pos.x).putDouble(pos.y).putDouble(pos.z);
        frameParams.putFloat(camera.getYaw()).putFloat(camera.getPitch());
        // View + projection
        putMat4(frameParams, view);
        putMat4(frameParams, proj);
        frameParams.putFloat(tickDelta);
        frameParams.flip();

        NativeBridge.renderFrame(frameParams);
    }

    public void shutdown() {
        if (!initialized) return;
        NativeBridge.shutdown();
        initialized = false;
    }

    private static void putMat4(ByteBuffer b, Matrix4f m) {
        float[] tmp = new float[16];
        m.get(tmp);
        for (float f : tmp) b.putFloat(f);
    }
}
