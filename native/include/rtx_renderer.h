#pragma once

#include <cstdint>

namespace rtxmc {

// Single struct that mirrors the byte layout written by
// VulkanRenderer.frameParams in Java. Order and padding MUST match.
#pragma pack(push, 1)
struct FrameParams {
    double  cam_x, cam_y, cam_z;
    float   yaw, pitch;
    float   view[16];
    float   proj[16];
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

// Phase 1.4.4: stitched block atlas (RGBA8 row-major).
void rtx_upload_block_atlas(int width, int height,
                            const void* pixels, uint32_t byte_count);

// DLSS settings (called from JNI DlssBridge)
void rtx_set_super_resolution(int preset);
void rtx_set_ray_reconstruction(int preset);
void rtx_set_frame_generation(int factor);

} // namespace rtxmc
