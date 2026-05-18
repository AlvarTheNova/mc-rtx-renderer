package com.rtxmc.mixin;

import com.rtxmc.RtxMod;
import com.rtxmc.render.NativeBridge;
import net.minecraft.client.texture.NativeImage;
import net.minecraft.client.texture.Sprite;
import net.minecraft.client.texture.SpriteAtlasTexture;
import net.minecraft.client.texture.SpriteLoader;
import net.minecraft.util.Identifier;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Map;

/**
 * Phase 1.4.4a — intercept the block atlas as MC finalises it, stitch the
 * individual sprite pixels into one contiguous buffer, forward to native.
 *
 * Why we stitch ourselves: MC 1.21.x doesn't keep a single ByteBuffer of the
 * full atlas around — it builds the GPU texture by uploading each sprite into
 * its slot. Each Sprite's {@link NativeImage} (accessed via the widened
 * {@code SpriteContents.image} field) still holds the source pixels; we copy
 * them into our own atlas buffer at the position MC's stitcher chose.
 *
 * Only the block atlas is captured (the items / particles / banner atlases
 * exist but chunk rendering only samples block textures).
 */
@Mixin(SpriteAtlasTexture.class)
public abstract class SpriteAtlasMixin {

    // HEAD instead of TAIL: SpriteAtlasTexture.create() calls upload() internally
    // which can release the source NativeImages back to the native allocator.
    // Reading sprite pixels at TAIL gave a mostly-empty atlas (only lava-near-(0,0)
    // visible). The StitchResult parameter still holds live sprites before any
    // upload work runs.
    @Inject(method = "create(Lnet/minecraft/client/texture/SpriteLoader$StitchResult;)V",
            at = @At("HEAD"))
    private void rtxmc$captureAtlas(SpriteLoader.StitchResult result, CallbackInfo ci) {
        SpriteAtlasTexture self = (SpriteAtlasTexture) (Object) this;
        if (!SpriteAtlasTexture.BLOCK_ATLAS_TEXTURE.equals(self.id)) return;

        final int w = result.width();
        final int h = result.height();
        final Map<Identifier, Sprite> sprites = result.sprites();
        if (sprites == null || sprites.isEmpty() || w <= 0 || h <= 0) {
            RtxMod.LOG.warn("rtxmc atlas: empty stitch ({}x{}), skipping", w, h);
            return;
        }

        // 4 bytes/pixel RGBA. Direct allocation zeroes the memory, so empty
        // atlas slots stay transparent.
        final int byteCount = w * h * 4;
        ByteBuffer atlas = ByteBuffer.allocateDirect(byteCount).order(ByteOrder.nativeOrder());

        int spritesCopied = 0;
        int nullImage = 0;
        int emptyPixels = 0;
        long totalPixels = 0;
        for (Sprite s : sprites.values()) {
            NativeImage img = s.getContents().image;
            if (img == null) { ++nullImage; continue; }

            // Place sprite where MC's vertex UVs actually point. Earlier
            // debug found Sprite.getX()/getY() returns position BEFORE a
            // 16 px mipmap-border; MC's vertex UVs use the post-border
            // position. Use minU/minV (the canonical MC UVs) to derive
            // pixel position so sampling lines up.
            final int sx = Math.round(s.getMinU() * w);
            final int sy = Math.round(s.getMinV() * h);
            final int sw = Math.round((s.getMaxU() - s.getMinU()) * w);
            final int sh = Math.round((s.getMaxV() - s.getMinV()) * h);
            final int srcStride = img.getWidth();

            int[] px;
            try {
                px = img.copyPixelsArgb();
            } catch (Throwable t) {
                ++nullImage;
                continue;
            }
            if (px == null || px.length == 0) { ++emptyPixels; continue; }

            // Bound by source image and atlas remaining space. Animated
            // textures stack frames vertically (img.height > sh) — we want
            // only the first frame.
            final int copyH = Math.min(Math.min(sh, h - sy), img.getHeight());
            final int copyW = Math.min(Math.min(sw, w - sx), srcStride);
            if (copyH <= 0 || copyW <= 0) { ++emptyPixels; continue; }

            for (int y = 0; y < copyH; ++y) {
                int dstRowStart = ((sy + y) * w + sx) * 4;
                int srcRowStart = y * srcStride;
                for (int x = 0; x < copyW; ++x) {
                    atlas.putInt(dstRowStart + x * 4, argbToRgba(px[srcRowStart + x]));
                }
            }
            totalPixels += (long) copyW * copyH;
            ++spritesCopied;
        }

        RtxMod.LOG.info("rtxmc atlas: BLOCK_ATLAS_TEXTURE captured — {}x{}, {} sprites total ({} copied, {} null/closed, {} empty), {} pixels written, {} buffer bytes",
                w, h, sprites.size(), spritesCopied, nullImage, emptyPixels, totalPixels, byteCount);

        NativeBridge.uploadBlockAtlas(w, h, atlas);
    }

    /** ARGB int (0xAARRGGBB) → RGBA int (0xAABBGGRR if we read LE), but we
     *  want the bytes laid out R G B A in memory order. So we shuffle to
     *  0xAABBGGRR which when written as native-LE int gives R G B A bytes. */
    private static int argbToRgba(int argb) {
        int a = (argb >>> 24) & 0xFF;
        int r = (argb >>> 16) & 0xFF;
        int g = (argb >>>  8) & 0xFF;
        int b = (argb       ) & 0xFF;
        // memory order R, G, B, A → little-endian int: A B G R
        return (a << 24) | (b << 16) | (g << 8) | r;
    }
}
