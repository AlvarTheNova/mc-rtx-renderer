#include "entity_renderer.h"
#include "vulkan_context.h"
#include "voxel_bvh.h"     // for bvh().queue_delete-like access (we piggyback)

#include "entity_vert.h"   // declares spirv_entity_vert[]
#include "entity_frag.h"   // declares spirv_entity_frag[]

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace rtxmc {

namespace {

constexpr uint32_t ENTITY_VERTEX_STRIDE = 36;

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
        log("vkCreateShaderModule (entity) failed");
    }
    return m;
}

uint32_t find_memory_type_e(uint32_t type_bits, VkMemoryPropertyFlags wanted) {
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

EntityRenderer g_entities;

} // namespace

EntityRenderer& entities() { return g_entities; }

bool EntityRenderer::init(VkFormat color_format, VkFormat depth_format,
                          VkDescriptorSetLayout shared_atlas_dsl) {
    auto& c = ctx();

    // Push constants: viewRot (64) + proj (64) = 128 B.
    VkPushConstantRange pc_range{};
    pc_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pc_range.offset     = 0;
    pc_range.size       = sizeof(float) * 16 * 2;

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount         = 1;
    plci.pSetLayouts            = &shared_atlas_dsl;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pc_range;
    if (vkCreatePipelineLayout(c.device, &plci, nullptr, &layout_) != VK_SUCCESS) {
        log("vkCreatePipelineLayout (entity) failed"); return false;
    }

    if (!build_pipeline(color_format, depth_format)) return false;
    log("entity pipeline created (36 B vertex format)");
    return true;
}

bool EntityRenderer::build_pipeline(VkFormat color_format, VkFormat depth_format) {
    auto& c = ctx();

    VkShaderModule vs = make_module(c.device, spirv_entity_vert, sizeof(spirv_entity_vert));
    VkShaderModule fs = make_module(c.device, spirv_entity_frag, sizeof(spirv_entity_frag));
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

    // 36 B vertex format — chunk layout + UV1 inserted at offset 24.
    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = ENTITY_VERTEX_STRIDE;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[6]{};
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT,  0};   // position
    attrs[1] = {1, 0, VK_FORMAT_R8G8B8A8_UINT,    12};   // color
    attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT,    16};   // uv0
    attrs[3] = {3, 0, VK_FORMAT_R16G16_UINT,      24};   // uv1 (overlay)
    attrs[4] = {4, 0, VK_FORMAT_R16G16_UINT,      28};   // uv2 (light)
    attrs[5] = {5, 0, VK_FORMAT_R8G8B8A8_UINT,    32};   // normal

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &binding;
    vi.vertexAttributeDescriptionCount = 6;
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

    // Entities: blend against whatever's behind (chunks already drawn), but
    // still write depth so other entities can self-occlude.
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS;
    ds.minDepthBounds   = 0.0f;
    ds.maxDepthBounds   = 1.0f;

    VkPipelineColorBlendAttachmentState cb_att{};
    cb_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cb_att.blendEnable         = VK_TRUE;
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

    VkResult r = vkCreateGraphicsPipelines(c.device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline_);
    vkDestroyShaderModule(c.device, vs, nullptr);
    vkDestroyShaderModule(c.device, fs, nullptr);
    if (r != VK_SUCCESS) {
        log("vkCreateGraphicsPipelines (entity) failed (%d)", r);
        return false;
    }
    return true;
}

