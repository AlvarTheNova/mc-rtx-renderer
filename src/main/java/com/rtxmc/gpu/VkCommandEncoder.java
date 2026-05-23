package com.rtxmc.gpu;

import com.mojang.blaze3d.buffers.GpuBuffer;
import com.mojang.blaze3d.buffers.GpuBufferSlice;
import com.mojang.blaze3d.buffers.GpuFence;
import com.mojang.blaze3d.systems.CommandEncoder;
import com.mojang.blaze3d.systems.GpuQuery;
import com.mojang.blaze3d.systems.RenderPass;
import com.mojang.blaze3d.textures.GpuTexture;
import com.mojang.blaze3d.textures.GpuTextureView;
import com.rtxmc.RtxMod;
import net.minecraft.client.texture.NativeImage;

import java.nio.ByteBuffer;
import java.util.OptionalInt;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Supplier;

/**
 * Phase 1.6.1e foundations — wraps Mojang's {@link CommandEncoder} and
 * delegates every call to it, logging first-N invocations per method so we
 * learn the priority order MC actually exercises for HUD / Screen rendering.
 *
 * <p>Same pattern that worked for {@link VkBackend} at 1.6.1a: scaffold +
 * trace before implementing. Steady-state has zero overhead (budget gated).
 *
 * <p>Per-method log counts are STATIC across all encoder instances — MC
 * creates a fresh encoder per frame so per-instance counters would never
 * exhaust their budget.
 */
public final class VkCommandEncoder implements CommandEncoder {

    private final CommandEncoder wrapped;

    private static final int LOG_BUDGET = 3;
    private static final ConcurrentHashMap<String, AtomicInteger> CALL_COUNTS = new ConcurrentHashMap<>();

    public VkCommandEncoder(CommandEncoder wrapped) {
        this.wrapped = wrapped;
    }

    private static void trace(String method) {
        int n = CALL_COUNTS.computeIfAbsent(method, k -> new AtomicInteger()).incrementAndGet();
        if (n <= LOG_BUDGET) {
            RtxMod.LOG.info("[vk-encoder] {} (call #{})", method, n);
        }
    }

    // ---- Render passes -----------------------------------------------------

    @Override
    public RenderPass createRenderPass(Supplier<String> labelGetter, GpuTextureView color,
                                       OptionalInt clearColor) {
        trace("createRenderPass(color)");
        RenderPass inner = wrapped.createRenderPass(labelGetter, color, clearColor);
        return new VkRenderPass(inner);
    }

    @Override
    public RenderPass createRenderPass(Supplier<String> labelGetter, GpuTextureView color,
                                       OptionalInt clearColor,
                                       GpuTextureView depth, java.util.OptionalDouble clearDepth) {
        trace("createRenderPass(color+depth)");
        RenderPass inner = wrapped.createRenderPass(labelGetter, color, clearColor, depth, clearDepth);
        return new VkRenderPass(inner);
    }

    // ---- Clears ------------------------------------------------------------

    @Override
    public void clearColorTexture(GpuTexture tex, int color) {
        trace("clearColorTexture");
        wrapped.clearColorTexture(tex, color);
    }

    @Override
    public void clearColorAndDepthTextures(GpuTexture color, int colorClear,
                                            GpuTexture depth, double depthClear) {
        trace("clearColorAndDepthTextures");
        wrapped.clearColorAndDepthTextures(color, colorClear, depth, depthClear);
    }

    @Override
    public void clearColorAndDepthTextures(GpuTexture color, int colorClear,
                                            GpuTexture depth, double depthClear,
                                            int x, int y, int w, int h) {
        trace("clearColorAndDepthTextures(rect)");
        wrapped.clearColorAndDepthTextures(color, colorClear, depth, depthClear, x, y, w, h);
    }

    @Override
    public void clearDepthTexture(GpuTexture depth, double clear) {
        trace("clearDepthTexture");
        wrapped.clearDepthTexture(depth, clear);
    }

    // ---- Buffer ops --------------------------------------------------------

    @Override
    public void writeToBuffer(GpuBufferSlice dst, ByteBuffer src) {
        trace("writeToBuffer");
        wrapped.writeToBuffer(dst, src);
    }

    @Override
    public GpuBuffer.MappedView mapBuffer(GpuBuffer buf, boolean read, boolean write) {
        trace("mapBuffer(GpuBuffer)");
        return wrapped.mapBuffer(buf, read, write);
    }

    @Override
    public GpuBuffer.MappedView mapBuffer(GpuBufferSlice slice, boolean read, boolean write) {
        trace("mapBuffer(slice)");
        return wrapped.mapBuffer(slice, read, write);
    }

    @Override
    public void copyToBuffer(GpuBufferSlice src, GpuBufferSlice dst) {
        trace("copyToBuffer");
        wrapped.copyToBuffer(src, dst);
    }

    // ---- Texture ops -------------------------------------------------------

    @Override
    public void writeToTexture(GpuTexture tex, NativeImage img) {
        trace("writeToTexture(NativeImage)");
        wrapped.writeToTexture(tex, img);
    }

    @Override
    public void writeToTexture(GpuTexture tex, NativeImage img,
                                int mipLevel, int layer, int sx, int sy, int w, int h, int dx, int dy) {
        trace("writeToTexture(NativeImage,rect)");
        wrapped.writeToTexture(tex, img, mipLevel, layer, sx, sy, w, h, dx, dy);
    }

    @Override
    public void writeToTexture(GpuTexture tex, ByteBuffer pixels, NativeImage.Format fmt,
                                int mipLevel, int layer, int x, int y, int w, int h) {
        trace("writeToTexture(bytes)");
        wrapped.writeToTexture(tex, pixels, fmt, mipLevel, layer, x, y, w, h);
    }

    @Override
    public void copyTextureToBuffer(GpuTexture src, GpuBuffer dst, long off, Runnable done, int mipLevel) {
        trace("copyTextureToBuffer");
        wrapped.copyTextureToBuffer(src, dst, off, done, mipLevel);
    }

    @Override
    public void copyTextureToBuffer(GpuTexture src, GpuBuffer dst, long off, Runnable done,
                                     int mipLevel, int x, int y, int w, int h) {
        trace("copyTextureToBuffer(rect)");
        wrapped.copyTextureToBuffer(src, dst, off, done, mipLevel, x, y, w, h);
    }

    @Override
    public void copyTextureToTexture(GpuTexture src, GpuTexture dst,
                                      int mipLevel, int sx, int sy, int dx, int dy, int w, int h) {
        trace("copyTextureToTexture");
        wrapped.copyTextureToTexture(src, dst, mipLevel, sx, sy, dx, dy, w, h);
    }

    // ---- Frame submit + queries --------------------------------------------

    @Override
    public void presentTexture(GpuTextureView swap) {
        trace("presentTexture");
        wrapped.presentTexture(swap);
    }

    @Override
    public GpuFence createFence() {
        trace("createFence");
        return wrapped.createFence();
    }

    @Override
    public GpuQuery timerQueryBegin() {
        trace("timerQueryBegin");
        return wrapped.timerQueryBegin();
    }

    @Override
    public void timerQueryEnd(GpuQuery q) {
        trace("timerQueryEnd");
        wrapped.timerQueryEnd(q);
    }
}
