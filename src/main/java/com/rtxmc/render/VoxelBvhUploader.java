package com.rtxmc.render;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayDeque;
import java.util.Deque;

/**
 * Bridges MC's chunk mesh data to the native BVH builder.
 *
 * Phase 1: hook ChunkBuilder's output and translate vanilla vertex format
 * to our packed format (see DESIGN.md §3.1). For now this is a queue + flush;
 * the actual mixin into ChunkBuilder lives in a TODO.
 */
public final class VoxelBvhUploader {

    private static final int BYTES_PER_VERTEX = 16; // pos16x3 + n8x2 + uv16x2 + matid16

    private final Deque<PendingChunk> dirtyChunks = new ArrayDeque<>();

    public void markDirty(PendingChunk chunk) {
        dirtyChunks.addLast(chunk);
    }

    public void flushDirtyChunks() {
        PendingChunk c;
        while ((c = dirtyChunks.pollFirst()) != null) {
            NativeBridge.uploadChunk(c.cx, c.cy, c.cz, c.vertices, c.indices, c.materialIds);
        }
    }

    public static ByteBuffer allocVertexBuffer(int vertexCount) {
        return ByteBuffer.allocateDirect(vertexCount * BYTES_PER_VERTEX)
                .order(ByteOrder.nativeOrder());
    }

    public record PendingChunk(
            int cx, int cy, int cz,
            ByteBuffer vertices,
            ByteBuffer indices,
            ByteBuffer materialIds
    ) {}
}
