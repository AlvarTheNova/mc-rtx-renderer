#include "rtx_renderer.h"
#include "vulkan_context.h"
#include "path_tracer.h"
#include "voxel_bvh.h"
#include "streamline_integration.h"
#include "triangle.h"
#include "chunk_renderer.h"

#include <array>
#include <cstdarg>
#include <cstdio>

namespace rtxmc {

namespace {

constexpr uint32_t FRAMES_IN_FLIGHT = 2;
constexpr VkFormat DEPTH_FORMAT     = VK_FORMAT_D32_SFLOAT;

struct FrameSync {
    VkSemaphore     image_available = VK_NULL_HANDLE;
    VkSemaphore     render_finished = VK_NULL_HANDLE;
    VkFence         in_flight       = VK_NULL_HANDLE;
    VkCommandBuffer cmd             = VK_NULL_HANDLE;
};

PathTracer    g_tracer;
Triangle      g_triangle;
ChunkRenderer g_chunks;
VkCommandPool g_cmd_pool = VK_NULL_HANDLE;
std::array<FrameSync, FRAMES_IN_FLIGHT> g_frames{};
uint32_t g_frame_idx     = 0;
uint64_t g_frame_counter = 0;
bool     g_swapchain_dirty = false;

// Depth resources (recreated on swapchain resize).
VkImage        g_depth_image  = VK_NULL_HANDLE;
VkDeviceMemory g_depth_memory = VK_NULL_HANDLE;
VkImageView    g_depth_view   = VK_NULL_HANDLE;
VkExtent2D     g_depth_extent = {0, 0};

void log(const char* fmt, ...) {
    std::fprintf(stderr, "[rtxmc native] ");
    va_list ap; va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

uint32_t find_memory_type_for_image(uint32_t type_bits, VkMemoryPropertyFlags wanted) {
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

void destroy_depth_resources() {
    auto& c = ctx();
    if (g_depth_view)   vkDestroyImageView(c.device, g_depth_view, nullptr);
    if (g_depth_image)  vkDestroyImage(c.device, g_depth_image, nullptr);
    if (g_depth_memory) vkFreeMemory(c.device, g_depth_memory, nullptr);
    g_depth_view = VK_NULL_HANDLE;
    g_depth_image = VK_NULL_HANDLE;
    g_depth_memory = VK_NULL_HANDLE;
    g_depth_extent = {0, 0};
}

bool create_depth_resources(VkExtent2D extent) {
    auto& c = ctx();
    if (g_depth_extent.width == extent.width && g_depth_extent.height == extent.height
            && g_depth_image) return true;
    destroy_depth_resources();

    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = DEPTH_FORMAT;
    ici.extent        = {extent.width, extent.height, 1};
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(c.device, &ici, nullptr, &g_depth_image) != VK_SUCCESS) {
        log("create_depth_resources: vkCreateImage failed"); return false;
    }

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(c.device, g_depth_image, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = find_memory_type_for_image(mr.memoryTypeBits,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(c.device, &ai, nullptr, &g_depth_memory) != VK_SUCCESS) {
        log("create_depth_resources: memory alloc failed"); return false;
    }
    vkBindImageMemory(c.device, g_depth_image, g_depth_memory, 0);

    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image    = g_depth_image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format   = DEPTH_FORMAT;
    vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(c.device, &vi, nullptr, &g_depth_view) != VK_SUCCESS) {
        log("create_depth_resources: image view failed"); return false;
    }
    g_depth_extent = extent;
    log("depth resources created (%ux%u, format=D32_SFLOAT)", extent.width, extent.height);
    return true;
}

bool create_per_frame_resources() {
    auto& c = ctx();

    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = c.gfx_family;
    if (vkCreateCommandPool(c.device, &cpci, nullptr, &g_cmd_pool) != VK_SUCCESS) {
        log("vkCreateCommandPool failed"); return false;
    }

    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        auto& f = g_frames[i];
        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(c.device, &sci, nullptr, &f.image_available);
        vkCreateSemaphore(c.device, &sci, nullptr, &f.render_finished);

        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(c.device, &fci, nullptr, &f.in_flight);

        VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cbai.commandPool        = g_cmd_pool;
        cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;
        vkAllocateCommandBuffers(c.device, &cbai, &f.cmd);
    }
    log("per-frame sync: %u frames-in-flight, %u command buffers",
        FRAMES_IN_FLIGHT, FRAMES_IN_FLIGHT);
    return true;
}

void destroy_per_frame_resources() {
    auto& c = ctx();
    for (auto& f : g_frames) {
        if (f.image_available) vkDestroySemaphore(c.device, f.image_available, nullptr);
        if (f.render_finished) vkDestroySemaphore(c.device, f.render_finished, nullptr);
        if (f.in_flight)       vkDestroyFence(c.device, f.in_flight, nullptr);
        f = {};
    }
    if (g_cmd_pool) { vkDestroyCommandPool(c.device, g_cmd_pool, nullptr); g_cmd_pool = VK_NULL_HANDLE; }
}

void transition_image(VkCommandBuffer cmd, VkImage img,
                      VkImageLayout from, VkImageLayout to,
                      VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                      VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access,
                      VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) {
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask        = src_stage;
    b.srcAccessMask       = src_access;
    b.dstStageMask        = dst_stage;
    b.dstAccessMask       = dst_access;
    b.oldLayout           = from;
    b.newLayout           = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = img;
    b.subresourceRange    = {aspect, 0, 1, 0, 1};

    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.imageMemoryBarrierCount = 1;
    di.pImageMemoryBarriers    = &b;
    vkCmdPipelineBarrier2(cmd, &di);
}

} // namespace

int rtx_init(void* hwnd, int w, int h) {
    log("rtx_init(hwnd=%p, %dx%d)", hwnd, w, h);
    auto& c = ctx();
    if (!c.init(hwnd, w, h)) { log("VulkanContext init failed"); return 1; }

    if (!sl().init(c.instance, c.device, c.phys)) {
        log("streamline init failed — continuing without DLSS");
    }
    if (!bvh().init())                                          return 2;
    if (!g_tracer.init())                                       return 3;
    if (!create_per_frame_resources())                          return 4;
    if (!g_triangle.init(c.swap_format, DEPTH_FORMAT))          return 5;
    if (!g_chunks.init(c.swap_format, DEPTH_FORMAT))            return 6;
    if (!create_depth_resources(c.swap_extent))                 return 7;

    g_tracer.resize(c.swap_extent, c.swap_extent);
    log("rtx_init complete (device=%s)", c.phys_name);
    return 0;
}

void rtx_resize(int w, int h) {
    log("rtx_resize(%dx%d)", w, h);
    ctx().resize(w, h);
    g_tracer.resize(ctx().swap_extent, ctx().swap_extent);
    create_depth_resources(ctx().swap_extent);
    g_swapchain_dirty = false;
}

void rtx_render_frame(const FrameParams& params) {
    auto& c = ctx();
    if (!c.device || !c.swapchain) return;

    auto& f = g_frames[g_frame_idx];

    vkWaitForFences(c.device, 1, &f.in_flight, VK_TRUE, UINT64_MAX);

    // After the fence: GPU is done with this slot's previous submission, so
    // any resources queued for deletion at or before this frame are safe.
    bvh().flush_pending_deletes();

    uint32_t img_idx = 0;
    VkResult ar = vkAcquireNextImageKHR(c.device, c.swapchain, UINT64_MAX,
                                        f.image_available, VK_NULL_HANDLE, &img_idx);
    if (ar == VK_ERROR_OUT_OF_DATE_KHR) {
        g_swapchain_dirty = true;
        return;
    } else if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) {
        log("vkAcquireNextImageKHR failed (%d)", ar);
        return;
    }

