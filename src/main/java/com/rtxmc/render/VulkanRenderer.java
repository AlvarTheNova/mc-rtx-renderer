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

    public void renderFrame(Camera camera, Matrix4f positionMatrix, Matrix4f proj, float tickDelta) {
        if (!initialized) return;

        bvhUploader.flushDirtyChunks();

        frameParams.clear();
        // Camera pose. Note: Camera.getPos() was removed in 1.21.11 Yarn;
        // the `pos` field is now accessed directly (widened via access widener).
        var pos = camera.pos;
        frameParams.putDouble(pos.x).putDouble(pos.y).putDouble(pos.z);
        frameParams.putFloat(camera.getYaw()).putFloat(camera.getPitch());

        // MC's positionMatrix in 1.21.11 is rotation-only — Mojang's vanilla
        // renderer applies the camera translation per-chunk via separate
        // matrix-stack ops. We need a full view matrix here, so combine the
        // rotation with translate(-camPos):
        //
        //   view = positionMatrix * translate(-px, -py, -pz)
        //
        // For a world point p:  view * p = R * (p - camPos) → eye-space coord.
        //
        // Float precision is fine while camPos is in normal-block range. For
        // far-from-origin worlds (millions of blocks) we'll need camera-
        // relative geometry like vanilla does — Phase 1.4 chunk meshing
        // problem, not this one.
        Matrix4f view = new Matrix4f(positionMatrix);
        view.translate((float) -pos.x, (float) -pos.y, (float) -pos.z);

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
