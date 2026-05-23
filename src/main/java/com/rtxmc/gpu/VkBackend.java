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
    /** First N calls per method also shadow-create a real VK resource to
     *  prove our native code path works. After this budget runs out we
     *  stop creating shadows (zero steady-state overhead). */
    private static final int SHADOW_BUDGET = 3;
    private final ConcurrentHashMap<String, AtomicInteger> callCounts = new ConcurrentHashMap<>();

    public VkBackend(GpuDevice wrapped) {
        this.wrapped = wrapped;
        RtxMod.LOG.info("rtxmc VkBackend installed (wrapping {})", wrapped.getClass().getName());
        rtxmc$shadercSmokeTest();
    }

    /** Phase 1.6.1d one-shot: compile a trivial vert+frag pair via shaderc
     *  so we know the toolchain links + runs before anyone depends on it. */
    private static void rtxmc$shadercSmokeTest() {
        final String vert = "#version 450\nvoid main(){ gl_Position = vec4(0); }\n";
        final String frag = "#version 450\nlayout(location=0) out vec4 c;\nvoid main(){ c = vec4(1); }\n";
        try {
            int vWords = VkResNative.testCompileShader(vert, 0, "smoke-test-vert");
            int fWords = VkResNative.testCompileShader(frag, 1, "smoke-test-frag");
            if (vWords > 0 && fWords > 0) {
                RtxMod.LOG.info("rtxmc shaderc smoke-test ok: vert={} words, frag={} words", vWords, fWords);
            } else {
                RtxMod.LOG.error("rtxmc shaderc smoke-test FAILED: vWords={} fWords={}", vWords, fWords);
            }
        } catch (Throwable t) {
            RtxMod.LOG.error("rtxmc shaderc smoke-test threw", t);
        }
    }

    private int trace(String method) {
        int n = callCounts.computeIfAbsent(method, k -> new AtomicInteger()).incrementAndGet();
        if (n <= LOG_BUDGET) {
            RtxMod.LOG.info("[vk-backend] {} (call #{})", method, n);
        }
        return n;
    }

    private boolean shouldShadow(String method) {
        AtomicInteger c = callCounts.get(method);
        return c != null && c.get() <= SHADOW_BUDGET;
    }

    /** Best-effort shadow runner. Catches everything because we don't want
     *  a bug in our impl to take down vanilla rendering. */
    private void shadowSafely(String label, Runnable r) {
        try { r.run(); }
        catch (Throwable t) {
            RtxMod.LOG.error("[vk-backend] shadow {} threw", label, t);
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
        if (shouldShadow("createSampler")) shadowSafely("createSampler", () -> {
            long h = VkResNative.createSampler(addrU.ordinal(), addrV.ordinal(),
                                               minFilter.ordinal(), magFilter.ordinal(),
                                               Math.max(1, maxAniso));
            RtxMod.LOG.info("[vk-backend] shadow createSampler h=0x{} addr=({},{}) filter=({},{}) aniso={}",
                    Long.toHexString(h), addrU, addrV, minFilter, magFilter, maxAniso);
            if (h != 0L) VkResNative.destroySampler(h);
        });
        return wrapped.createSampler(addrU, addrV, minFilter, magFilter, maxAniso, maxLOD);
    }

    @Override public GpuTexture createTexture(Supplier<String> labelGetter, int usage,
                                              TextureFormat format, int w, int h,
                                              int depthOrLayers, int mipLevels) {
        trace("createTexture(Supplier)");
        rtxmc$shadowCreateTexture("createTexture(Supplier)", usage, format, w, h, depthOrLayers, mipLevels);
        return wrapped.createTexture(labelGetter, usage, format, w, h, depthOrLayers, mipLevels);
    }
    @Override public GpuTexture createTexture(String label, int usage,
                                              TextureFormat format, int w, int h,
                                              int depthOrLayers, int mipLevels) {
        trace("createTexture(String)");
        rtxmc$shadowCreateTexture("createTexture(String)", usage, format, w, h, depthOrLayers, mipLevels);
        return wrapped.createTexture(label, usage, format, w, h, depthOrLayers, mipLevels);
    }

    private void rtxmc$shadowCreateTexture(String method, int usage, TextureFormat fmt,
                                            int w, int h, int depth, int mips) {
        if (!shouldShadow(method)) return;
        shadowSafely(method, () -> {
            long handle = VkResNative.createTexture(usage, fmt.ordinal(), w, h, depth, mips);
            RtxMod.LOG.info("[vk-backend] shadow {} h=0x{} {}x{} fmt={} usage=0x{} mips={}",
                    method, Long.toHexString(handle), w, h, fmt,
                    Integer.toHexString(usage), mips);
            if (handle != 0L) VkResNative.destroyTexture(handle);
        });
    }

    @Override public GpuTextureView createTextureView(GpuTexture tex) {
        trace("createTextureView");
        return wrapped.createTextureView(tex);
    }
    @Override public GpuTextureView createTextureView(GpuTexture tex, int baseMip, int mipLevels) {
        trace("createTextureView(mip)");
        return wrapped.createTextureView(tex, baseMip, mipLevels);
        // Shadow disabled here: requires a VkGpuTexture handle which only
        // exists once createTexture itself is real (1.6.1c). Until then GL
        // textures don't carry our native handle.
    }

    @Override public GpuBuffer createBuffer(Supplier<String> labelGetter, int usage, long size) {
        trace("createBuffer(size)");
        if (shouldShadow("createBuffer(size)")) shadowSafely("createBuffer(size)", () -> {
            long h = VkResNative.createBuffer(usage, size, null);
            RtxMod.LOG.info("[vk-backend] shadow createBuffer(size) h=0x{} usage=0x{} size={}",
                    Long.toHexString(h), Integer.toHexString(usage), size);
            if (h != 0L) VkResNative.destroyBuffer(h);
        });
        return wrapped.createBuffer(labelGetter, usage, size);
    }
    @Override public GpuBuffer createBuffer(Supplier<String> labelGetter, int usage, ByteBuffer data) {
        trace("createBuffer(data)");
        if (shouldShadow("createBuffer(data)")) shadowSafely("createBuffer(data)", () -> {
            // MC may hand us non-direct ByteBuffers; copy into a direct one
            // for the JNI GetDirectBufferAddress path.
            ByteBuffer direct = data;
            if (!data.isDirect()) {
                direct = ByteBuffer.allocateDirect(data.remaining());
                int p = data.position();
                direct.put(data.duplicate());
                direct.flip();
                data.position(p); // restore caller's position
            }
            long h = VkResNative.createBuffer(usage, direct.remaining(), direct);
            RtxMod.LOG.info("[vk-backend] shadow createBuffer(data) h=0x{} usage=0x{} bytes={}",
                    Long.toHexString(h), Integer.toHexString(usage), direct.remaining());
            if (h != 0L) VkResNative.destroyBuffer(h);
        });
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
