#include "rtx_renderer.h"
#include "vulkan_context.h"
#include "path_tracer.h"
#include "voxel_bvh.h"
#include "streamline_integration.h"

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cmath>

namespace rtxmc {

namespace {

constexpr uint32_t FRAMES_IN_FLIGHT = 2;

struct FrameSync {
    VkSemaphore     image_available = VK_NULL_HANDLE;
    VkSemaphore     render_finished = VK_NULL_HANDLE;
    VkFence         in_flight       = VK_NULL_HANDLE;
    VkCommandBuffer cmd             = VK_NULL_HANDLE;
};

PathTracer g_tracer;
VkCommandPool g_cmd_pool = VK_NULL_HANDLE;
std::array<FrameSync, FRAMES_IN_FLIGHT> g_frames{};
uint32_t g_frame_idx     = 0;
uint64_t g_frame_counter = 0;
bool     g_swapchain_dirty = false;

void log(const char* fmt, ...) {
    std::fprintf(stderr, "[rtxmc native] ");
    va_list ap; va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
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

// Phase 1.1 placeholder: animate the clear color so a visual diff would
// prove VK output is alive *if* GL weren't winning the swapchain race.
// Phase 1.2 (GL suppression) is what actually makes these pixels visible.
VkClearColorValue animated_clear() {
    float t = (float)g_frame_counter * 0.01f;
    return {{
        0.5f + 0.5f * std::sin(t * 1.0f),
        0.5f + 0.5f * std::sin(t * 1.3f + 2.0f),
        0.5f + 0.5f * std::sin(t * 0.7f + 4.0f),
        1.0f,
    }};
}

void transition_image(VkCommandBuffer cmd, VkImage img,
                      VkImageLayout from, VkImageLayout to,
                      VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                      VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
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
    b.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

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
    if (!bvh().init())                          return 2;
    if (!g_tracer.init())                       return 3;
    if (!create_per_frame_resources())          return 4;

    g_tracer.resize(c.swap_extent, c.swap_extent);
    log("rtx_init complete (device=%s)", c.phys_name);
    return 0;
}

void rtx_resize(int w, int h) {
    log("rtx_resize(%dx%d)", w, h);
    ctx().resize(w, h);
    g_tracer.resize(ctx().swap_extent, ctx().swap_extent);
    g_swapchain_dirty = false;
}

void rtx_render_frame(const FrameParams& params) {
    auto& c = ctx();
    if (!c.device || !c.swapchain) return;

    auto& f = g_frames[g_frame_idx];

    vkWaitForFences(c.device, 1, &f.in_flight, VK_TRUE, UINT64_MAX);

    // Acquire next swapchain image
    uint32_t img_idx = 0;
    VkResult ar = vkAcquireNextImageKHR(c.device, c.swapchain, UINT64_MAX,
                                        f.image_available, VK_NULL_HANDLE, &img_idx);
    if (ar == VK_ERROR_OUT_OF_DATE_KHR) {
        // Surface size changed — defer to next rtx_resize. Skip this frame.
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

    // TODO Phase 3: bvh().update_tlas(f.cmd); g_tracer.record(f.cmd, params);
    // Phase 1.1 stops here — clear + present.
    (void)params;

    VkImage img = c.swap_images[img_idx];

    transition_image(f.cmd, img,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
        VK_PIPELINE_STAGE_2_CLEAR_BIT,       VK_ACCESS_2_TRANSFER_WRITE_BIT);

    VkClearColorValue clear = animated_clear();
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(f.cmd, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &clear, 1, &range);

    transition_image(f.cmd, img,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_PIPELINE_STAGE_2_CLEAR_BIT,         VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);

    vkEndCommandBuffer(f.cmd);

    // Submit: wait on image_available before the clear writes, signal render_finished
    VkSemaphoreSubmitInfo wait_si{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait_si.semaphore = f.image_available;
    wait_si.stageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT;

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
}

void rtx_shutdown() {
    log("rtx_shutdown");
    auto& c = ctx();
    if (c.device) vkDeviceWaitIdle(c.device);
    g_tracer.destroy();
    bvh().destroy();
    sl().destroy();
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
