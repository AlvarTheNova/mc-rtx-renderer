#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace rtxmc {

// Phase 1.5.2b — first-light entity rasteriser. Consumes MC's 36 B entity
// vertex format (chunk format + UV1 overlay coords). Reuses the block atlas
// for the fragment sampler (entity textures will be wrong; 1.5.2d wires per-
// mob skins via TextureManager).
//
// Lifecycle vs ChunkRenderer:
//   - Chunks have persistent per-section VkBuffers. Mesh once, draw N times.
//   - Entities re-emit every frame. We accept bytes into transient HOST_VISIBLE
//     VkBuffers each frame; old buffers go through BvhStore's deferred-delete
//     pipeline (same safety-margin pattern).
//
// 1.5.2b restrictions:
//   - Only batches whose bytes/vertex_count == 36 are accepted.
//   - Other formats (LINES=24, GLINT=20, debug 16) are silently dropped until
//     1.5.2c adds pipeline variants.
//   - Single opaque pipeline. Translucent variant deferred to 1.5.2c.
class EntityRenderer {
public:
    bool init(VkFormat color_format, VkFormat depth_format,
              VkDescriptorSetLayout shared_atlas_dsl);
    void destroy();

    // Atlas descriptor — we reuse ChunkRenderer's descriptor set directly.
    // Caller passes its set after bind in record_all.
    void set_atlas_dset(VkDescriptorSet dset) { atlas_dset_ = dset; }

    // Worker JNI thread (well — render thread in practice; entity batches
    // come from Immediate.draw on the main render thread). Allocates a fresh
    // HOST_VISIBLE VkBuffer, copies bytes, stashes for this frame's draw.
    // Filters by stride: only 36 B vertices accepted in 1.5.2b.
    void upload_batch(int layer_hash, const void* verts,
                      uint32_t vertex_count, uint32_t vertex_bytes);

    // Called per-frame after chunks. Binds pipeline + descriptor set,
    // pushes constants, draws every accumulated batch, then queues the
    // batch VkBuffers for deferred deletion.
    void record_and_consume(VkCommandBuffer cmd,
                            const float view_rot[16],
                            const float proj[16]);

private:
    struct EntityBatch {
        VkBuffer       buffer       = VK_NULL_HANDLE;
        VkDeviceMemory memory       = VK_NULL_HANDLE;
        uint32_t       vertex_count = 0;
        int32_t        layer_hash   = 0;
    };

    bool build_pipeline(VkFormat color_format, VkFormat depth_format);

    VkPipelineLayout layout_      = VK_NULL_HANDLE;
    VkPipeline       pipeline_    = VK_NULL_HANDLE;
    VkDescriptorSet  atlas_dset_  = VK_NULL_HANDLE;

    // Current frame's batches. Single-threaded access from the render thread
    // (uploads happen during WorldRenderer.render, draw happens at TAIL of
    // same call). No mutex needed.
    std::vector<EntityBatch> current_batches_;
    int log_budget_ = 8;
};

EntityRenderer& entities();

} // namespace rtxmc