    vkResetFences(c.device, 1, &f.in_flight);
    vkResetCommandBuffer(f.cmd, 0);

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(f.cmd, &bi);

    VkImage img = c.swap_images[img_idx];

    // Color: UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
    transition_image(f.cmd, img,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,             0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);

    // Depth: UNDEFINED → DEPTH_ATTACHMENT_OPTIMAL
    transition_image(f.cmd, g_depth_image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,                   0,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
            | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
            | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT);

    VkRenderingAttachmentInfo color_att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color_att.imageView   = c.swap_views[img_idx];
    color_att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_att.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_att.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    color_att.clearValue.color = {{0.08f, 0.10f, 0.14f, 1.0f}};

    VkRenderingAttachmentInfo depth_att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth_att.imageView   = g_depth_view;
    depth_att.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth_att.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_att.storeOp     = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_att.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo render_info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    render_info.renderArea.extent    = c.swap_extent;
    render_info.layerCount           = 1;
    render_info.colorAttachmentCount = 1;
    render_info.pColorAttachments    = &color_att;
    render_info.pDepthAttachment     = &depth_att;

    vkCmdBeginRendering(f.cmd, &render_info);

    // Dynamic viewport / scissor (both triangle + chunk pipelines use these)
    VkViewport vp{};
    vp.x = 0.0f; vp.y = 0.0f;
    vp.width  = (float)c.swap_extent.width;
    vp.height = (float)c.swap_extent.height;
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    vkCmdSetViewport(f.cmd, 0, 1, &vp);
    VkRect2D scissor{}; scissor.extent = c.swap_extent;
    vkCmdSetScissor(f.cmd, 0, 1, &scissor);

