package com.rtxmc.render;

import net.minecraft.client.render.RenderLayer;

/**
 * Thread-local context shared between {@code VertexConsumerProviderMixin}
 * (sets the current layer when entering {@code Immediate.draw}) and
 * {@code BufferBuilderEndMixin} (reads it when intercepting
 * {@code BufferBuilder.end()}). Lets us differentiate entity/particle/UI
 * batches from chunk-meshing {@code end()} calls — chunks don't go through
 * {@code Immediate.draw}.
 */
public final class EntityBatchContext {
    private static final ThreadLocal<RenderLayer> CURRENT = new ThreadLocal<>();

    private EntityBatchContext() {}

    public static void set(RenderLayer layer) { CURRENT.set(layer); }
    public static void clear()                { CURRENT.set(null); }
    public static RenderLayer get()           { return CURRENT.get(); }
    public static boolean inImmediateDraw()   { return CURRENT.get() != null; }
}
