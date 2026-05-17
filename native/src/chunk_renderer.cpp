#include "chunk_renderer.h"
#include "vulkan_context.h"

#include "chunk_vert.h"   // declares spirv_chunk_vert[]
#include "chunk_frag.h"   // declares spirv_chunk_frag[]

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace rtxmc {
namespace {

void log(const char* fmt, ...) {
    std::fprintf(stderr, "[rtxmc native] ");
    va_list ap; va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

VkShaderModule make_module(VkDevice dev, const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes;
    ci.pCode    = code;
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(dev, &ci, nullptr, &m) != VK_SUCCESS) {
        log("vkCreateShaderModule (chunk) failed");
    }
    return m;
}

} // namespace

bool ChunkRenderer::init(VkFormat color_format, VkFormat depth_format) {
    auto& c = ctx();

    VkShaderModule vs = make_module(c.device, spirv_chunk_vert, sizeof(spirv_chunk_vert));
    VkShaderModule fs = make_module(c.device, spirv_chunk_frag, sizeof(spirv_chunk_frag));
    if (!vs || !fs) return false;

    // Push constants: view (64) + proj (64) + ivec4 sectionPos (16) = 144 B.
    // Min guaranteed maxPushConstantsSize is 128; NVIDIA supports 256+.
    VkPushConstantRange pc_range{};
    pc_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pc_range.offset     = 0;
    pc_range.size       = sizeof(float) * 16 * 2 + sizeof(int32_t) * 4;

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pc_range;
    if (vkCreatePipelineLayout(c.device, &plci, nullptr, &layout_) != VK_SUCCESS) {
        log("vkCreatePipelineLayout (chunk) failed");
        vkDestroyShaderModule(c.device, vs, nullptr);
        vkDestroyShaderModule(c.device, fs, nullptr);
        return false;
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

    // Vertex format mirrors MC's exactly (see shaders/chunk.vert byte layout).
    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = 32;  // 12 + 4 + 8 + 4 + 4
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[5]{};
    // location 0 — position (vec3 f32)
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT,  0};
    // location 1 — color (uvec4 u8 RGBA, NOT normalised — shader divides by 255)
    attrs[1] = {1, 0, VK_FORMAT_R8G8B8A8_UINT,    12};
    // location 2 — uv0 (vec2 f32)
    attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT,    16};
    // location 3 — uv2 (uvec2 u16)
    attrs[3] = {3, 0, VK_FORMAT_R16G16_UINT,      24};
    // location 4 — normal (uvec4 u8 — shader reinterprets > 127 as negative)
    attrs[4] = {4, 0, VK_FORMAT_R8G8B8A8_UINT,    28};

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &binding;
    vi.vertexAttributeDescriptionCount = 5;
    vi.pVertexAttributeDescriptions    = attrs;

    // MC emits QUADS — 4 verts per quad. We honor the implicit quad→2-triangle
    // expansion by interpreting the vertex stream with PRIMITIVE_TOPOLOGY_
    // TRIANGLE_LIST and trusting MC's matching index buffer would have given
    // the right triangles. BUT — we ignored MC's index buffer in 1.4.1. For
    // now we use TRIANGLE_LIST and hope for the best; if the geometry looks
    // wrong, we need to either honor the indices or rebuild as triangles.
    //
    // TODO Phase 1.4.2.5: respect MC's index buffer to get correct quad
    // expansion (0,1,2 / 2,3,0). For now: most rectangles render OK as long
    // as 6 of every 4 verts produce a quad — they don't, so expect tearing
    // until we wire indices.
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;     // MC's winding doesn't always match VK conventions
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS;     // VK reversed-Z would be GREATER; we keep classic
    ds.minDepthBounds   = 0.0f;
    ds.maxDepthBounds   = 1.0f;

    VkPipelineColorBlendAttachmentState cb_att{};
    cb_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cb_att.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments    = &cb_att;

    VkDynamicState dyn_states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates    = dyn_states;

    VkPipelineRenderingCreateInfo rendering_info{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering_info.colorAttachmentCount    = 1;
    rendering_info.pColorAttachmentFormats = &color_format;
    rendering_info.depthAttachmentFormat   = depth_format;

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
    pci.layout              = layout_;

    VkResult r = vkCreateGraphicsPipelines(c.device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline_);
    vkDestroyShaderModule(c.device, vs, nullptr);
    vkDestroyShaderModule(c.device, fs, nullptr);
    if (r != VK_SUCCESS) {
        log("vkCreateGraphicsPipelines (chunk) failed (%d)", r);
        return false;
    }
    log("chunk pipeline created (color=%d depth=%d)", (int)color_format, (int)depth_format);
    return true;
}

void ChunkRenderer::record(VkCommandBuffer cmd,
                           const float view[16],
                           const float proj[16],
                           int section_x, int section_y, int section_z,
                           VkBuffer vertex_buffer,
                           uint32_t vertex_count) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    // Push constants — must match the layout in chunk.vert's PC block.
    struct {
        float view[16];
        float proj[16];
        int32_t sectionPos[4];   // xyz + pad
    } pc;
    std::memcpy(pc.view, view, sizeof(pc.view));
    std::memcpy(pc.proj, proj, sizeof(pc.proj));
    pc.sectionPos[0] = section_x;
    pc.sectionPos[1] = section_y;
    pc.sectionPos[2] = section_z;
    pc.sectionPos[3] = 0;
    vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &offset);
    vkCmdDraw(cmd, vertex_count, 1, 0, 0);
}

void ChunkRenderer::destroy() {
    auto& c = ctx();
    if (pipeline_) { vkDestroyPipeline(c.device, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    if (layout_)   { vkDestroyPipelineLayout(c.device, layout_, nullptr); layout_ = VK_NULL_HANDLE; }
}

} // namespace rtxmc
