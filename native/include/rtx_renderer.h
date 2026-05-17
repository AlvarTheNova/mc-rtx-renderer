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

// Chunk geometry (called from JNI uploadChunk)
void rtx_upload_chunk(int cx, int cy, int cz,
                      const void* vertices, uint32_t vertex_bytes,
                      const void* indices,  uint32_t index_bytes,
                      const void* mat_ids,  uint32_t mat_bytes);
void rtx_remove_chunk(int cx, int cy, int cz);

// DLSS settings (called from JNI DlssBridge)
void rtx_set_super_resolution(int preset);
void rtx_set_ray_reconstruction(int preset);
void rtx_set_frame_generation(int factor);

} // namespace rtxmc
