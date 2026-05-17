#pragma once

#include <vulkan/vulkan.h>
#include <atomic>
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

    // Shared quad → triangle index buffer (Phase 1.4.2.5). MC emits QUADS;
    // we synthesise {0,1,2, 2,3,0} × MAX_QUADS once at init() and reuse it
    // for every chunk draw via vkCmdDrawIndexed.
    VkBuffer  shared_quad_index_buffer() const { return quad_index_buffer_; }
    uint32_t  shared_quad_index_capacity_indices() const { return quad_index_capacity_; }

    // Frame ticking for deferred deletion. Render thread calls tick_frame()
    // at the start of each frame; worker threads observe the counter when
    // queueing pending deletes. flush_pending_deletes() actually destroys
    // resources whose safe-frame has passed — called by render thread after
    // vkWaitForFences (which guarantees frames N-FRAMES_IN_FLIGHT are done).
    void     tick_frame()                  { current_frame_.fetch_add(1, std::memory_order_release); }
    uint64_t current_frame() const         { return current_frame_.load(std::memory_order_acquire); }
    void     flush_pending_deletes();

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

    // Phase 1.4.3 deferred deletion. When a section re-meshes (worker
    // thread) the old VkBuffer can't free inline — render thread may have
    // it queued in an in-flight command buffer. We push into pending_; the
    // render thread drains entries whose safe_at_frame is past after
    // vkWaitForFences guarantees the relevant frame is done on GPU.
    struct PendingDelete {
        VkBuffer       buffer;
        VkDeviceMemory memory;
        uint64_t       safe_at_frame;
    };
    std::vector<PendingDelete> pending_;
    std::atomic<uint64_t>      current_frame_{0};

    VkBuffer       quad_index_buffer_   = VK_NULL_HANDLE;
    VkDeviceMemory quad_index_memory_   = VK_NULL_HANDLE;
    uint32_t       quad_index_capacity_ = 0;  // in indices
};

BvhStore& bvh();

} // namespace rtxmc
