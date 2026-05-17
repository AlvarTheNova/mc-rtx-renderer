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

    @Inject(method = "create(Lnet/minecraft/client/texture/SpriteLoader$StitchResult;)V",
            at = @At("TAIL"))
    private void rtxmc$captureAtlas(SpriteLoader.StitchResult result, CallbackInfo ci) {
        SpriteAtlasTexture self = (SpriteAtlasTexture) (Object) this;
        if (!SpriteAtlasTexture.BLOCK_ATLAS_TEXTURE.equals(self.id)) return;

        final int w = self.width;
        final int h = self.height;
        final Map<Identifier, Sprite> sprites = self.sprites;
        if (sprites == null || sprites.isEmpty() || w <= 0 || h <= 0) {
            RtxMod.LOG.warn("rtxmc atlas: empty stitch ({}x{}), skipping", w, h);
            return;
        }

        // 4 bytes/pixel RGBA. Direct allocation zeroes the memory, so empty
        // atlas slots stay transparent.
        final int byteCount = w * h * 4;
        ByteBuffer atlas = ByteBuffer.allocateDirect(byteCount).order(ByteOrder.nativeOrder());

        int spritesCopied = 0;
        for (Sprite s : sprites.values()) {
            NativeImage img = s.getContents().image;
            if (img == null) continue;

            final int sx = s.getX();
            final int sy = s.getY();
            final int sw = img.getWidth();
            final int sh = img.getHeight();
            // copyPixelsArgb returns row-major int[]; each int is 0xAARRGGBB.
            int[] px = img.copyPixelsArgb();
            for (int y = 0; y < sh; ++y) {
                int dstRowStart = ((sy + y) * w + sx) * 4;
                int srcRowStart = y * sw;
                for (int x = 0; x < sw; ++x) {
                    atlas.putInt(dstRowStart + x * 4, argbToRgba(px[srcRowStart + x]));
                }
            }
            ++spritesCopied;
        }

        RtxMod.LOG.info("rtxmc atlas: BLOCK_ATLAS_TEXTURE captured — {}x{} ({} sprites, {} bytes)",
                w, h, spritesCopied, byteCount);

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
