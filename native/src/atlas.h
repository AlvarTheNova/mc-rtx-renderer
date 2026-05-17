#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <mutex>

namespace rtxmc {

// Phase 1.4.4 — owns the stitched MC block atlas as a VkImage + sampler.
// Built on the worker thread that calls upload_block_atlas; render thread
// reads the resulting handles (image_view, sampler) once they're ready.
class BlockAtlas {
public:
    bool init();
    void destroy();

    // Worker-thread entry. Performs full staging upload synchronously
    // (vkQueueWaitIdle after submission) — atlas upload is rare (once per
    // resource pack load) so the simple synchronous path is acceptable.
    void upload(int width, int height, const void* rgba_pixels, uint32_t byte_count);

    // Render-thread read. Returns VK_NULL_HANDLE until upload completes
    // and the layout is SHADER_READ_ONLY_OPTIMAL.
    VkImageView view() const    { return view_; }
    VkSampler   sampler() const { return sampler_; }
    bool        ready() const   { return ready_; }
    int         width() const   { return width_; }
    int         height() const  { return height_; }

private:
    std::mutex     mutex_;
    VkImage        image_         = VK_NULL_HANDLE;
    VkDeviceMemory image_memory_  = VK_NULL_HANDLE;
    VkImageView    view_          = VK_NULL_HANDLE;
    VkSampler      sampler_       = VK_NULL_HANDLE;
    VkCommandPool  upload_pool_   = VK_NULL_HANDLE;
    bool           ready_         = false;
    int            width_         = 0;
    int            height_        = 0;
};

BlockAtlas& atlas();

} // namespace rtxmc
