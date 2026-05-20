package com.rtxmc.gpu;

import com.mojang.blaze3d.buffers.GpuBuffer;
import java.nio.ByteBuffer;

/**
 * Phase 1.6.1b — Vulkan-backed {@link GpuBuffer}. Holds an opaque native
 * handle pointing at a {@code VkBuffer + VkDeviceMemory} pair on the C++
 * side (see {@code vk_resources.cpp}).
 *
 * <p>HOST_VISIBLE + COHERENT for now — simplest path. DEVICE_LOCAL with
 * staging copies comes in 1.6.1c-ish when we measure bandwidth.
 *
 * <p>Closeable: the GpuBuffer base class declares {@code AutoCloseable},
 * so MC's resource management invokes close() automatically.
 */
public final class VkGpuBuffer extends GpuBuffer {

    /** 0 means "already destroyed". */
    private long nativeHandle;

    public VkGpuBuffer(int usage, long size, long handle) {
        super(usage, size);
        this.nativeHandle = handle;
    }

    public long handle() { return nativeHandle; }

    @Override
    public boolean isClosed() { return nativeHandle == 0L; }

    @Override
    public void close() {
        if (nativeHandle == 0L) return;
        VkResNative.destroyBuffer(nativeHandle);
        nativeHandle = 0L;
    }

    /** Convenience for the encoder/render-pass path. Direct ByteBuffer
     *  alias over the mapped GPU memory. Caller must {@link #unmap()}. */
    public ByteBuffer map(long offset, long length) {
        if (nativeHandle == 0L) return null;
        return VkResNative.mapBuffer(nativeHandle, offset, length);
    }
    public void unmap() {
        if (nativeHandle == 0L) return;
        VkResNative.unmapBuffer(nativeHandle);
    }
}
