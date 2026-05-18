package com.rtxmc.mixin;

import com.rtxmc.RtxMod;
import net.minecraft.client.render.RenderLayer;
import net.minecraft.client.render.VertexConsumerProvider;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;

/**
 * Phase 1.5.1 — proof-of-life hook on MC's batched-draw chokepoint.
 *
 * Every entity, particle, and "anything that uses {@code VertexConsumerProvider}"
 * eventually flushes geometry through {@link VertexConsumerProvider.Immediate#draw(RenderLayer)}.
 * This mixin observes that call without consuming the BufferBuilder — pulls a
 * count of unique layers and a per-period draw rate so we can see the volume
 * of work entities/particles represent. Phase 1.5.2 will steal the actual
 * vertex bytes.
 */
@Mixin(VertexConsumerProvider.Immediate.class)
public abstract class VertexConsumerProviderMixin {

    private static final ConcurrentHashMap<String, Boolean> rtxmc$layersSeen = new ConcurrentHashMap<>();
    private static final AtomicInteger rtxmc$logBudget = new AtomicInteger(20);
    private static final AtomicLong    rtxmc$drawsThisPeriod = new AtomicLong(0);
    private static long rtxmc$periodStart = System.nanoTime();

    @Inject(method = "draw(Lnet/minecraft/client/render/RenderLayer;)V", at = @At("HEAD"))
    private void rtxmc$logDraw(RenderLayer layer, CallbackInfo ci) {
        if (layer == null) return;
        rtxmc$drawsThisPeriod.incrementAndGet();

        // First N unique layers — log each once.
        final String name = layer.toString();
        if (rtxmc$layersSeen.putIfAbsent(name, Boolean.TRUE) == null
                && rtxmc$logBudget.getAndDecrement() > 0) {
            RtxMod.LOG.info("rtxmc batched-draw layer: {} (fmt={}, mode={})",
                    name, layer.getVertexFormat(), layer.getDrawMode());
        }

        // Periodic rate log (every ~5 seconds wall-clock).
        long now = System.nanoTime();
        if (now - rtxmc$periodStart > 5_000_000_000L) {
            long count = rtxmc$drawsThisPeriod.getAndSet(0);
            rtxmc$periodStart = now;
            RtxMod.LOG.info("rtxmc batched-draws/5s: {} ({} unique layers seen total)",
                    count, rtxmc$layersSeen.size());
        }
    }
}
