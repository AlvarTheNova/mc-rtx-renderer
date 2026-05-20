package com.rtxmc.gpu;

import com.mojang.blaze3d.textures.AddressMode;
import com.mojang.blaze3d.textures.FilterMode;
import net.minecraft.client.gl.GpuSampler;

import java.util.OptionalDouble;

/**
 * Phase 1.6.1b — Vulkan-backed {@link GpuSampler}. Wraps a VkSampler.
 *
 * <p>Note: GpuSampler is in MC's package, not Mojang's blaze3d package
 * — quirk of the 1.21.11 abstraction split.
 */
public final class VkGpuSampler extends GpuSampler {

    private final AddressMode addrU, addrV;
    private final FilterMode minFilter, magFilter;
    private final int maxAniso;
    private final OptionalDouble maxLod;
    private long nativeHandle;

    public VkGpuSampler(AddressMode addrU, AddressMode addrV,
                        FilterMode minFilter, FilterMode magFilter,
                        int maxAniso, OptionalDouble maxLod, long handle) {
        this.addrU = addrU;
        this.addrV = addrV;
        this.minFilter = minFilter;
        this.magFilter = magFilter;
        this.maxAniso = maxAniso;
        this.maxLod = maxLod;
        this.nativeHandle = handle;
    }

    public long handle() { return nativeHandle; }

    @Override public AddressMode getAddressModeU()      { return addrU; }
    @Override public AddressMode getAddressModeV()      { return addrV; }
    @Override public FilterMode  getMinFilterMode()     { return minFilter; }
    @Override public FilterMode  getMagFilterMode()     { return magFilter; }
    @Override public int         getMaxAnisotropy()     { return maxAniso; }
    @Override public OptionalDouble getMaxLevelOfDetail() { return maxLod; }

    @Override
    public void close() {
        if (nativeHandle == 0L) return;
        VkResNative.destroySampler(nativeHandle);
        nativeHandle = 0L;
    }
}