    // Chunks first (depth-tested terrain). Snapshot under mutex; iterate outside.
    // Bind the shared quad → triangle index buffer once per render pass; each
    // chunk draw reuses it through vkCmdDrawIndexed.
    auto draws = bvh().snapshot_for_draw();
    if (!draws.empty()) {
        vkCmdBindIndexBuffer(f.cmd, bvh().shared_quad_index_buffer(), 0,
                             VK_INDEX_TYPE_UINT32);
    }
    for (const auto& d : draws) {
        // Sanity: skip any section bigger than our shared index buffer can index.
        const uint32_t needed_indices = (d.vertex_count / 4) * 6;
        if (needed_indices > bvh().shared_quad_index_capacity_indices()) continue;
        g_chunks.record(f.cmd, params.view, params.proj,
                        d.cx, d.cy, d.cz, d.buffer, d.vertex_count);
    }

    // Sanity triangle on top (always-visible NDC sentinel + world triangle).
    g_triangle.record(f.cmd, params.view, params.proj, c.swap_extent);

    vkCmdEndRendering(f.cmd);

    transition_image(f.cmd, img,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,    0,
        VK_IMAGE_ASPECT_COLOR_BIT);

    vkEndCommandBuffer(f.cmd);

    VkSemaphoreSubmitInfo wait_si{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait_si.semaphore = f.image_available;
    wait_si.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo sig_si{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    sig_si.semaphore = f.render_finished;
    sig_si.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;

    VkCommandBufferSubmitInfo cbsi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cbsi.commandBuffer = f.cmd;

    VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    si.waitSemaphoreInfoCount   = 1; si.pWaitSemaphoreInfos   = &wait_si;
    si.commandBufferInfoCount   = 1; si.pCommandBufferInfos   = &cbsi;
    si.signalSemaphoreInfoCount = 1; si.pSignalSemaphoreInfos = &sig_si;
    vkQueueSubmit2(c.gfx_queue, 1, &si, f.in_flight);

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &f.render_finished;
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &c.swapchain;
    pi.pImageIndices      = &img_idx;
    VkResult pr = vkQueuePresentKHR(c.gfx_queue, &pi);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) {
        g_swapchain_dirty = true;
    } else if (pr != VK_SUCCESS) {
        log("vkQueuePresentKHR failed (%d)", pr);
    }

    g_frame_idx = (g_frame_idx + 1) % FRAMES_IN_FLIGHT;
    ++g_frame_counter;

    // Tell BvhStore a frame has elapsed — worker threads see the new counter
    // when queueing future deletions.
    bvh().tick_frame();
}

void rtx_shutdown() {
    log("rtx_shutdown");
    auto& c = ctx();
    if (c.device) vkDeviceWaitIdle(c.device);
    g_chunks.destroy();
    g_triangle.destroy();
    g_tracer.destroy();
    bvh().destroy();
    sl().destroy();
    destroy_depth_resources();
    destroy_per_frame_resources();
    c.destroy();
}

// ---- Chunk + DLSS forwarders -----------------------------------------------

void rtx_upload_chunk(int cx, int cy, int cz,
                      const void* v, uint32_t vb,
                      const void* i, uint32_t ib,
                      const void* m, uint32_t mb) {
    bvh().upload_chunk(cx, cy, cz, v, vb, i, ib, m, mb);
}
void rtx_remove_chunk(int cx, int cy, int cz) { bvh().remove_chunk(cx, cy, cz); }

void rtx_set_super_resolution(int p)   { sl().set_sr_preset(p); }
void rtx_set_ray_reconstruction(int p) { sl().set_rr_enabled(p); }
void rtx_set_frame_generation(int f)   { sl().set_fg_factor(f); }

} // namespace rtxmc
