#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <unordered_map>

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
    VkAccelerationStructureKHR as     = VK_NULL_HANDLE;
    VkBuffer                   buffer = VK_NULL_HANDLE;
    VkDeviceMemory             memory = VK_NULL_HANDLE;
    uint64_t                   device_address = 0;
    uint32_t                   triangle_count = 0;
    uint32_t                   instance_id    = 0; // index into TLAS instance array
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

private:
    std::unordered_map<ChunkKey, ChunkBlas, ChunkKeyHash> chunks_;
    VkAccelerationStructureKHR tlas_         = VK_NULL_HANDLE;
    VkBuffer                   tlas_buffer_  = VK_NULL_HANDLE;
    VkDeviceMemory             tlas_memory_  = VK_NULL_HANDLE;
    bool                       tlas_dirty_   = false;
};

BvhStore& bvh();

} // namespace rtxmc
