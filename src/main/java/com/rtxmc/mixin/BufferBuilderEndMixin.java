package com.rtxmc.mixin;

import com.rtxmc.RtxMod;
import com.rtxmc.render.EntityBatchContext;
import com.rtxmc.render.NativeBridge;
import net.minecraft.client.render.BufferBuilder;
import net.minecraft.client.render.BuiltBuffer;
import net.minecraft.client.render.RenderLayer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Phase 1.5.2a — intercept BufferBuilder.end() and steal a COPY of the
 * resulting BuiltBuffer's bytes when we're inside
 * {@code Immediate.draw(RenderLayer)} (signaled via
 * {@link EntityBatchContext}). The chunk mesher calls end() too but goes
 * through SectionBuilder, never Immediate.draw — so the thread-local guard
 * keeps the two streams separate.
 *
 * Vanilla still gets the BuiltBuffer back unmodified; we just observe.
 * Phase 1.5.2b will actually upload these to native and render them.
 */
@Mixin(BufferBuilder.class)
public abstract class BufferBuilderEndMixin {

    private static final AtomicInteger rtxmc$logBudget = new AtomicInteger(8);

    // Target BOTH end() and endNullable() — vanilla's private
    // VertexConsumerProvider$Immediate.draw uses endNullable().
    @Inject(method = {
                "end()Lnet/minecraft/client/render/BuiltBuffer;",
                "endNullable()Lnet/minecraft/client/render/BuiltBuffer;"
            },
            at = @At("RETURN"))
    private void rtxmc$captureEntityBatch(CallbackInfoReturnable<BuiltBuffer> cir) {
        RenderLayer layer = EntityBatchContext.get();
        if (layer == null) return; // not an entity / batched-draw context

        BuiltBuffer built = cir.getReturnValue();
        if (built == null) return;

        BuiltBuffer.DrawParameters dp = built.getDrawParameters();
        if (dp.vertexCount() == 0) return;

        ByteBuffer src = built.getBuffer();
        if (src == null) return;
        int size = src.remaining();
        if (size <= 0) return;

        // Copy bytes before vanilla disposes the BuiltBuffer. Allocator-backed.
        ByteBuffer copy = ByteBuffer.allocateDirect(size).order(ByteOrder.nativeOrder());
        copy.put(src.duplicate());
        copy.flip();

        // Phase 1.5.2b: stash with a stable layer hash AND vertex count so the
        // native side can filter by stride (skip non-36-B formats like LINES
        // and GLINT until 1.5.2c lights them up).
        int layerHash = layer.toString().hashCode();
        NativeBridge.uploadEntityBatch(layerHash, dp.vertexCount(), copy);

        if (rtxmc$logBudget.getAndDecrement() > 0) {
            RtxMod.LOG.info("rtxmc entity batch: layer={} verts={} bytes={} fmt={}",
                    layer.toString().substring(0, Math.min(30, layer.toString().length())),
                    dp.vertexCount(), size, dp.format());
        }
    }
}
