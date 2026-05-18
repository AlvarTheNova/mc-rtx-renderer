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

    // Descriptor set layout: one combined image sampler (atlas) at set 0 binding 0.
    VkDescriptorSetLayoutBinding dslb{};
    dslb.binding         = 0;
    dslb.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    dslb.descriptorCount = 1;
    dslb.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dslci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dslci.bindingCount = 1;
    dslci.pBindings    = &dslb;
    if (vkCreateDescriptorSetLayout(c.device, &dslci, nullptr, &dsl_) != VK_SUCCESS) {
        log("vkCreateDescriptorSetLayout (chunk) failed");
        return false;
    }

    // Descriptor pool — one set total (atlas is shared across opaque/translucent).
    VkDescriptorPoolSize psz{};
    psz.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    psz.descriptorCount = 1;
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets       = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes    = &psz;
    if (vkCreateDescriptorPool(c.device, &dpci, nullptr, &dpool_) != VK_SUCCESS) {
        log("vkCreateDescriptorPool (chunk) failed");
        return false;
    }

    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool     = dpool_;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts        = &dsl_;
    if (vkAllocateDescriptorSets(c.device, &dsai, &dset_) != VK_SUCCESS) {
        log("vkAllocateDescriptorSets (chunk) failed");
        return false;
    }

    // Push constants: view (64) + proj (64) + ivec4 sectionPos (16) = 144 B.
    VkPushConstantRange pc_range{};
    pc_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pc_range.offset     = 0;
    pc_range.size       = sizeof(float) * 16 * 2 + sizeof(int32_t) * 4;

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount         = 1;
    plci.pSetLayouts            = &dsl_;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pc_range;
    if (vkCreatePipelineLayout(c.device, &plci, nullptr, &layout_) != VK_SUCCESS) {
        log("vkCreatePipelineLayout (chunk) failed");
        return false;
    }

    if (!build_pipeline(pipeline_opaque_,     color_format, depth_format, /*translucent*/ false)) return false;
    if (!build_pipeline(pipeline_translucent_, color_format, depth_format, /*translucent*/ true))  return false;

    log("chunk pipelines created (opaque + translucent, color=%d depth=%d)",
        (int)color_format, (int)depth_format);
    return true;
}

bool ChunkRenderer::build_pipeline(VkPipeline& out, VkFormat color_format, VkFormat depth_format,
                                   bool translucent) {
    auto& c = ctx();

    VkShaderModule vs = make_module(c.device, spirv_chunk_vert, sizeof(spirv_chunk_vert));
    VkShaderModule fs = make_module(c.device, spirv_chunk_frag, sizeof(spirv_chunk_frag));
    if (!vs || !fs) return false;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName  = "main";

    // Vertex format mirrors MC's 32 B SOLID-layer format (same for all layers).
    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = 32;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[5]{};
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT,  0};   // position
    attrs[1] = {1, 0, VK_FORMAT_R8G8B8A8_UINT,    12};   // color
    attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT,    16};   // uv0
    attrs[3] = {3, 0, VK_FORMAT_R16G16_UINT,      24};   // uv2 (light)
    attrs[4] = {4, 0, VK_FORMAT_R8G8B8A8_UINT,    28};   // normal + pad

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &binding;
    vi.vertexAttributeDescriptionCount = 5;
    vi.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth: opaque writes + tests depth; translucent only tests (so it
    // blends correctly with whatever opaque drew below).
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = translucent ? VK_FALSE : VK_TRUE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS;
    ds.minDepthBounds   = 0.0f;
    ds.maxDepthBounds   = 1.0f;

    // Blending: opaque = none; translucent = standard alpha (src.a, 1-src.a).
    VkPipelineColorBlendAttachmentState cb_att{};
    cb_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cb_att.blendEnable         = translucent ? VK_TRUE : VK_FALSE;
    cb_att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cb_att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cb_att.colorBlendOp        = VK_BLEND_OP_ADD;
    cb_att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cb_att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    cb_att.alphaBlendOp        = VK_BLEND_OP_ADD;

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

    VkResult r = vkCreateGraphicsPipelines(c.device, VK_NULL_HANDLE, 1, &pci, nullptr, &out);
    vkDestroyShaderModule(c.device, vs, nullptr);
    vkDestroyShaderModule(c.device, fs, nullptr);
    if (r != VK_SUCCESS) {
        log("vkCreateGraphicsPipelines (chunk %s) failed (%d)",
            translucent ? "translucent" : "opaque", r);
        return false;
    }
    return true;
}

void ChunkRenderer::bind_atlas(VkImageView view, VkSampler sampler) {
    if (!view || !sampler) { atlas_bound_ = false; return; }
    auto& c = ctx();
    VkDescriptorImageInfo dii{};
    dii.sampler     = sampler;
    dii.imageView   = view;
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet          = dset_;
    w.dstBinding      = 0;
    w.descriptorCount = 1;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo      = &dii;
    vkUpdateDescriptorSets(c.device, 1, &w, 0, nullptr);
    atlas_bound_ = true;
}

void ChunkRenderer::bind_descriptor_set(VkCommandBuffer cmd) {
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_,
                            0, 1, &dset_, 0, nullptr);
}

void ChunkRenderer::bind_pipeline(VkCommandBuffer cmd, bool translucent) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      translucent ? pipeline_translucent_ : pipeline_opaque_);
}

void ChunkRenderer::record(VkCommandBuffer cmd,
                           const float view[16],
                           const float proj[16],
                           int section_x, int section_y, int section_z,
                           VkBuffer vertex_buffer,
                           uint32_t vertex_count) {
    struct {
        float view[16];
        float proj[16];
        int32_t sectionPos[4];
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

    const uint32_t quad_count  = vertex_count / 4;
    const uint32_t index_count = quad_count * 6;
    vkCmdDrawIndexed(cmd, index_count, 1, 0, 0, 0);
}

void ChunkRenderer::destroy() {
    auto& c = ctx();
    if (pipeline_opaque_)      { vkDestroyPipeline(c.device, pipeline_opaque_, nullptr);      pipeline_opaque_ = VK_NULL_HANDLE; }
    if (pipeline_translucent_) { vkDestroyPipeline(c.device, pipeline_translucent_, nullptr); pipeline_translucent_ = VK_NULL_HANDLE; }
    if (layout_)               { vkDestroyPipelineLayout(c.device, layout_, nullptr);         layout_ = VK_NULL_HANDLE; }
    if (dpool_)                { vkDestroyDescriptorPool(c.device, dpool_, nullptr);          dpool_ = VK_NULL_HANDLE; }
    if (dsl_)                  { vkDestroyDescriptorSetLayout(c.device, dsl_, nullptr);       dsl_ = VK_NULL_HANDLE; }
    dset_ = VK_NULL_HANDLE;
    atlas_bound_ = false;
}

} // namespace rtxmc
