#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace rtxmc {

struct ChunkKey {
    int x, y, z;
    bool operator==(const ChunkKey& o) const { return x==o.x && y==o.y && z==o.z; }
};
struct ChunkKeyHash {
    size_t operator()(const ChunkKey& k) const noexcept {
        // Splitmix-ish; chunks are spatially clustered so we want low collision
        // density across nearby coords.
        uint64_t h = (uint64_t)(uint32_t)k.x * 0x9E3779B97F4A7C15ull;
        h ^= ((uint64_t)(uint32_t)k.y * 0xBF58476D1CE4E5B9ull) + (h << 6) + (h >> 2);
        h ^= ((uint64_t)(uint32_t)k.z * 0x94D049BB133111EBull) + (h << 6) + (h >> 2);
        return (size_t)h;
    }
};

struct ChunkBlas {
    // Phase 1.4.2: now also holds the raster vertex buffer (the same bytes
    // that will eventually feed BLAS construction in Phase 3).
    VkAccelerationStructureKHR as     = VK_NULL_HANDLE;
    VkBuffer                   buffer = VK_NULL_HANDLE;   // VERTEX_BUFFER
    VkDeviceMemory             memory = VK_NULL_HANDLE;   // HOST_VISIBLE + COHERENT
    uint64_t                   device_address = 0;
    uint32_t                   vertex_count   = 0;        // bytes / 32
    uint32_t                   triangle_count = 0;
    uint32_t                   instance_id    = 0;
};

class BvhStore {
public:
    bool init();
    void destroy();

    // Called from rtx_upload_chunk on the JNI thread.
    // Builds (or rebuilds) one BLAS for this 16³ subchunk and queues a
    // TLAS rebuild for next frame.
    void upload_chunk(int cx, int cy, int cz,
                      const void* verts, uint32_t vbytes,
                      const void* idx,   uint32_t ibytes,
                      const void* mats,  uint32_t mbytes);

    void remove_chunk(int cx, int cy, int cz);

    // Called from PathTracer::record between updates and the trace dispatch.
    // Rebuilds (or refits) TLAS if any chunk geometry changed since last frame.
    void update_tlas(VkCommandBuffer cmd);

    VkAccelerationStructureKHR tlas() const { return tlas_; }

    // Snapshot the current chunk map for the render thread. Returns a vector
    // of (key, blas-handles-needed-for-draw) so the renderer can iterate
    // without holding the mutex.
    struct ChunkDraw {
        int32_t  cx, cy, cz;
        VkBuffer buffer;
        uint32_t vertex_count;
    };
    std::vector<ChunkDraw> snapshot_for_draw();

private:
    // Worker-thread chunk uploads contend with render-thread reads.
    std::mutex                 mutex_;
    std::unordered_map<ChunkKey, ChunkBlas, ChunkKeyHash> chunks_;
    VkAccelerationStructureKHR tlas_         = VK_NULL_HANDLE;
    VkBuffer                   tlas_buffer_  = VK_NULL_HANDLE;
    VkDeviceMemory             tlas_memory_  = VK_NULL_HANDLE;
    bool                       tlas_dirty_   = false;
    int                        log_budget_   = 8;

    // Phase 1.4.2 leak-on-replace: when a section re-meshes, the previous
    // VkBuffer/VkDeviceMemory can't be safely freed inline — the render
    // thread may still hold the handle in an in-flight command buffer.
    // We orphan them into this list and only free at destroy() under
    // vkDeviceWaitIdle. Phase 1.4.3 replaces this with proper deferred
    // deletion keyed on frame fences.
    struct OrphanedResources {
        VkBuffer       buffer;
        VkDeviceMemory memory;
    };
    std::vector<OrphanedResources> leaked_;
};

BvhStore& bvh();

} // namespace rtxmc
