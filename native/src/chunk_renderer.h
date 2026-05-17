#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

namespace rtxmc {

// Phase 1.4.2 — rasterises one chunk section per draw call. Pipeline accepts
// MC 1.21.11's blessed SOLID-layer vertex format directly (no repacking yet);
// see shaders/chunk.vert for the byte layout.
class ChunkRenderer {
public:
    bool init(VkFormat color_format, VkFormat depth_format);
    void destroy();

    // Records draw commands for ONE section. Caller is responsible for
    // vkCmdBeginRendering / vkCmdEndRendering and viewport state. The shared
    // quad index buffer must be bound before the first call (the renderer
    // does this once per render pass).
    void record(VkCommandBuffer cmd,
                const float view[16],
                const float proj[16],
                int section_x, int section_y, int section_z,
                VkBuffer vertex_buffer,
                uint32_t vertex_count);

private:
    VkPipelineLayout layout_   = VK_NULL_HANDLE;
    VkPipeline       pipeline_ = VK_NULL_HANDLE;
};

} // namespace rtxmc
