#include "voxel_bvh.h"
#include "vulkan_context.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace rtxmc {

namespace {

void log(const char* fmt, ...) {
    std::fprintf(stderr, "[rtxmc native] ");
    va_list ap; va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

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

namespace {

constexpr uint32_t MC_VERTEX_STRIDE = 32; // [Position, Color, UV0, UV2, Normal]

uint32_t find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags wanted) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(ctx().phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & wanted) == wanted) {
            return i;
        }
    }
    return UINT32_MAX;
}

void free_chunk(ChunkBlas& b) {
    auto& c = ctx();
    if (b.as)     c.ext.vkDestroyAccelerationStructureKHR(c.device, b.as, nullptr);
    if (b.buffer) vkDestroyBuffer(c.device, b.buffer, nullptr);
    if (b.memory) vkFreeMemory(c.device, b.memory, nullptr);
    b = {};
}

} // namespace

void BvhStore::upload_chunk(int cx, int cy, int cz,
                            const void* verts, uint32_t vbytes,
                            const void* idx,   uint32_t ibytes,
                            const void* mats,  uint32_t mbytes) {
    (void)idx; (void)mats; (void)ibytes; (void)mbytes;
    ChunkKey k{cx, cy, cz};
    auto& c = ctx();

    std::lock_guard<std::mutex> guard(mutex_);

    // Tear down any previous incarnation of this section.
    auto it = chunks_.find(k);
    if (it != chunks_.end()) {
        free_chunk(it->second);
        chunks_.erase(it);
    }

    if (!verts || vbytes == 0) {
        tlas_dirty_ = true;
        return;
    }

    ChunkBlas b{};
    b.vertex_count = vbytes / MC_VERTEX_STRIDE;

    // Buffer creation. HOST_VISIBLE + COHERENT keeps Phase 1.4.2 simple — no
    // staging buffer, no transfer queue dance. Phase 1.4.3+ should move to
    // DEVICE_LOCAL with a staging upload for proper bandwidth.
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size        = vbytes;
    bci.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(c.device, &bci, nullptr, &b.buffer) != VK_SUCCESS) {
        log("upload_chunk: vkCreateBuffer failed (section %d,%d,%d, %u B)", cx, cy, cz, vbytes);
        return;
    }

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(c.device, b.buffer, &mr);

    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = find_memory_type(mr.memoryTypeBits,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(c.device, &ai, nullptr, &b.memory) != VK_SUCCESS) {
        log("upload_chunk: memory allocation failed (section %d,%d,%d)", cx, cy, cz);
        vkDestroyBuffer(c.device, b.buffer, nullptr);
        return;
    }
    vkBindBufferMemory(c.device, b.buffer, b.memory, 0);

    void* mapped = nullptr;
    if (vkMapMemory(c.device, b.memory, 0, vbytes, 0, &mapped) != VK_SUCCESS) {
        log("upload_chunk: vkMapMemory failed (section %d,%d,%d)", cx, cy, cz);
        free_chunk(b);
        return;
    }
    std::memcpy(mapped, verts, vbytes);
    vkUnmapMemory(c.device, b.memory);

    chunks_[k] = b;
    tlas_dirty_ = true;

    if (log_budget_ > 0) {
        --log_budget_;
        log("upload_chunk section=(%d,%d,%d): allocated %u-vert VkBuffer (%u B)",
            cx, cy, cz, b.vertex_count, vbytes);
    }
}

std::vector<BvhStore::ChunkDraw> BvhStore::snapshot_for_draw() {
    std::lock_guard<std::mutex> guard(mutex_);
    std::vector<ChunkDraw> out;
    out.reserve(chunks_.size());
    for (auto& [k, b] : chunks_) {
        if (b.buffer && b.vertex_count) {
            out.push_back({k.x, k.y, k.z, b.buffer, b.vertex_count});
        }
    }
    return out;
}

void BvhStore::remove_chunk(int cx, int cy, int cz) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = chunks_.find({cx, cy, cz});
    if (it == chunks_.end()) return;
    free_chunk(it->second);
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
    std::lock_guard<std::mutex> guard(mutex_);
    auto& c = ctx();
    for (auto& [_, b] : chunks_) {
        free_chunk(b);
    }
    chunks_.clear();
    if (tlas_)        c.ext.vkDestroyAccelerationStructureKHR(c.device, tlas_, nullptr);
    if (tlas_buffer_) vkDestroyBuffer(c.device, tlas_buffer_, nullptr);
    if (tlas_memory_) vkFreeMemory(c.device, tlas_memory_, nullptr);
    tlas_ = VK_NULL_HANDLE;
    tlas_buffer_ = VK_NULL_HANDLE;
    tlas_memory_ = VK_NULL_HANDLE;
}

} // namespace rtxmc
