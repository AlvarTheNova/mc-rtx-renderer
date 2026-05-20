package com.rtxmc.gpu;

import com.mojang.blaze3d.textures.GpuTexture;
import com.mojang.blaze3d.textures.GpuTextureView;

/**
 * Phase 1.6.1b — Vulkan-backed {@link GpuTextureView}. Thin wrapper around
 * a VkImageView under an opaque native handle.
 */
public final class VkGpuTextureView extends GpuTextureView {

    private long nativeHandle;

    public VkGpuTextureView(GpuTexture texture, int baseMip, int mipLevels, long handle) {
        super(texture, baseMip, mipLevels);
        this.nativeHandle = handle;
    }

    public long handle() { return nativeHandle; }

    @Override public boolean isClosed() { return nativeHandle == 0L; }

    @Override
    public void close() {
        if (nativeHandle == 0L) return;
        VkResNative.destroyTextureView(nativeHandle);
        nativeHandle = 0L;
    }
}
