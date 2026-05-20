package com.rtxmc.gpu;

import java.nio.ByteBuffer;

/**
 * Phase 1.6.1b — thin JNI surface for Vulkan-backed Blaze3D resources.
 * All methods are stateless and return opaque {@code long} handles (native
 * pointers / monotonic IDs). Native side: see {@code native/src/vk_resources.cpp}.
 *
 * <p>The wrapper Vk*-classes ({@link VkGpuBuffer} etc.) hold the handles
 * and forward close() / map() / etc. through here.
 */
public final class VkResNative {

    static {
        // Idempotent — NativeBridge already loaded the lib; this just ensures
        // VkResNative's symbols are resolvable when called from any thread
        // even if some VkBackend path is first to touch this class.
        System.loadLibrary("rtx_renderer");
    }

    private VkResNative() {}

    // Buffers
    public static native long createBuffer(int usage, long size, ByteBuffer optionalInitial);
    public static native void destroyBuffer(long handle);
    public static native ByteBuffer mapBuffer(long handle, long offset, long length);
    public static native void unmapBuffer(long handle);

    // Textures
    public static native long createTexture(int usage, int formatCode,
                                            int width, int height,
                                            int depthOrLayers, int mipLevels);
    public static native void destroyTexture(long handle);

    // Texture views
    public static native long createTextureView(long textureHandle, int baseMip, int mipLevels);
    public static native void destroyTextureView(long handle);

    // Samplers
    public static native long createSampler(int addressU, int addressV,
                                            int minFilter, int magFilter,
                                            int maxAnisotropy);
    public static native void destroySampler(long handle);
}
