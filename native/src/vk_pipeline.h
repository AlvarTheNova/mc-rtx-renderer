#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

// Phase 1.6.1d step 2 — translates Mojang's RenderPipeline state into a
// VkGraphicsPipeline. Java side packs the spec into VkPipelineSpec; native
// side compiles shaders via shaderc, builds the pipeline, returns an opaque
// handle owned by vk_pipeline.cpp.
//
// Scope for first pass:
//   - Vertex + fragment shaders (no geometry/tess/compute)
//   - Single color attachment (matches our swapchain) + optional depth
//   - Standard alpha-blend / additive / disabled blend modes
//   - Depth test funcs from Mojang's DepthTestFunction enum
//   - Cull on/off (no per-face winding override; assume CCW front)
//   - Topology from DrawMode enum
//   - Vertex format = single binding, sequential attribute layout
//
// Deferred:
//   - Push constants / uniform descriptors (need encoder bind story first)
//   - Sampler bindings (same — descriptor sets come with 1.6.1e)
//   - LogicOp (rarely used by MC)
//   - Depth bias

namespace rtxmc {

// Mirrors GpuTexture format codes used by vk_resources.h.
//   0=RGBA8, 1=RED8, 2=RED8I, 3=DEPTH32
// Default color = RGBA8 (we'll wire actual swapchain format later).

struct VkVertexAttrSpec {
    uint32_t location;   // index in shader (matches Java element index)
    uint32_t type_code;  // Mojang VertexFormatElement.Type ordinal
    uint32_t count;      // 1..4
    uint32_t offset;     // bytes from start of vertex
};

struct VkPipelineSpec {
    // Pipeline state (all enums passed as ordinals from Java).
    uint32_t topology;            // DrawMode ordinal — 0=LINES, 4=TRIANGLES, etc.
    uint32_t polygon_mode;        // PolygonMode ordinal — 0=FILL, 1=WIREFRAME
    uint32_t cull;                // 0/1
    uint32_t depth_test_func;     // DepthTestFunction ordinal — 0=NO_DEPTH, 1=EQUAL, 2=LEQUAL, 3=LESS, 4=GREATER
    uint32_t write_depth;         // 0/1
    uint32_t write_color;         // 0/1 — controls RGB channels
    uint32_t write_alpha;         // 0/1 — controls A channel
    uint32_t blend_enabled;       // 0/1
    uint32_t blend_src_color;     // SourceFactor ordinal
    uint32_t blend_dst_color;     // DestFactor ordinal
    uint32_t blend_src_alpha;     // SourceFactor ordinal
    uint32_t blend_dst_alpha;     // DestFactor ordinal

    // Vertex input.
    uint32_t vertex_stride;       // bytes per vertex
    std::vector<VkVertexAttrSpec> attrs;

    // Color attachment format. For now we default to swapchain format
    // (BGRA8_UNORM). The depth format is fixed at D32_SFLOAT (matches our
    // existing depth resource).
    uint32_t color_format_code;   // GpuTexture format code (default 0 = RGBA8)
    uint32_t depth_attachment;    // 0/1 — include depth in dynamic-rendering info
};

// Creates a VkGraphicsPipeline from the spec + GLSL sources. Returns
// opaque handle (uint64_t). 0 on failure. Thread-safe.
uint64_t vkpipe_create(std::string_view vert_glsl,
                       std::string_view frag_glsl,
                       const VkPipelineSpec& spec,
                       std::string_view debug_label);

void vkpipe_destroy(uint64_t handle);

// Look up the underlying VkPipeline + VkPipelineLayout for binding from
// CommandEncoder/RenderPass. Returns {nullptr,nullptr} for invalid handles.
struct VkPipelineRef {
    void* pipeline;       // VkPipeline
    void* layout;         // VkPipelineLayout
};
VkPipelineRef vkpipe_ref(uint64_t handle);

struct VkPipelineStats {
    uint32_t live_pipelines;
    uint32_t total_built;
    uint32_t build_failures;
};
VkPipelineStats vkpipe_stats();

} // namespace rtxmc
