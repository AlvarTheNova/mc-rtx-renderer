package com.rtxmc.gpu;

import com.mojang.blaze3d.pipeline.BlendFunction;
import com.mojang.blaze3d.pipeline.RenderPipeline;
import com.mojang.blaze3d.platform.DepthTestFunction;
import com.mojang.blaze3d.platform.DestFactor;
import com.mojang.blaze3d.platform.PolygonMode;
import com.mojang.blaze3d.platform.SourceFactor;
import com.mojang.blaze3d.vertex.VertexFormat;
import com.mojang.blaze3d.vertex.VertexFormat.DrawMode;
import com.mojang.blaze3d.vertex.VertexFormatElement;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.List;
import java.util.Optional;

/**
 * Phase 1.6.1d step 3 — packs Mojang's {@link RenderPipeline} state into a
 * direct ByteBuffer matching the layout that
 * {@code Java_com_rtxmc_gpu_VkResNative_createPipeline} reads (see
 * {@code native/src/jni_bridge.cpp}).
 *
 * <h3>Wire format</h3>
 * <pre>
 *   u32 topology          // DrawMode ordinal
 *   u32 polygon_mode      // PolygonMode ordinal
 *   u32 cull              // bool 0/1
 *   u32 depth_test_func   // DepthTestFunction ordinal
 *   u32 flags             // bit0=writeDepth, bit1=writeColor, bit2=writeAlpha
 *   u32 blend_enabled
 *   u32 blend_src_color   // SourceFactor ordinal
 *   u32 blend_dst_color   // DestFactor ordinal
 *   u32 blend_src_alpha
 *   u32 blend_dst_alpha
 *   u32 vertex_stride
 *   u32 color_format_code
 *   u32 depth_attachment
 *   u32 attr_count
 *   { u32 location, u32 type_ord, u32 count, u32 offset } × attr_count
 * </pre>
 *
 * Allocates a fresh direct buffer per call — fine for {@code precompilePipeline}
 * which runs once per (RenderPipeline, hardware) pair at world load.
 */
public final class VkPipelineSpecPacker {

    private VkPipelineSpecPacker() {}

    public static ByteBuffer pack(RenderPipeline pipeline) {
        VertexFormat vf = pipeline.getVertexFormat();
        List<VertexFormatElement> elements = vf.getElements();
        int attrCount = elements.size();
        int sizeBytes = (14 + 4 * attrCount) * Integer.BYTES;

        ByteBuffer buf = ByteBuffer.allocateDirect(sizeBytes).order(ByteOrder.LITTLE_ENDIAN);

        DrawMode drawMode = pipeline.getVertexFormatMode();
        PolygonMode polygonMode = pipeline.getPolygonMode();
        DepthTestFunction depthFn = pipeline.getDepthTestFunction();
        Optional<BlendFunction> blendOpt = pipeline.getBlendFunction();

        // Header
        buf.putInt(drawMode.ordinal());
        buf.putInt(polygonMode.ordinal());
        buf.putInt(pipeline.isCull() ? 1 : 0);
        buf.putInt(depthFn.ordinal());

        int flags = 0;
        if (pipeline.isWriteDepth()) flags |= 1;
        if (pipeline.isWriteColor()) flags |= 2;
        if (pipeline.isWriteAlpha()) flags |= 4;
        buf.putInt(flags);

        if (blendOpt.isPresent()) {
            BlendFunction bf = blendOpt.get();
            buf.putInt(1);
            buf.putInt(bf.sourceColor().ordinal());
            buf.putInt(bf.destColor().ordinal());
            buf.putInt(bf.sourceAlpha().ordinal());
            buf.putInt(bf.destAlpha().ordinal());
        } else {
            // Disabled blend; factors don't matter but keep zeroed.
            buf.putInt(0);
            buf.putInt(0); buf.putInt(0); buf.putInt(0); buf.putInt(0);
        }

        buf.putInt(vf.getVertexSize());

        // Default attachments: swapchain-style RGBA8 color, depth if requested.
        buf.putInt(0); // color_format_code: 0 = RGBA8
        buf.putInt(pipeline.wantsDepthTexture() ? 1 : 0);

        buf.putInt(attrCount);

        // Per-element attrs. Locations are element index per Mojang's
        // convention; offsets come from VertexFormat.getOffset(element).
        int[] offsets = vf.getOffsetsByElement();
        for (int i = 0; i < attrCount; i++) {
            VertexFormatElement e = elements.get(i);
            buf.putInt(i);                       // location
            buf.putInt(e.type().ordinal());      // type code (FLOAT=0, UBYTE=1, ...)
            buf.putInt(e.count());               // 1..4
            buf.putInt(offsets[i]);              // byte offset
        }

        buf.flip();
        return buf;
    }
}
