package com.rtxmc.gpu;

import com.mojang.blaze3d.buffers.GpuBuffer;
import com.mojang.blaze3d.buffers.GpuBufferSlice;
import com.mojang.blaze3d.pipeline.RenderPipeline;
import com.mojang.blaze3d.systems.RenderPass;
import com.mojang.blaze3d.textures.GpuTextureView;
import com.mojang.blaze3d.vertex.VertexFormat;
import com.rtxmc.RtxMod;
import net.minecraft.client.gl.GpuSampler;

import java.util.Collection;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Supplier;

/**
 * Phase 1.6.1e foundations — wraps Mojang's {@link RenderPass} and delegates
 * every call to it, logging first-N invocations per method.
 *
 * <p>Static per-method counters across all pass instances (same reasoning as
 * {@link VkCommandEncoder}).
 */
public final class VkRenderPass implements RenderPass {

    private final RenderPass wrapped;

    private static final int LOG_BUDGET = 3;
    private static final ConcurrentHashMap<String, AtomicInteger> CALL_COUNTS = new ConcurrentHashMap<>();

    public VkRenderPass(RenderPass wrapped) {
        this.wrapped = wrapped;
    }

    private static void trace(String method) {
        int n = CALL_COUNTS.computeIfAbsent(method, k -> new AtomicInteger()).incrementAndGet();
        if (n <= LOG_BUDGET) {
            RtxMod.LOG.info("[vk-pass] {} (call #{})", method, n);
        }
    }

    @Override
    public void pushDebugGroup(Supplier<String> labelGetter) {
        trace("pushDebugGroup");
        wrapped.pushDebugGroup(labelGetter);
    }

    @Override
    public void popDebugGroup() {
        trace("popDebugGroup");
        wrapped.popDebugGroup();
    }

    @Override
    public void setPipeline(RenderPipeline pipeline) {
        trace("setPipeline");
        wrapped.setPipeline(pipeline);
    }

    @Override
    public void bindTexture(String name, GpuTextureView view, GpuSampler sampler) {
        trace("bindTexture");
        wrapped.bindTexture(name, view, sampler);
    }

    @Override
    public void setUniform(String name, GpuBuffer buffer) {
        trace("setUniform(GpuBuffer)");
        wrapped.setUniform(name, buffer);
    }

    @Override
    public void setUniform(String name, GpuBufferSlice slice) {
        trace("setUniform(slice)");
        wrapped.setUniform(name, slice);
    }

    @Override
    public void enableScissor(int x, int y, int w, int h) {
        trace("enableScissor");
        wrapped.enableScissor(x, y, w, h);
    }

    @Override
    public void disableScissor() {
        trace("disableScissor");
        wrapped.disableScissor();
    }

    @Override
    public void setVertexBuffer(int idx, GpuBuffer buf) {
        trace("setVertexBuffer");
        wrapped.setVertexBuffer(idx, buf);
    }

    @Override
    public void setIndexBuffer(GpuBuffer buf, VertexFormat.IndexType type) {
        trace("setIndexBuffer");
        wrapped.setIndexBuffer(buf, type);
    }

    @Override
    public void drawIndexed(int baseVertex, int firstIndex, int indexCount, int instanceCount) {
        trace("drawIndexed");
        wrapped.drawIndexed(baseVertex, firstIndex, indexCount, instanceCount);
    }

    @Override
    public <T> void drawMultipleIndexed(Collection<RenderObject<T>> objects, GpuBuffer indexBuffer,
                                         VertexFormat.IndexType type,
                                         Collection<String> sharedUniforms, T sharedDynamicUniform) {
        trace("drawMultipleIndexed");
        wrapped.drawMultipleIndexed(objects, indexBuffer, type, sharedUniforms, sharedDynamicUniform);
    }

    @Override
    public void draw(int offset, int count) {
        trace("draw");
        wrapped.draw(offset, count);
    }

    @Override
    public void close() {
        trace("close");
        wrapped.close();
    }
}
