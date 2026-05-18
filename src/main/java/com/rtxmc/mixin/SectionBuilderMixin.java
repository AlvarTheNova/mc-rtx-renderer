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
 * For now we only forward the SOLID layer (the bulk of normal terrain) and
 * we COPY the bytes out — the BuiltBuffer's underlying allocator-backed
 * ByteBuffer is freed when vanilla calls {@code RenderData.close()}, which
 * happens shortly after the upload command lands on the render thread.
 *
 * Thread note: this runs on chunk worker threads. The native side
 * ({@code BvhStore::upload_chunk}) must be thread-safe.
 */
@Mixin(SectionBuilder.class)
public abstract class SectionBuilderMixin {

    private static final AtomicInteger rtxmc$logBudget = new AtomicInteger(8);

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
        BuiltBuffer solid = data.buffers.get(BlockRenderLayer.SOLID);
        if (solid == null) return; // empty/air section

        BuiltBuffer.DrawParameters dp = solid.getDrawParameters();
        ByteBuffer src = solid.getBuffer();
        if (src == null || dp.vertexCount() == 0) return;

        // Copy into a fresh direct buffer we own. src is allocator-backed and
        // becomes invalid after RenderData.close() returns to the render
        // thread.
        int size = src.remaining();
        ByteBuffer copy = ByteBuffer.allocateDirect(size).order(ByteOrder.nativeOrder());
        copy.put(src.duplicate()); // duplicate so we don't disturb src position
        copy.flip();

        // Optional indices — for non-indexed quad draws indexCount is 0.
        ByteBuffer emptyIdx = ByteBuffer.allocateDirect(0).order(ByteOrder.nativeOrder());
        ByteBuffer emptyMat = ByteBuffer.allocateDirect(0).order(ByteOrder.nativeOrder());

        NativeBridge.uploadChunk(pos.getX(), pos.getY(), pos.getZ(),
                copy, emptyIdx, emptyMat);

        if (rtxmc$logBudget.getAndDecrement() > 0) {
            RtxMod.LOG.info("rtxmc chunk section ({}, {}, {}): SOLID layer {} verts, {} bytes, fmt={}, mode={}, idxCount={}",
                    pos.getX(), pos.getY(), pos.getZ(),
                    dp.vertexCount(), size,
                    dp.format(), dp.mode(), dp.indexCount());

            // Hex dump first 4 vertices (one quad's worth, 128 bytes) so we
            // can decode what's actually in the vertex stream.
            int dumpBytes = Math.min(128, size);
            StringBuilder sb = new StringBuilder("rtxmc chunk first quad bytes: ");
            for (int i = 0; i < dumpBytes; ++i) {
                if (i > 0 && i % 32 == 0) sb.append("\n  vert").append(i / 32).append(": ");
                else if (i == 0) sb.append("\n  vert0: ");
                else if (i % 4 == 0) sb.append(" ");
                sb.append(String.format("%02x", copy.get(i) & 0xFF));
            }
            RtxMod.LOG.info(sb.toString());

            // Also decode each vertex's expected fields under my assumed layout
            for (int v = 0; v < 4; ++v) {
                int b = v * 32;
                float px = copy.getFloat(b + 0);
                float py = copy.getFloat(b + 4);
                float pz = copy.getFloat(b + 8);
                int colorPack = copy.getInt(b + 12);
                float u0 = copy.getFloat(b + 16);
                float v0 = copy.getFloat(b + 20);
                short uv2x = copy.getShort(b + 24);
                short uv2y = copy.getShort(b + 26);
                byte nx = copy.get(b + 28);
                byte ny = copy.get(b + 29);
                byte nz = copy.get(b + 30);
                byte pad = copy.get(b + 31);
                RtxMod.LOG.info("  vert{}: pos=({},{},{}) color=0x{} uv0=({},{}) uv2=({},{}) n=({},{},{}) pad=0x{}",
                        v, px, py, pz, Integer.toHexString(colorPack), u0, v0, uv2x, uv2y, nx, ny, nz, Integer.toHexString(pad & 0xFF));
            }
        }
    }
}
