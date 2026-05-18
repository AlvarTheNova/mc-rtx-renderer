package com.rtxmc.mixin;

import com.rtxmc.RtxMod;
import com.rtxmc.render.EntityBatchContext;
import net.minecraft.client.render.RenderLayer;
import net.minecraft.client.render.VertexConsumerProvider;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Phase 1.5.2a — set / clear a thread-local "current render layer" while
 * vanilla is inside {@link VertexConsumerProvider.Immediate#draw(RenderLayer)}.
 * {@link BufferBuilderEndMixin} reads it to differentiate entity/particle/UI
 * batches from the chunk mesher's BufferBuilder.end() calls (chunks don't go
 * through Immediate.draw).
 *
 * Also keeps a one-shot log of unique layer names seen (Phase 1.5.1 carry-over).
 */
@Mixin(VertexConsumerProvider.Immediate.class)
public abstract class VertexConsumerProviderMixin {

    private static final ConcurrentHashMap<String, Boolean> rtxmc$layersSeen = new ConcurrentHashMap<>();
    private static final AtomicInteger rtxmc$logBudget = new AtomicInteger(8);

    @Inject(method = "draw(Lnet/minecraft/client/render/RenderLayer;)V", at = @At("HEAD"))
    private void rtxmc$enterDraw(RenderLayer layer, CallbackInfo ci) {
        EntityBatchContext.set(layer);
        if (layer != null) {
            String name = layer.toString();
            if (rtxmc$layersSeen.putIfAbsent(name, Boolean.TRUE) == null
                    && rtxmc$logBudget.getAndDecrement() > 0) {
                RtxMod.LOG.info("rtxmc batched-draw layer (1.5.2): {} (fmt={})",
                        name, layer.getVertexFormat());
            }
        }
    }

    @Inject(method = "draw(Lnet/minecraft/client/render/RenderLayer;)V", at = @At("RETURN"))
    private void rtxmc$exitDraw(RenderLayer layer, CallbackInfo ci) {
        EntityBatchContext.clear();
    }
}
