package com.rtxmc.gpu;

import com.mojang.blaze3d.pipeline.CompiledRenderPipeline;

/**
 * Phase 1.6.1d step 4 — Vulkan-backed {@link CompiledRenderPipeline}. Holds
 * the opaque native handle returned by {@code vkpipe_create}. Not yet usable
 * for actual draws (no descriptor sets / uniform bindings until 1.6.1e), so
 * {@link #isValid()} reports whether the underlying VkPipeline was built but
 * we do not yet return this to MC from {@code VkBackend.precompilePipeline}.
 */
public final class VkCompiledRenderPipeline implements CompiledRenderPipeline {

    private long nativeHandle;
    private final String label;

    public VkCompiledRenderPipeline(long handle, String label) {
        this.nativeHandle = handle;
        this.label = label;
    }

    public long handle() { return nativeHandle; }
    public String label() { return label; }

    @Override
    public boolean isValid() { return nativeHandle != 0L; }

    /** Free the native VkPipeline + VkPipelineLayout. Idempotent. */
    public void destroy() {
        if (nativeHandle == 0L) return;
        VkResNative.destroyPipeline(nativeHandle);
        nativeHandle = 0L;
    }
}
