#include "voxel_bvh.h"
#include "vulkan_context.h"

namespace rtxmc {

namespace {
BvhStore g_bvh;
} // namespace

BvhStore& bvh() { return g_bvh; }

bool BvhStore::init() {
    // TODO Phase 3:
    //   - Pre-create staging buffer pool for BLAS scratch (compute proper
    //     scratch size from vkGetAccelerationStructureBuildSizesKHR with a
    //     pessimistic triangle count).
    //   - Pre-allocate TLAS instance buffer (max ~16k instances at RD 12).
    return true;
}

void BvhStore::upload_chunk(int cx, int cy, int cz,
                            const void* verts, uint32_t vbytes,
                            const void* idx,   uint32_t ibytes,
                            const void* mats,  uint32_t mbytes) {
    (void)verts; (void)vbytes; (void)idx; (void)ibytes; (void)mats; (void)mbytes;
    ChunkKey k{cx, cy, cz};

    // TODO Phase 3:
    //   1. If chunk already exists, free old BLAS (or rebuild in place if
    //      triangle count fits in existing buffer).
    //   2. Upload vertex+index buffers to device-local memory.
    //   3. Build BLAS via vkCmdBuildAccelerationStructuresKHR with
    //      FAST_TRACE | ALLOW_COMPACTION.
    //   4. Optionally schedule compaction pass next frame.
    //   5. Store material indirection table for closest-hit shader.

    chunks_[k] = ChunkBlas{};
    tlas_dirty_ = true;
}

void BvhStore::remove_chunk(int cx, int cy, int cz) {
    auto it = chunks_.find({cx, cy, cz});
    if (it == chunks_.end()) return;
    auto& c = ctx();
    if (it->second.as)     vkDestroyAccelerationStructureKHR(c.device, it->second.as, nullptr);
    if (it->second.buffer) vkDestroyBuffer(c.device, it->second.buffer, nullptr);
    if (it->second.memory) vkFreeMemory(c.device, it->second.memory, nullptr);
    chunks_.erase(it);
    tlas_dirty_ = true;
}

void BvhStore::update_tlas(VkCommandBuffer cmd) {
    if (!tlas_dirty_) return;
    (void)cmd;
    // TODO Phase 3:
    //   - Pack VkAccelerationStructureInstanceKHR for every entry in chunks_
    //     (transform = chunk-origin translation, instanceCustomIndex = chunk id
    //      for material lookup in chit, accelerationStructureReference =
    //      blas.device_address).
    //   - Upload to instance buffer.
    //   - vkCmdBuildAccelerationStructuresKHR with TYPE_TOP_LEVEL.
    //   - Insert a memory barrier so the trace can read it.
    tlas_dirty_ = false;
}

void BvhStore::destroy() {
    auto& c = ctx();
    for (auto& [_, b] : chunks_) {
        if (b.as)     vkDestroyAccelerationStructureKHR(c.device, b.as, nullptr);
        if (b.buffer) vkDestroyBuffer(c.device, b.buffer, nullptr);
        if (b.memory) vkFreeMemory(c.device, b.memory, nullptr);
    }
    chunks_.clear();
    if (tlas_)        vkDestroyAccelerationStructureKHR(c.device, tlas_, nullptr);
    if (tlas_buffer_) vkDestroyBuffer(c.device, tlas_buffer_, nullptr);
    if (tlas_memory_) vkFreeMemory(c.device, tlas_memory_, nullptr);
    tlas_ = VK_NULL_HANDLE;
    tlas_buffer_ = VK_NULL_HANDLE;
    tlas_memory_ = VK_NULL_HANDLE;
}

} // namespace rtxmc
