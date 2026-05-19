#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace rtxmc {

// Phase 1.5.2c — batched-draw renderer for Immediate.draw layers (entities,
// glints, lines, debug boxes). One class because they all share:
//   - Camera-relative-world vertex coords (use viewRot, not full view)
//   - Same push-constant block (viewRot + proj = 128 B)
//   - Same pipeline layout (set 0 = combined image sampler; lines/debug
//     pipelines just don't sample it — VK accepts unused bound descriptors)
//   - Same per-frame transient HOST_VISIBLE VkBuffer lifecycle via
//     BvhStore::queue_buffer_delete
//
// Routing is by stride (vertex_bytes / vertex_count):
//
//   stride | variant      | topology     | blend          | uses atlas
//   -------+--------------+--------------+----------------+-----------
//     36   | ENTITY       | TRIANGLE_LIST| alpha (src/1-src)| yes
//     20   | GLINT        | TRIANGLE_LIST| additive (1/1) | yes (wrong tex)
//     24   | LINES        | LINE_LIST    | alpha           | no
//     16   | DEBUG_BOX    | TRIANGLE_LIST| alpha          | no
//
// Other strides are silently dropped (12 B Position-only — water_mask /
// end_portal — needs its own shader; out of scope for 1.5.2c).
class EntityRenderer {
public:
    bool init(VkFormat color_format, VkFormat depth_format,
              VkDescriptorSetLayout shared_atlas_dsl);
    void destroy();

    void set_atlas_dset(VkDescriptorSet dset) { atlas_dset_ = dset; }

    // Worker (in practice: render thread). Routes by stride; rejected
    // batches are logged + dropped.
    void upload_batch(int layer_hash, const void* verts,
                      uint32_t vertex_count, uint32_t vertex_bytes);

    // Render all accumulated batches in draw order (entity → debug_box →
    // lines → glint). Queues per-batch VkBuffers for deferred deletion.
    void record_and_consume(VkCommandBuffer cmd,
                            const float view_rot[16],
                            const float proj[16]);

private:
    enum Variant : uint32_t {
        VAR_ENTITY    = 0,  // 36 B
        VAR_GLINT     = 1,  // 20 B
        VAR_LINES     = 2,  // 24 B
        VAR_DEBUG_BOX = 3,  // 16 B
        VAR_COUNT     = 4,
    };

    struct Batch {
        VkBuffer       buffer       = VK_NULL_HANDLE;
        VkDeviceMemory memory       = VK_NULL_HANDLE;
        uint32_t       vertex_count = 0;
    };

    bool build_pipeline(Variant v, VkFormat color_format, VkFormat depth_format);
    bool create_buffer_and_copy(Batch& out, const void* verts, uint32_t bytes);
    void draw_variant(VkCommandBuffer cmd, Variant v);

    VkPipelineLayout layout_    = VK_NULL_HANDLE;
    VkPipeline       pipelines_[VAR_COUNT] = {};
    VkDescriptorSet  atlas_dset_ = VK_NULL_HANDLE;

    std::vector<Batch> batches_[VAR_COUNT];
    int log_budget_ = 16;
};

EntityRenderer& entities();

} // namespace rtxmc
