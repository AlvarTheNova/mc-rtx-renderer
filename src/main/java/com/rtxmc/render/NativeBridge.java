package com.rtxmc.render;

import java.nio.ByteBuffer;

/**
 * Thin JNI surface to the native renderer (rtx_renderer.dll).
 *
 * Loaded once at class init. The native lib path comes from
 * {@code -Djava.library.path} set in the Loom run config.
 *
 * Design rule: every method here must take or return only primitives /
 * direct ByteBuffers. No object marshalling on the hot path.
 */
public final class NativeBridge {

    static {
        System.loadLibrary("rtx_renderer");
    }

    private NativeBridge() {}

    /** @return 0 on success, non-zero error code otherwise. */
    public static native int init(long glfwWindowHandle, int width, int height);

    public static native void resize(int width, int height);

    /** Upload a chunk's mesh. Vertex + index data laid out per DESIGN.md §3.1. */
    public static native void uploadChunk(
            int chunkX, int chunkY, int chunkZ,
            ByteBuffer vertices, ByteBuffer indices,
            ByteBuffer materialIds);

    public static native void removeChunk(int chunkX, int chunkY, int chunkZ);

    /** Render one frame using packed params (see VulkanRenderer.frameParams layout). */
    public static native void renderFrame(ByteBuffer packedParams);

    public static native void shutdown();
}
