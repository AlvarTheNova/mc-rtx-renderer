#include "rtx_renderer.h"
#include "vulkan_context.h"
#include "path_tracer.h"
#include "voxel_bvh.h"
#include "streamline_integration.h"

#include <cstdio>

namespace rtxmc {

namespace {
PathTracer g_tracer;

VkCommandPool   g_cmd_pool   = VK_NULL_HANDLE;
VkCommandBuffer g_cmd        = VK_NULL_HANDLE;
VkFence         g_frame_fence = VK_NULL_HANDLE;
} // namespace

int rtx_init(void* hwnd, int w, int h) {
    auto& c = ctx();
    if (!c.init(hwnd, w, h)) return 1;
    if (!sl().init(c.instance, c.device, c.phys)) {
        std::fprintf(stderr, "rtxmc: streamline init failed — continuing without DLSS\n");
    }
    if (!bvh().init())       return 2;
    if (!g_tracer.init())    return 3;

    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = c.gfx_family;
    vkCreateCommandPool(c.device, &cpci, nullptr, &g_cmd_pool);

    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool        = g_cmd_pool;
    cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    vkAllocateCommandBuffers(c.device, &cbai, &g_cmd);

    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(c.device, &fci, nullptr, &g_frame_fence);

    // Initial RT target sizing: assume native = swap, internal = native (no
    // DLSS yet). Phase 2 flips this when SR is enabled.
    g_tracer.resize(c.swap_extent, c.swap_extent);
    return 0;
}

void rtx_resize(int w, int h) {
    ctx().resize(w, h);
    g_tracer.resize(ctx().swap_extent, ctx().swap_extent);
}

void rtx_render_frame(const FrameParams& params) {
    auto& c = ctx();
    vkWaitForFences(c.device, 1, &g_frame_fence, VK_TRUE, UINT64_MAX);
    vkResetFences(c.device, 1, &g_frame_fence);

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(g_cmd, &bi);

    bvh().update_tlas(g_cmd);
    g_tracer.record(g_cmd, params);

    // TODO: build DlssInputs from g_tracer.gbuffer() + params; call
    //       sl().evaluate_rr(g_cmd, in); then tonemap + bloom; then
    //       sl().evaluate_fg(g_cmd, in); then HUD composite + present.

    vkEndCommandBuffer(g_cmd);

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &g_cmd;
    vkQueueSubmit(c.gfx_queue, 1, &si, g_frame_fence);
}

void rtx_shutdown() {
    auto& c = ctx();
    if (c.device) vkDeviceWaitIdle(c.device);
    g_tracer.destroy();
    bvh().destroy();
    sl().destroy();
    if (g_frame_fence) vkDestroyFence(c.device, g_frame_fence, nullptr);
    if (g_cmd_pool)    vkDestroyCommandPool(c.device, g_cmd_pool, nullptr);
    c.destroy();
    g_frame_fence = VK_NULL_HANDLE;
    g_cmd_pool    = VK_NULL_HANDLE;
    g_cmd         = VK_NULL_HANDLE;
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
