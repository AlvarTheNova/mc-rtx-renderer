#include "path_tracer.h"
#include "vulkan_context.h"
#include "voxel_bvh.h"

#include <cstdio>

namespace rtxmc {

bool PathTracer::init() {
    // TODO Phase 3:
    //   1. Load compiled SPIR-V (pathtrace.rgen/.rmiss/.rchit) from disk or
    //      from embedded resources.
    //   2. Create ray tracing pipeline via vkCreateRayTracingPipelinesKHR.
    //   3. Allocate shader binding table buffer (size from ctx().rt_props).
    //   4. Create G-buffer images at internal_extent.
    //   5. Build descriptor set: TLAS, bindless texture array, frame UBO,
    //      G-buffer storage images.
    return build_pipeline() && build_sbt() && build_descriptor_set();
}

bool PathTracer::build_pipeline() {
    // TODO. Sketch:
    //   - vkCreateShaderModule for each .spv
    //   - VkRayTracingShaderGroupCreateInfoKHR x3 (rgen, miss, chit)
    //   - VkRayTracingPipelineCreateInfoKHR with maxPipelineRayRecursionDepth = 2
    return true;
}

bool PathTracer::build_sbt() {
    // TODO. Sketch:
    //   - Query group handles with vkGetRayTracingShaderGroupHandlesKHR
    //   - Round to ctx().rt_props.shaderGroupBaseAlignment
    //   - Copy into a device-local buffer (with VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR)
    //   - Fill rgen_region_/miss_region_/hit_region_ with deviceAddress + stride + size
    return true;
}

bool PathTracer::build_descriptor_set() {
    // TODO. Bindings needed:
    //   0: TLAS                                       (acceleration structure)
    //   1: storage image array (color, albedo, normal, roughness, spec, MV)
    //   2: UBO   (camera, jitter, frame index, sky params)
    //   3: bindless sampler2D[]  (block textures, normal maps, MER maps)
    //   4: SSBO  (material table)
    //   5: SSBO  (emissive sample list for ReSTIR DI)
    return true;
}

void PathTracer::resize(VkExtent2D internal_ext, VkExtent2D native_ext) {
    gb_.internal_extent = internal_ext;
    gb_.native_extent   = native_ext;
    // TODO: recreate G-buffer images, rebind descriptors.
}

void PathTracer::record(VkCommandBuffer cmd, const FrameParams& fp) {
    (void)cmd; (void)fp;
    // TODO Phase 3:
    //   1. Update UBO with view/proj, jitter (Halton(2,3)[frame_index_ % 64]),
    //      sun direction (from FrameParams), prev-frame camera.
    //   2. Transition G-buffer images to GENERAL.
    //   3. Bind RT pipeline + descriptor set.
    //   4. vkCmdTraceRaysKHR(cmd, &rgen_region_, &miss_region_, &hit_region_,
    //                        &call_region_, internal_w, internal_h, 1);
    //   5. Transition G-buffer images to SHADER_READ_ONLY for downstream
    //      Streamline / post.
    ++frame_index_;
}

void PathTracer::destroy() {
    auto& c = ctx();
    if (pipeline_)        vkDestroyPipeline(c.device, pipeline_, nullptr);
    if (pipeline_layout_) vkDestroyPipelineLayout(c.device, pipeline_layout_, nullptr);
    if (dsl_)             vkDestroyDescriptorSetLayout(c.device, dsl_, nullptr);
    if (dpool_)           vkDestroyDescriptorPool(c.device, dpool_, nullptr);
    if (sbt_buffer_)      vkDestroyBuffer(c.device, sbt_buffer_, nullptr);
    if (sbt_memory_)      vkFreeMemory(c.device, sbt_memory_, nullptr);
    *this = {};
}

} // namespace rtxmc
