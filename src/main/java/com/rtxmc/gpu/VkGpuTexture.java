package com.rtxmc.gpu;

import com.mojang.blaze3d.textures.GpuTexture;
import com.mojang.blaze3d.textures.TextureFormat;

/**
 * Phase 1.6.1b — Vulkan-backed {@link GpuTexture}. Holds a native VkImage
 * + VkDeviceMemory pair under an opaque handle.
 */
public final class VkGpuTexture extends GpuTexture {

    private long nativeHandle;

    public VkGpuTexture(int usage, String label, TextureFormat format,
                        int width, int height, int depthOrLayers, int mipLevels,
                        long handle) {
        super(usage, label, format, width, height, depthOrLayers, mipLevels);
        this.nativeHandle = handle;
    }

    public long handle() { return nativeHandle; }

    @Override public boolean isClosed() { return nativeHandle == 0L; }

    @Override
    public void close() {
        if (nativeHandle == 0L) return;
        VkResNative.destroyTexture(nativeHandle);
        nativeHandle = 0L;
    }
}
