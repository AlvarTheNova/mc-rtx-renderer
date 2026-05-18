#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

namespace rtxmc {

// Phase 1.4.2 — rasterises one chunk section per draw call. Pipeline accepts
// MC 1.21.11's blessed SOLID-layer vertex format directly (no repacking yet);
// see shaders/chunk.vert for the byte layout.
//
// 1.4.4b adds descriptor set 0 = combined image sampler bound to the block
// atlas. The descriptor set is updated once when the atlas first becomes
// ready (BlockAtlas::ready()) and reused thereafter; bind_descriptor_set()
// is called once per render pass before the chunk draw loop.
class ChunkRenderer {
public:
    bool init(VkFormat color_format, VkFormat depth_format);
    void destroy();

    // Updates the descriptor set to point at the current atlas view+sampler.
    // Idempotent and cheap — call when atlas readiness might have changed.
    void bind_atlas(VkImageView view, VkSampler sampler);
    bool atlas_bound() const { return atlas_bound_; }

    // Bind the descriptor set onto the command buffer. Call once per render
    // pass before issuing chunk draws.
    void bind_descriptor_set(VkCommandBuffer cmd);

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
    VkPipelineLayout      layout_     = VK_NULL_HANDLE;
    VkPipeline            pipeline_   = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsl_        = VK_NULL_HANDLE;
    VkDescriptorPool      dpool_      = VK_NULL_HANDLE;
    VkDescriptorSet       dset_       = VK_NULL_HANDLE;
    bool                  atlas_bound_ = false;
};

} // namespace rtxmc
