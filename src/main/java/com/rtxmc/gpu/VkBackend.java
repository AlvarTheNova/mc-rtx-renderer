package com.rtxmc.gpu;

import com.mojang.blaze3d.buffers.GpuBuffer;
import com.mojang.blaze3d.pipeline.CompiledRenderPipeline;
import com.mojang.blaze3d.pipeline.RenderPipeline;
import com.mojang.blaze3d.systems.CommandEncoder;
import com.mojang.blaze3d.systems.GpuDevice;
import com.mojang.blaze3d.textures.AddressMode;
import com.mojang.blaze3d.textures.FilterMode;
import com.mojang.blaze3d.textures.GpuTexture;
import com.mojang.blaze3d.textures.GpuTextureView;
import com.mojang.blaze3d.textures.TextureFormat;
import com.rtxmc.RtxMod;
import net.minecraft.client.gl.GpuSampler;
import net.minecraft.client.gl.ShaderSourceGetter;

import java.nio.ByteBuffer;
import java.util.List;
import java.util.OptionalDouble;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Supplier;

/**
 * Phase 1.6.1a — VkBackend scaffold. Wraps Mojang's GlBackend and delegates
 * every call, logging the first few invocations per method so we can learn
 * the order MC calls things in and prioritise actual VK implementation.
 *
 * <p>Why wrapper-mode for 1.6.1a: replacing GlBackend wholesale would crash
 * MC immediately the moment something fundamental was called (e.g. the GUI
 * renderer's first {@code createCommandEncoder}). Wrapping keeps MC alive
 * while we observe — vanilla GL keeps drawing into our suppressed dummy
 * framebuffer (which goes nowhere on-screen) so we get clean diagnostics
 * without breakage.
 *
 * <p>1.6.1b+ will progressively swap individual delegations for real VK
 * implementations.
 */
public final class VkBackend implements GpuDevice {

    private final GpuDevice wrapped;

    /** First N calls per method get logged; further calls are silent. */
    private static final int LOG_BUDGET = 3;
    private final ConcurrentHashMap<String, AtomicInteger> callCounts = new ConcurrentHashMap<>();

    public VkBackend(GpuDevice wrapped) {
        this.wrapped = wrapped;
        RtxMod.LOG.info("rtxmc VkBackend installed (wrapping {})", wrapped.getClass().getName());
    }

    private void trace(String method) {
        int n = callCounts.computeIfAbsent(method, k -> new AtomicInteger()).incrementAndGet();
        if (n <= LOG_BUDGET) {
            RtxMod.LOG.info("[vk-backend] {} (call #{})", method, n);
        }
    }

    // ---- Pure-query methods: cheap to forward, useful for F3 / debug ----

    @Override public String getImplementationInformation() {
        trace("getImplementationInformation");
        return "rtxmc-vk-wrapper(" + wrapped.getImplementationInformation() + ")";
    }
    @Override public List<String> getLastDebugMessages() { trace("getLastDebugMessages"); return wrapped.getLastDebugMessages(); }
    @Override public boolean isDebuggingEnabled()        { trace("isDebuggingEnabled");   return wrapped.isDebuggingEnabled(); }
    @Override public String getVendor()                  { trace("getVendor");            return wrapped.getVendor(); }
    @Override public String getBackendName()             { trace("getBackendName");       return "rtxmc-vk(" + wrapped.getBackendName() + ")"; }
    @Override public String getVersion()                 { trace("getVersion");           return wrapped.getVersion(); }
    @Override public String getRenderer()                { trace("getRenderer");          return wrapped.getRenderer(); }
    @Override public int getMaxTextureSize()             { trace("getMaxTextureSize");    return wrapped.getMaxTextureSize(); }
    @Override public int getUniformOffsetAlignment()     { trace("getUniformOffsetAlignment"); return wrapped.getUniformOffsetAlignment(); }
    @Override public List<String> getEnabledExtensions() { trace("getEnabledExtensions"); return wrapped.getEnabledExtensions(); }
    @Override public int getMaxSupportedAnisotropy()     { trace("getMaxSupportedAnisotropy"); return wrapped.getMaxSupportedAnisotropy(); }

    // ---- Resource creation: forward to GL for now ----

    @Override public CommandEncoder createCommandEncoder() {
        trace("createCommandEncoder");
        return wrapped.createCommandEncoder();
    }

    @Override public GpuSampler createSampler(AddressMode addrU, AddressMode addrV,
                                              FilterMode minFilter, FilterMode magFilter,
                                              int maxAniso, OptionalDouble maxLOD) {
        trace("createSampler");
        return wrapped.createSampler(addrU, addrV, minFilter, magFilter, maxAniso, maxLOD);
    }

    @Override public GpuTexture createTexture(Supplier<String> labelGetter, int usage,
                                              TextureFormat format, int w, int h,
                                              int depthOrLayers, int mipLevels) {
        trace("createTexture(Supplier)");
        return wrapped.createTexture(labelGetter, usage, format, w, h, depthOrLayers, mipLevels);
    }
    @Override public GpuTexture createTexture(String label, int usage,
                                              TextureFormat format, int w, int h,
                                              int depthOrLayers, int mipLevels) {
        trace("createTexture(String)");
        return wrapped.createTexture(label, usage, format, w, h, depthOrLayers, mipLevels);
    }

    @Override public GpuTextureView createTextureView(GpuTexture tex) {
        trace("createTextureView");
        return wrapped.createTextureView(tex);
    }
    @Override public GpuTextureView createTextureView(GpuTexture tex, int baseMip, int mipLevels) {
        trace("createTextureView(mip)");
        return wrapped.createTextureView(tex, baseMip, mipLevels);
    }

    @Override public GpuBuffer createBuffer(Supplier<String> labelGetter, int usage, long size) {
        trace("createBuffer(size)");
        return wrapped.createBuffer(labelGetter, usage, size);
    }
    @Override public GpuBuffer createBuffer(Supplier<String> labelGetter, int usage, ByteBuffer data) {
        trace("createBuffer(data)");
        return wrapped.createBuffer(labelGetter, usage, data);
    }

    @Override public CompiledRenderPipeline precompilePipeline(RenderPipeline pipeline, ShaderSourceGetter sourceGetter) {
        trace("precompilePipeline(2-arg)");
        return wrapped.precompilePipeline(pipeline, sourceGetter);
    }

    @Override public void clearPipelineCache() {
        trace("clearPipelineCache");
        wrapped.clearPipelineCache();
    }

    @Override public void close() {
        trace("close");
        wrapped.close();
    }
}