void EntityRenderer::upload_batch(int layer_hash, const void* verts,
                                  uint32_t vertex_count, uint32_t vertex_bytes) {
    if (!verts || vertex_count == 0 || vertex_bytes == 0) return;

    // 1.5.2b filter: only 36 B format. Lines/glint/debug come in 1.5.2c.
    if (vertex_bytes != vertex_count * ENTITY_VERTEX_STRIDE) {
        if (log_budget_ > 0) {
            --log_budget_;
            log("entity batch SKIPPED (stride %u != 36): layer=0x%08x verts=%u bytes=%u",
                vertex_count ? vertex_bytes / vertex_count : 0,
                (uint32_t)layer_hash, vertex_count, vertex_bytes);
        }
        return;
    }

    auto& c = ctx();

    EntityBatch eb{};
    eb.layer_hash   = layer_hash;
    eb.vertex_count = vertex_count;

    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size        = vertex_bytes;
    bci.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(c.device, &bci, nullptr, &eb.buffer) != VK_SUCCESS) {
        log("entity upload_batch: vkCreateBuffer failed (%u B)", vertex_bytes);
        return;
    }

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(c.device, eb.buffer, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = find_memory_type_e(mr.memoryTypeBits,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(c.device, &ai, nullptr, &eb.memory) != VK_SUCCESS) {
        log("entity upload_batch: memory alloc failed");
        vkDestroyBuffer(c.device, eb.buffer, nullptr);
        return;
    }
    vkBindBufferMemory(c.device, eb.buffer, eb.memory, 0);

    void* mapped = nullptr;
    if (vkMapMemory(c.device, eb.memory, 0, vertex_bytes, 0, &mapped) != VK_SUCCESS) {
        log("entity upload_batch: vkMapMemory failed");
        vkDestroyBuffer(c.device, eb.buffer, nullptr);
        vkFreeMemory(c.device, eb.memory, nullptr);
        return;
    }
    std::memcpy(mapped, verts, vertex_bytes);
    vkUnmapMemory(c.device, eb.memory);

    current_batches_.push_back(eb);
}

void EntityRenderer::record_and_consume(VkCommandBuffer cmd,
                                        const float view_rot[16],
                                        const float proj[16]) {
    if (current_batches_.empty()) return;
    if (atlas_dset_ == VK_NULL_HANDLE) {
        // Drop accumulated batches if atlas isn't bound yet (renders nothing).
        for (auto& b : current_batches_) {
            bvh().queue_buffer_delete(b.buffer, b.memory);
        }
        current_batches_.clear();
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_,
                            0, 1, &atlas_dset_, 0, nullptr);

    struct {
        float viewRot[16];
        float proj[16];
    } pc;
    std::memcpy(pc.viewRot, view_rot, sizeof(pc.viewRot));
    std::memcpy(pc.proj,    proj,     sizeof(pc.proj));
    vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

    // Bind the same shared quad index buffer the chunk renderer set up.
    // Entities are also QUADS in MC, so the {0,1,2, 2,3,0} pattern works.
    vkCmdBindIndexBuffer(cmd, bvh().shared_quad_index_buffer(), 0,
                         VK_INDEX_TYPE_UINT32);

    const uint32_t max_indices = bvh().shared_quad_index_capacity_indices();

    uint32_t drawn_batches = 0, drawn_verts = 0;
    for (auto& b : current_batches_) {
        const uint32_t needed = (b.vertex_count / 4) * 6;
        if (needed > max_indices) {
            // Batch too big for our shared index buffer; queue for delete + skip.
            bvh().queue_buffer_delete(b.buffer, b.memory);
            continue;
        }

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &b.buffer, &offset);
        vkCmdDrawIndexed(cmd, needed, 1, 0, 0, 0);

        ++drawn_batches;
        drawn_verts += b.vertex_count;

        bvh().queue_buffer_delete(b.buffer, b.memory);
    }
    current_batches_.clear();

    if (log_budget_ > 0) {
        --log_budget_;
        log("entity draw: %u batches, %u verts", drawn_batches, drawn_verts);
    }
}

void EntityRenderer::destroy() {
    auto& c = ctx();
    // Free any unconsumed batches (no fence to wait on — destroy happens at
    // shutdown after vkDeviceWaitIdle).
    for (auto& b : current_batches_) {
        if (b.buffer) vkDestroyBuffer(c.device, b.buffer, nullptr);
        if (b.memory) vkFreeMemory(c.device, b.memory, nullptr);
    }
    current_batches_.clear();

    if (pipeline_) { vkDestroyPipeline(c.device, pipeline_, nullptr);          pipeline_ = VK_NULL_HANDLE; }
    if (layout_)   { vkDestroyPipelineLayout(c.device, layout_, nullptr);      layout_   = VK_NULL_HANDLE; }
    atlas_dset_ = VK_NULL_HANDLE;
}

} // namespace rtxmc
