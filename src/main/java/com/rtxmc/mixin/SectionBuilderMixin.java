package com.rtxmc.mixin;

import com.rtxmc.RtxMod;
import com.rtxmc.render.NativeBridge;
import net.minecraft.client.render.BlockRenderLayer;
import net.minecraft.client.render.BuiltBuffer;
import net.minecraft.client.render.chunk.BlockBufferAllocatorStorage;
import net.minecraft.client.render.chunk.ChunkRendererRegion;
import net.minecraft.client.render.chunk.SectionBuilder;
import net.minecraft.util.math.ChunkSectionPos;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Phase 1.4.1 — intercept finished chunk-section meshes as they leave the
 * vanilla mesher. Vanilla builds each 16×16×16 section on a worker thread
 * and returns a {@link SectionBuilder.RenderData} containing per-render-layer
 * {@link BuiltBuffer}s of vertex data.
 *
 * Phase 1.4.x — now forwards all four block render layers (SOLID, CUTOUT,
 * TRANSLUCENT, TRIPWIRE), each tagged with a numeric layer ID so the native
 * side can route to the right pipeline (opaque vs alpha-blend).
 *
 * Thread note: this runs on chunk worker threads. The native side
 * ({@code BvhStore::upload_chunk}) must be thread-safe.
 */
@Mixin(SectionBuilder.class)
public abstract class SectionBuilderMixin {

    private static final AtomicInteger rtxmc$logBudget = new AtomicInteger(8);

    // Layer IDs shared with native (see chunk_renderer.h LayerId).
    private static final int LAYER_SOLID       = 0;
    private static final int LAYER_CUTOUT      = 1;
    private static final int LAYER_TRANSLUCENT = 2;
    private static final int LAYER_TRIPWIRE    = 3;

    private static int layerIdOf(BlockRenderLayer l) {
        if (l == BlockRenderLayer.SOLID)       return LAYER_SOLID;
        if (l == BlockRenderLayer.CUTOUT)      return LAYER_CUTOUT;
        if (l == BlockRenderLayer.TRANSLUCENT) return LAYER_TRANSLUCENT;
        if (l == BlockRenderLayer.TRIPWIRE)    return LAYER_TRIPWIRE;
        return -1;
    }

    @Inject(
            method = "build(Lnet/minecraft/util/math/ChunkSectionPos;Lnet/minecraft/client/render/chunk/ChunkRendererRegion;Lcom/mojang/blaze3d/systems/VertexSorter;Lnet/minecraft/client/render/chunk/BlockBufferAllocatorStorage;)Lnet/minecraft/client/render/chunk/SectionBuilder$RenderData;",
            at = @At("RETURN")
    )
    private void rtxmc$captureSectionMesh(
            ChunkSectionPos pos,
            ChunkRendererRegion region,
            com.mojang.blaze3d.systems.VertexSorter sorter,
            BlockBufferAllocatorStorage storage,
            CallbackInfoReturnable<SectionBuilder.RenderData> cir
    ) {
        SectionBuilder.RenderData data = cir.getReturnValue();
        if (data == null) return;

        boolean loggedThisSection = false;
        for (var entry : data.buffers.entrySet()) {
            final int layerId = layerIdOf(entry.getKey());
            if (layerId < 0) continue;
            final BuiltBuffer buf = entry.getValue();
            if (buf == null) continue;
            final BuiltBuffer.DrawParameters dp = buf.getDrawParameters();
            if (dp.vertexCount() == 0) continue;
            final ByteBuffer src = buf.getBuffer();
            if (src == null) continue;

            // Copy into a fresh direct buffer we own. src is allocator-backed
            // and becomes invalid once RenderData.close() runs.
            final int size = src.remaining();
            ByteBuffer copy = ByteBuffer.allocateDirect(size).order(ByteOrder.nativeOrder());
            copy.put(src.duplicate());
            copy.flip();

            NativeBridge.uploadChunk(pos.getX(), pos.getY(), pos.getZ(), layerId, copy);

            if (!loggedThisSection && rtxmc$logBudget.getAndDecrement() > 0) {
                RtxMod.LOG.info("rtxmc section ({},{},{}) layer={} verts={} bytes={} mode={} idxCount={}",
                        pos.getX(), pos.getY(), pos.getZ(),
                        entry.getKey().getName(),
                        dp.vertexCount(), size, dp.mode(), dp.indexCount());
                loggedThisSection = true;
            }
        }
    }
}
