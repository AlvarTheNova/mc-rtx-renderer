#include "atlas.h"
#include "vulkan_context.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace rtxmc {

namespace {

BlockAtlas g_atlas;

void log(const char* fmt, ...) {
    std::fprintf(stderr, "[rtxmc native] ");
    va_list ap; va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

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

} // namespace

BlockAtlas& atlas() { return g_atlas; }

bool BlockAtlas::init() {
    auto& c = ctx();

    // Sampler is format-agnostic and can be created up front.
    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter    = VK_FILTER_NEAREST;     // MC blocks are pixel-art — no bilinear
    sci.minFilter    = VK_FILTER_NEAREST;
    sci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.maxLod       = 0.0f;
    if (vkCreateSampler(c.device, &sci, nullptr, &sampler_) != VK_SUCCESS) {
        log("BlockAtlas::init: vkCreateSampler failed");
        return false;
    }

    // Dedicated command pool so worker-thread uploads don't contend with
    // the render thread's pool. Externally synchronized (we hold mutex_).
    VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpi.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    cpi.queueFamilyIndex = c.gfx_family;
    if (vkCreateCommandPool(c.device, &cpi, nullptr, &upload_pool_) != VK_SUCCESS) {
        log("BlockAtlas::init: vkCreateCommandPool failed");
        return false;
    }
    log("block atlas init: sampler + upload pool ready (NEAREST/REPEAT)");
    return true;
}

void BlockAtlas::upload(int width, int height, const void* rgba_pixels, uint32_t byte_count) {
    auto& c = ctx();
    if (!c.device) { log("atlas upload before VK init"); return; }
    if (!rgba_pixels || byte_count == 0 || width <= 0 || height <= 0) {
        log("atlas upload: invalid args (w=%d h=%d bytes=%u ptr=%p)",
            width, height, byte_count, rgba_pixels);
        return;
    }

    std::lock_guard<std::mutex> guard(mutex_);
    const VkDeviceSize required = (VkDeviceSize)width * (VkDeviceSize)height * 4;
    if (byte_count < required) {
        log("atlas upload: short buffer (%u < %llu)", byte_count, (unsigned long long)required);
        return;
    }

    // Free any prior atlas (reload). We hold the mutex; render thread reads
    // ready_ outside the lock so flip it false first and wait for in-flight
    // frames to drain before destroying GPU resources.
    if (image_ || view_ || image_memory_) {
        ready_ = false;
        vkDeviceWaitIdle(c.device);
        if (view_)         vkDestroyImageView(c.device, view_, nullptr);
        if (image_)        vkDestroyImage(c.device, image_, nullptr);
        if (image_memory_) vkFreeMemory(c.device, image_memory_, nullptr);
        view_ = VK_NULL_HANDLE;
        image_ = VK_NULL_HANDLE;
        image_memory_ = VK_NULL_HANDLE;
    }

    // -- 1. Create the destination image (DEVICE_LOCAL, sampled + transfer-dst)
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent        = {(uint32_t)width, (uint32_t)height, 1};
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(c.device, &ici, nullptr, &image_) != VK_SUCCESS) {
        log("atlas upload: vkCreateImage failed"); return;
    }

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(c.device, image_, &mr);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = find_memory_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mai.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(c.device, &mai, nullptr, &image_memory_) != VK_SUCCESS) {
        log("atlas upload: image memory alloc failed"); return;
    }
    vkBindImageMemory(c.device, image_, image_memory_, 0);

    // -- 2. Staging buffer (HOST_VISIBLE), copy pixels into it
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    {
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size        = required;
        bci.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(c.device, &bci, nullptr, &staging) != VK_SUCCESS) {
            log("atlas upload: staging buffer create failed"); return;
        }
        VkMemoryRequirements sr;
        vkGetBufferMemoryRequirements(c.device, staging, &sr);
        VkMemoryAllocateInfo sai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        sai.allocationSize  = sr.size;
        sai.memoryTypeIndex = find_memory_type(sr.memoryTypeBits,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (sai.memoryTypeIndex == UINT32_MAX ||
            vkAllocateMemory(c.device, &sai, nullptr, &staging_mem) != VK_SUCCESS) {
            log("atlas upload: staging memory alloc failed"); return;
        }
        vkBindBufferMemory(c.device, staging, staging_mem, 0);
        void* mapped = nullptr;
        vkMapMemory(c.device, staging_mem, 0, required, 0, &mapped);
        std::memcpy(mapped, rgba_pixels, (size_t)required);
        vkUnmapMemory(c.device, staging_mem);
    }

    // -- 3. Record + submit a one-shot command buffer: barrier → copy → barrier
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    {
        VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cbai.commandPool        = upload_pool_;
        cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;
        vkAllocateCommandBuffers(c.device, &cbai, &cmd);

        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);

        // UNDEFINED → TRANSFER_DST_OPTIMAL
        VkImageMemoryBarrier2 b1{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b1.srcStageMask  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        b1.srcAccessMask = 0;
        b1.dstStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT;
        b1.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        b1.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b1.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b1.image = image_;
        b1.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b1.srcQueueFamilyIndex = b1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        VkDependencyInfo di1{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        di1.imageMemoryBarrierCount = 1;
        di1.pImageMemoryBarriers = &b1;
        vkCmdPipelineBarrier2(cmd, &di1);

        VkBufferImageCopy copy{};
        copy.bufferOffset      = 0;
        copy.bufferRowLength   = 0;   // tightly packed
        copy.bufferImageHeight = 0;
        copy.imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent       = {(uint32_t)width, (uint32_t)height, 1};
        vkCmdCopyBufferToImage(cmd, staging, image_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        // TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL
        VkImageMemoryBarrier2 b2{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b2.srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT;
        b2.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        b2.dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        b2.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        b2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b2.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b2.image = image_;
        b2.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b2.srcQueueFamilyIndex = b2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        VkDependencyInfo di2{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        di2.imageMemoryBarrierCount = 1;
        di2.pImageMemoryBarriers = &b2;
        vkCmdPipelineBarrier2(cmd, &di2);

        vkEndCommandBuffer(cmd);

        VkCommandBufferSubmitInfo cbsi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        cbsi.commandBuffer = cmd;
        VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        si.commandBufferInfoCount = 1;
        si.pCommandBufferInfos = &cbsi;
        vkQueueSubmit2(c.gfx_queue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(c.gfx_queue);     // simple: stall until upload done
    }

    // -- 4. Cleanup staging
    vkDestroyBuffer(c.device, staging, nullptr);
    vkFreeMemory(c.device, staging_mem, nullptr);
    vkFreeCommandBuffers(c.device, upload_pool_, 1, &cmd);

    // -- 5. Image view for shader sampling
    VkImageViewCreateInfo ivi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    ivi.image    = image_;
    ivi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivi.format   = VK_FORMAT_R8G8B8A8_UNORM;
    ivi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(c.device, &ivi, nullptr, &view_) != VK_SUCCESS) {
        log("atlas upload: vkCreateImageView failed"); return;
    }

    width_  = width;
    height_ = height;
    ready_  = true;
    log("block atlas uploaded: %dx%d (%llu bytes, R8G8B8A8_UNORM, SHADER_READ layout)",
        width, height, (unsigned long long)required);
}

void BlockAtlas::destroy() {
    auto& c = ctx();
    std::lock_guard<std::mutex> guard(mutex_);
    if (c.device) vkDeviceWaitIdle(c.device);
    if (view_)         vkDestroyImageView(c.device, view_, nullptr);
    if (image_)        vkDestroyImage(c.device, image_, nullptr);
    if (image_memory_) vkFreeMemory(c.device, image_memory_, nullptr);
    if (sampler_)      vkDestroySampler(c.device, sampler_, nullptr);
    if (upload_pool_)  vkDestroyCommandPool(c.device, upload_pool_, nullptr);
    view_ = VK_NULL_HANDLE;
    image_ = VK_NULL_HANDLE;
    image_memory_ = VK_NULL_HANDLE;
    sampler_ = VK_NULL_HANDLE;
    upload_pool_ = VK_NULL_HANDLE;
    ready_ = false;
}

} // namespace rtxmc
