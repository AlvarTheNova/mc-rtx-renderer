#include "vk_pipeline.h"
#include "vk_shaderc.h"
#include "vulkan_context.h"

#include <vulkan/vulkan.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>

namespace rtxmc {
namespace {

void log(const char* fmt, ...) {
    std::fprintf(stderr, "[rtxmc vkpipe] ");
    va_list ap; va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

// ---- Mojang enum → VK enum translation tables -----------------------------

// DrawMode ordinal → topology. Mojang's QUADS is special — VK has no QUADS
// primitive, expand to TRIANGLE_LIST and rely on an external index buffer.
// (DrawMode order from javap: LINES, DEBUG_LINES, DEBUG_LINE_STRIP, POINTS,
//  TRIANGLES, TRIANGLE_STRIP, TRIANGLE_FAN, QUADS)
VkPrimitiveTopology translate_topology(uint32_t mc_drawmode) {
    switch (mc_drawmode) {
        case 0: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;        // LINES
        case 1: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;        // DEBUG_LINES
        case 2: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;       // DEBUG_LINE_STRIP
        case 3: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;       // POINTS
        case 4: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;    // TRIANGLES
        case 5: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;   // TRIANGLE_STRIP
        case 6: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;     // TRIANGLE_FAN
        case 7: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;    // QUADS — expand via index buf
        default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

// PolygonMode ordinal → VK enum.  0=FILL, 1=WIREFRAME
VkPolygonMode translate_polygon_mode(uint32_t mc) {
    return (mc == 1) ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
}

// DepthTestFunction ordinal — 0=NO_DEPTH, 1=EQUAL, 2=LEQUAL, 3=LESS, 4=GREATER
VkCompareOp translate_depth_compare(uint32_t mc) {
    switch (mc) {
        case 1: return VK_COMPARE_OP_EQUAL;
        case 2: return VK_COMPARE_OP_LESS_OR_EQUAL;
        case 3: return VK_COMPARE_OP_LESS;
        case 4: return VK_COMPARE_OP_GREATER;
        default: return VK_COMPARE_OP_ALWAYS; // NO_DEPTH_TEST equivalent
    }
}

// SourceFactor / DestFactor ordinals (alphabetic from javap):
//   0=CONSTANT_ALPHA, 1=CONSTANT_COLOR, 2=DST_ALPHA, 3=DST_COLOR, 4=ONE,
//   5=ONE_MINUS_CONSTANT_ALPHA, 6=ONE_MINUS_CONSTANT_COLOR,
//   7=ONE_MINUS_DST_ALPHA, 8=ONE_MINUS_DST_COLOR,
//   9=ONE_MINUS_SRC_ALPHA, 10=ONE_MINUS_SRC_COLOR,
//   11=SRC_ALPHA, [12=SRC_ALPHA_SATURATE on SrcFactor only],
//   12/13=SRC_COLOR, 13/14=ZERO. Note SrcFactor has 15 vals (extra SRC_ALPHA_SATURATE),
//   DstFactor has 14 vals.
VkBlendFactor translate_blend_factor(uint32_t mc, bool is_src) {
    // Common values match in both enums by name; just dispatch.
    switch (mc) {
        case 0:  return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case 1:  return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case 2:  return VK_BLEND_FACTOR_DST_ALPHA;
        case 3:  return VK_BLEND_FACTOR_DST_COLOR;
        case 4:  return VK_BLEND_FACTOR_ONE;
        case 5:  return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        case 6:  return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case 7:  return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case 8:  return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case 9:  return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case 10: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case 11: return VK_BLEND_FACTOR_SRC_ALPHA;
        case 12: return is_src ? VK_BLEND_FACTOR_SRC_ALPHA_SATURATE : VK_BLEND_FACTOR_SRC_COLOR;
        case 13: return is_src ? VK_BLEND_FACTOR_SRC_COLOR          : VK_BLEND_FACTOR_ZERO;
        case 14: return is_src ? VK_BLEND_FACTOR_ZERO               : VK_BLEND_FACTOR_ZERO;
        default: return VK_BLEND_FACTOR_ZERO;
    }
}

// VertexFormatElement.Type ordinal → VkFormat (paired with count).
// Mojang Type enum order (from source): FLOAT, UBYTE, BYTE, USHORT, SHORT, UINT, INT
VkFormat translate_vertex_format(uint32_t type, uint32_t count) {
    if (count < 1 || count > 4) return VK_FORMAT_UNDEFINED;
    static const VkFormat kFloat[]  = {VK_FORMAT_UNDEFINED,
        VK_FORMAT_R32_SFLOAT,        VK_FORMAT_R32G32_SFLOAT,
        VK_FORMAT_R32G32B32_SFLOAT,  VK_FORMAT_R32G32B32A32_SFLOAT};
    static const VkFormat kUByte[]  = {VK_FORMAT_UNDEFINED,
        VK_FORMAT_R8_UINT, VK_FORMAT_R8G8_UINT, VK_FORMAT_R8G8B8_UINT, VK_FORMAT_R8G8B8A8_UINT};
    static const VkFormat kByte[]   = {VK_FORMAT_UNDEFINED,
        VK_FORMAT_R8_SINT, VK_FORMAT_R8G8_SINT, VK_FORMAT_R8G8B8_SINT, VK_FORMAT_R8G8B8A8_SINT};
    static const VkFormat kUShort[] = {VK_FORMAT_UNDEFINED,
        VK_FORMAT_R16_UINT, VK_FORMAT_R16G16_UINT, VK_FORMAT_R16G16B16_UINT, VK_FORMAT_R16G16B16A16_UINT};
    static const VkFormat kShort[]  = {VK_FORMAT_UNDEFINED,
        VK_FORMAT_R16_SINT, VK_FORMAT_R16G16_SINT, VK_FORMAT_R16G16B16_SINT, VK_FORMAT_R16G16B16A16_SINT};
    static const VkFormat kUInt[]   = {VK_FORMAT_UNDEFINED,
        VK_FORMAT_R32_UINT, VK_FORMAT_R32G32_UINT, VK_FORMAT_R32G32B32_UINT, VK_FORMAT_R32G32B32A32_UINT};
    static const VkFormat kInt[]    = {VK_FORMAT_UNDEFINED,
        VK_FORMAT_R32_SINT, VK_FORMAT_R32G32_SINT, VK_FORMAT_R32G32B32_SINT, VK_FORMAT_R32G32B32A32_SINT};
    switch (type) {
        case 0: return kFloat[count];
        case 1: return kUByte[count];
        case 2: return kByte[count];
        case 3: return kUShort[count];
        case 4: return kShort[count];
        case 5: return kUInt[count];
        case 6: return kInt[count];
        default: return VK_FORMAT_UNDEFINED;
    }
}

VkFormat translate_color_format(uint32_t code) {
    // Mirror vk_resources.cpp's table.
    switch (code) {
        case 0: return VK_FORMAT_R8G8B8A8_UNORM;
        case 1: return VK_FORMAT_R8_UNORM;
        case 2: return VK_FORMAT_R8_SINT;
        default: return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

// ---- Pipeline registry -----------------------------------------------------

struct PipelineRec {
    VkPipeline       pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout   = VK_NULL_HANDLE;
};

std::mutex g_mutex;
std::unordered_map<uint64_t, PipelineRec> g_pipelines;
std::atomic<uint64_t> g_next_handle{1};
std::atomic<uint32_t> g_total_built{0};
std::atomic<uint32_t> g_build_failures{0};

uint64_t alloc_handle() { return g_next_handle.fetch_add(1, std::memory_order_relaxed); }

VkShaderModule make_module(VkDevice dev, const std::vector<uint32_t>& spirv) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = spirv.size() * sizeof(uint32_t);
    ci.pCode    = spirv.data();
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(dev, &ci, nullptr, &m) != VK_SUCCESS) {
        log("vkCreateShaderModule failed (%zu words)", spirv.size());
    }
    return m;
}

} // namespace

uint64_t vkpipe_create(std::string_view vert_glsl, std::string_view frag_glsl,
                       const VkPipelineSpec& spec, std::string_view label) {
    if (ctx().device == VK_NULL_HANDLE) {
        log("vkpipe_create: device not ready (%.*s)", (int)label.size(), label.data());
        return 0;
    }
    auto& c = ctx();
    const std::string label_s(label);

    // 1. Compile shaders.
    auto vs_spirv = shaderc_compile(vert_glsl, ShaderStage::Vertex,   label_s + "[vs]");
    auto fs_spirv = shaderc_compile(frag_glsl, ShaderStage::Fragment, label_s + "[fs]");
    if (vs_spirv.empty() || fs_spirv.empty()) {
        log("vkpipe_create: shader compile failed (%s)", label_s.c_str());
        g_build_failures.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }

    VkShaderModule vs = make_module(c.device, vs_spirv);
    VkShaderModule fs = make_module(c.device, fs_spirv);
    if (!vs || !fs) {
        if (vs) vkDestroyShaderModule(c.device, vs, nullptr);
        if (fs) vkDestroyShaderModule(c.device, fs, nullptr);
        g_build_failures.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName  = "main";

    // 2. Vertex input.
    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = spec.vertex_stride;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> vk_attrs;
    vk_attrs.reserve(spec.attrs.size());
    for (const auto& a : spec.attrs) {
        VkFormat fmt = translate_vertex_format(a.type_code, a.count);
        if (fmt == VK_FORMAT_UNDEFINED) {
            log("vkpipe_create: bad vertex attr (loc=%u type=%u count=%u)",
                a.location, a.type_code, a.count);
            vkDestroyShaderModule(c.device, vs, nullptr);
            vkDestroyShaderModule(c.device, fs, nullptr);
            g_build_failures.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }
        VkVertexInputAttributeDescription vk_a{};
        vk_a.location = a.location;
        vk_a.binding  = 0;
        vk_a.format   = fmt;
        vk_a.offset   = a.offset;
        vk_attrs.push_back(vk_a);
    }

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount   = spec.vertex_stride > 0 ? 1 : 0;
    vi.pVertexBindingDescriptions      = spec.vertex_stride > 0 ? &binding : nullptr;
    vi.vertexAttributeDescriptionCount = (uint32_t)vk_attrs.size();
    vi.pVertexAttributeDescriptions    = vk_attrs.empty() ? nullptr : vk_attrs.data();

    // 3. State blocks.
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = translate_topology(spec.topology);

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = translate_polygon_mode(spec.polygon_mode);
    rs.cullMode    = spec.cull ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable  = spec.depth_test_func == 0 ? VK_FALSE : VK_TRUE;
    ds.depthWriteEnable = spec.write_depth ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp   = translate_depth_compare(spec.depth_test_func);
    ds.minDepthBounds   = 0.0f;
    ds.maxDepthBounds   = 1.0f;

    VkPipelineColorBlendAttachmentState cb_att{};
    cb_att.colorWriteMask = 0;
    if (spec.write_color) cb_att.colorWriteMask |= VK_COLOR_COMPONENT_R_BIT
                                                  | VK_COLOR_COMPONENT_G_BIT
                                                  | VK_COLOR_COMPONENT_B_BIT;
    if (spec.write_alpha) cb_att.colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;
    cb_att.blendEnable         = spec.blend_enabled ? VK_TRUE : VK_FALSE;
    cb_att.srcColorBlendFactor = translate_blend_factor(spec.blend_src_color, /*is_src*/ true);
    cb_att.dstColorBlendFactor = translate_blend_factor(spec.blend_dst_color, /*is_src*/ false);
    cb_att.colorBlendOp        = VK_BLEND_OP_ADD;
    cb_att.srcAlphaBlendFactor = translate_blend_factor(spec.blend_src_alpha, /*is_src*/ true);
    cb_att.dstAlphaBlendFactor = translate_blend_factor(spec.blend_dst_alpha, /*is_src*/ false);
    cb_att.alphaBlendOp        = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments    = &cb_att;

    VkDynamicState dyn_states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates    = dyn_states;

    // 4. Empty pipeline layout (no descriptor sets, no push constants yet —
    //    they come with the encoder/render-pass work in 1.6.1e).
    VkPipelineLayout layout = VK_NULL_HANDLE;
    {
        VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        if (vkCreatePipelineLayout(c.device, &plci, nullptr, &layout) != VK_SUCCESS) {
            log("vkpipe_create: vkCreatePipelineLayout failed (%s)", label_s.c_str());
            vkDestroyShaderModule(c.device, vs, nullptr);
            vkDestroyShaderModule(c.device, fs, nullptr);
            g_build_failures.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }
    }

    // 5. Dynamic-rendering attachment formats.
    VkFormat color_fmt = translate_color_format(spec.color_format_code);
    VkFormat depth_fmt = VK_FORMAT_D32_SFLOAT;
    VkPipelineRenderingCreateInfo rendering_info{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering_info.colorAttachmentCount    = 1;
    rendering_info.pColorAttachmentFormats = &color_fmt;
    rendering_info.depthAttachmentFormat   = spec.depth_attachment ? depth_fmt : VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pci.pNext               = &rendering_info;
    pci.stageCount          = 2;
    pci.pStages             = stages;
    pci.pVertexInputState   = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState      = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState   = &ms;
    pci.pDepthStencilState  = &ds;
    pci.pColorBlendState    = &cb;
    pci.pDynamicState       = &dyn;
    pci.layout              = layout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult r = vkCreateGraphicsPipelines(c.device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline);
    vkDestroyShaderModule(c.device, vs, nullptr);
    vkDestroyShaderModule(c.device, fs, nullptr);

    if (r != VK_SUCCESS) {
        log("vkpipe_create: vkCreateGraphicsPipelines failed (%d) %s", r, label_s.c_str());
        vkDestroyPipelineLayout(c.device, layout, nullptr);
        g_build_failures.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }

    uint64_t h = alloc_handle();
    {
        std::lock_guard<std::mutex> g(g_mutex);
        g_pipelines[h] = {pipeline, layout};
    }
    g_total_built.fetch_add(1, std::memory_order_relaxed);
    log("created pipeline h=0x%llx (%s): %u attrs, stride=%u, topo=%u",
        (unsigned long long)h, label_s.c_str(), (uint32_t)spec.attrs.size(),
        spec.vertex_stride, spec.topology);
    return h;
}

void vkpipe_destroy(uint64_t handle) {
    PipelineRec r{};
    {
        std::lock_guard<std::mutex> g(g_mutex);
        auto it = g_pipelines.find(handle);
        if (it == g_pipelines.end()) return;
        r = it->second;
        g_pipelines.erase(it);
    }
    auto& c = ctx();
    if (r.pipeline) vkDestroyPipeline(c.device, r.pipeline, nullptr);
    if (r.layout)   vkDestroyPipelineLayout(c.device, r.layout, nullptr);
}

VkPipelineRef vkpipe_ref(uint64_t handle) {
    std::lock_guard<std::mutex> g(g_mutex);
    auto it = g_pipelines.find(handle);
    if (it == g_pipelines.end()) return {nullptr, nullptr};
    return {(void*)it->second.pipeline, (void*)it->second.layout};
}

VkPipelineStats vkpipe_stats() {
    std::lock_guard<std::mutex> g(g_mutex);
    return {
        (uint32_t)g_pipelines.size(),
        g_total_built.load(std::memory_order_relaxed),
        g_build_failures.load(std::memory_order_relaxed),
    };
}

} // namespace rtxmc
