#pragma once

#include <cstdint>

namespace rtxmc {

// Single struct that mirrors the byte layout written by
// VulkanRenderer.frameParams in Java. Order and padding MUST match.
//
// view     = full world→eye matrix = positionMatrix * translate(-camPos)
//            used for chunk geometry which is in world-space coords.
// view_rot = positionMatrix unmodified (rotation only).
//            MC's entity rendering pre-translates by -camPos via MatrixStack
//            BEFORE writing the vertex bytes, so entity verts arrive in
//            camera-relative-world coords; applying view (which translates
//            again) would double-shift. Use view_rot for entities.
#pragma pack(push, 1)
struct FrameParams {
    double  cam_x, cam_y, cam_z;
    float   yaw, pitch;
    float   view[16];
    float   proj[16];
    float   view_rot[16];
    float   tick_delta;
};
#pragma pack(pop)

// Top-level lifecycle
int  rtx_init(void* glfw_window_handle, int width, int height);
void rtx_resize(int width, int height);
void rtx_render_frame(const FrameParams& params);
void rtx_shutdown();

// Render layer IDs — must match Java SectionBuilderMixin and
// chunk_renderer.h LayerId.
enum LayerId {
    LAYER_SOLID       = 0,
    LAYER_CUTOUT      = 1,
    LAYER_TRANSLUCENT = 2,
    LAYER_TRIPWIRE    = 3,
    LAYER_COUNT       = 4,
};

// Chunk geometry (called from JNI uploadChunk). One call per (section, layer).
void rtx_upload_chunk(int cx, int cy, int cz, int layer,
                      const void* vertices, uint32_t vertex_bytes);
void rtx_remove_chunk(int cx, int cy, int cz);

// Phase 1.5.2: per-frame transient entity batch. layer_hash identifies the
// render layer (Java side: layer.toString().hashCode()). vertex_count is
// needed because native filters by stride (only 36 B accepted in 1.5.2b).
void rtx_upload_entity_batch(int layer_hash, uint32_t vertex_count,
                             const void* vertices, uint32_t vertex_bytes);

// Phase 1.4.4: stitched block atlas (RGBA8 row-major).
void rtx_upload_block_atlas(int width, int height,
                            const void* pixels, uint32_t byte_count);

// DLSS settings (called from JNI DlssBridge)
void rtx_set_super_resolution(int preset);
void rtx_set_ray_reconstruction(int preset);
void rtx_set_frame_generation(int factor);

} // namespace rtxmc
