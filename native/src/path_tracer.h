#pragma once

#include <vulkan/vulkan.h>
#include "rtx_renderer.h"

namespace rtxmc {

// G-buffer images fed to DLSS-RR. All at internal (pre-upscale) resolution
// except depth which is also kept at native res for Frame Gen.
struct GBuffer {
    VkImage color_hdr;          // path-traced HDR linear, pre-tonemap
    VkImage albedo;             // first-hit albedo (for RR demodulation)
    VkImage normal_ws;          // first-hit world-space normal, RGB10A2
    VkImage roughness_metallic; // RG8
    VkImage specular_hit_dist;  // R16F, length of first specular bounce
    VkImage motion_vectors;     // RG16F, NaN = invalid
    VkImage depth_internal;     // D32F at internal res (for RR)
    VkImage depth_native;       // D32F at native res    (for FG)
    VkExtent2D internal_extent;
    VkExtent2D native_extent;
};

class PathTracer {
public:
    bool init();
    void destroy();

    void resize(VkExtent2D internal_ext, VkExtent2D native_ext);
    void record(VkCommandBuffer cmd, const FrameParams& fp);

    const GBuffer& gbuffer() const { return gb_; }

private:
    bool build_pipeline();
    bool build_sbt();
    bool build_descriptor_set();

    VkPipeline           pipeline_       = VK_NULL_HANDLE;
    VkPipelineLayout     pipeline_layout_= VK_NULL_HANDLE;
    VkDescriptorSetLayout dsl_           = VK_NULL_HANDLE;
    VkDescriptorPool     dpool_          = VK_NULL_HANDLE;
    VkDescriptorSet      dset_           = VK_NULL_HANDLE;

    // Shader binding table — VK_KHR_ray_tracing_pipeline
    VkBuffer             sbt_buffer_     = VK_NULL_HANDLE;
    VkDeviceMemory       sbt_memory_     = VK_NULL_HANDLE;
    VkStridedDeviceAddressRegionKHR rgen_region_{}, miss_region_{}, hit_region_{}, call_region_{};

    GBuffer gb_{};
    uint64_t frame_index_ = 0;
};

} // namespace rtxmc
