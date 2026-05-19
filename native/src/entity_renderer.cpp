#include "entity_renderer.h"
#include "vulkan_context.h"
#include "voxel_bvh.h"

#include "entity_vert.h"
#include "entity_frag.h"
#include "glint_vert.h"
#include "glint_frag.h"
#include "lines_vert.h"
#include "lines_frag.h"
#include "debug_box_vert.h"
#include "debug_box_frag.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace rtxmc {

namespace {

constexpr uint32_t STRIDE_ENTITY    = 36;
constexpr uint32_t STRIDE_GLINT     = 20;
constexpr uint32_t STRIDE_LINES     = 24;
constexpr uint32_t STRIDE_DEBUG_BOX = 16;

const char* variant_name(EntityRenderer::Variant v) {
    switch (v) {
        case EntityRenderer::VAR_ENTITY:    return "entity";
        case EntityRenderer::VAR_GLINT:     return "glint";
        case EntityRenderer::VAR_LINES:     return "lines";
        case EntityRenderer::VAR_DEBUG_BOX: return "debug_box";
        default:                            return "?";
    }
}

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
        log("vkCreateShaderModule failed");
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

    // Shared push constants: viewRot (64) + proj (64) = 128 B.
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

    for (uint32_t v = 0; v < VAR_COUNT; ++v) {
        if (!build_pipeline((Variant)v, color_format, depth_format)) return false;
    }
    log("entity pipelines created: entity(36B) + glint(20B) + lines(24B) + debug_box(16B)");
    return true;
}

bool EntityRenderer::build_pipeline(Variant v, VkFormat color_format, VkFormat depth_format) {
    auto& c = ctx();

    const uint32_t* vs_code = nullptr; size_t vs_bytes = 0;
    const uint32_t* fs_code = nullptr; size_t fs_bytes = 0;
    uint32_t stride = 0;
    VkVertexInputAttributeDescription attrs[6]{};
    uint32_t attr_count = 0;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    bool depth_write = true;

    // Blend factors per variant.
    VkBlendFactor src_color = VK_BLEND_FACTOR_SRC_ALPHA;
    VkBlendFactor dst_color = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

    switch (v) {
    case VAR_ENTITY:
        vs_code = spirv_entity_vert; vs_bytes = sizeof(spirv_entity_vert);
        fs_code = spirv_entity_frag; fs_bytes = sizeof(spirv_entity_frag);
        stride = STRIDE_ENTITY;
        attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT,  0};
        attrs[1] = {1, 0, VK_FORMAT_R8G8B8A8_UINT,    12};
        attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT,    16};
        attrs[3] = {3, 0, VK_FORMAT_R16G16_UINT,      24};
        attrs[4] = {4, 0, VK_FORMAT_R16G16_UINT,      28};
        attrs[5] = {5, 0, VK_FORMAT_R8G8B8A8_UINT,    32};
        attr_count = 6;
        break;
    case VAR_GLINT:
        vs_code = spirv_glint_vert; vs_bytes = sizeof(spirv_glint_vert);
        fs_code = spirv_glint_frag; fs_bytes = sizeof(spirv_glint_frag);
        stride = STRIDE_GLINT;
        attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT,  0};
        attrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT,    12};
        attr_count = 2;
        // Additive blend, no depth write — glint is an overlay on top of
        // already-drawn entity geometry.
        src_color = VK_BLEND_FACTOR_ONE;
        dst_color = VK_BLEND_FACTOR_ONE;
        depth_write = false;
        break;
    case VAR_LINES:
        vs_code = spirv_lines_vert; vs_bytes = sizeof(spirv_lines_vert);
        fs_code = spirv_lines_frag; fs_bytes = sizeof(spirv_lines_frag);
        stride = STRIDE_LINES;
        attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT,  0};
        attrs[1] = {1, 0, VK_FORMAT_R8G8B8A8_UINT,    12};
        attrs[2] = {2, 0, VK_FORMAT_R8G8B8A8_UINT,    16};   // normal (unused)
        attrs[3] = {3, 0, VK_FORMAT_R32_SFLOAT,       20};   // line_width (unused; VK uses vkCmdSetLineWidth)
        attr_count = 4;
        topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        break;
    case VAR_DEBUG_BOX:
        vs_code = spirv_debug_box_vert; vs_bytes = sizeof(spirv_debug_box_vert);
        fs_code = spirv_debug_box_frag; fs_bytes = sizeof(spirv_debug_box_frag);
        stride = STRIDE_DEBUG_BOX;
        attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT,  0};
        attrs[1] = {1, 0, VK_FORMAT_R8G8B8A8_UINT,    12};
        attr_count = 2;
        break;
    default: return false;
    }

    VkShaderModule vs = make_module(c.device, vs_code, vs_bytes);
    VkShaderModule fs = make_module(c.device, fs_code, fs_bytes);
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

    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = stride;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &binding;
    vi.vertexAttributeDescriptionCount = attr_count;
    vi.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = topology;

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

    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = depth_write ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS;
    ds.minDepthBounds   = 0.0f;
    ds.maxDepthBounds   = 1.0f;

    VkPipelineColorBlendAttachmentState cb_att{};
    cb_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cb_att.blendEnable         = VK_TRUE;
    cb_att.srcColorBlendFactor = src_color;
    cb_att.dstColorBlendFactor = dst_color;
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

    VkResult r = vkCreateGraphicsPipelines(c.device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipelines_[v]);
    vkDestroyShaderModule(c.device, vs, nullptr);
    vkDestroyShaderModule(c.device, fs, nullptr);
    if (r != VK_SUCCESS) {
        log("vkCreateGraphicsPipelines (%s) failed (%d)", variant_name(v), r);
        return false;
    }
    return true;
}

bool EntityRenderer::create_buffer_and_copy(Batch& out, const void* verts, uint32_t bytes) {
    auto& c = ctx();

    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size        = bytes;
    bci.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(c.device, &bci, nullptr, &out.buffer) != VK_SUCCESS) {
        log("entity upload: vkCreateBuffer failed (%u B)", bytes);
        return false;
    }

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(c.device, out.buffer, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = find_memory_type_e(mr.memoryTypeBits,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(c.device, &ai, nullptr, &out.memory) != VK_SUCCESS) {
        log("entity upload: memory alloc failed");
        vkDestroyBuffer(c.device, out.buffer, nullptr);
        out.buffer = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(c.device, out.buffer, out.memory, 0);

    void* mapped = nullptr;
    if (vkMapMemory(c.device, out.memory, 0, bytes, 0, &mapped) != VK_SUCCESS) {
        log("entity upload: vkMapMemory failed");
        vkDestroyBuffer(c.device, out.buffer, nullptr);
        vkFreeMemory(c.device, out.memory, nullptr);
        out.buffer = VK_NULL_HANDLE;
        out.memory = VK_NULL_HANDLE;
        return false;
    }
    std::memcpy(mapped, verts, bytes);
    vkUnmapMemory(c.device, out.memory);
    return true;
}

void EntityRenderer::upload_batch(int layer_hash, const void* verts,
                                  uint32_t vertex_count, uint32_t vertex_bytes) {
    (void)layer_hash;
    if (!verts || vertex_count == 0 || vertex_bytes == 0) return;

    const uint32_t stride = vertex_bytes / vertex_count;
    if (stride * vertex_count != vertex_bytes) {
        if (log_budget_-- > 0) {
            log("entity batch skipped (non-integer stride): verts=%u bytes=%u",
                vertex_count, vertex_bytes);
        }
        return;
    }

    Variant v;
    switch (stride) {
        case STRIDE_ENTITY:    v = VAR_ENTITY;    break;
        case STRIDE_GLINT:     v = VAR_GLINT;     break;
        case STRIDE_LINES:     v = VAR_LINES;     break;
        case STRIDE_DEBUG_BOX: v = VAR_DEBUG_BOX; break;
        default:
            if (log_budget_-- > 0) {
                log("entity batch dropped (unknown stride %u): verts=%u bytes=%u",
                    stride, vertex_count, vertex_bytes);
            }
            return;
    }

    Batch b{};
    b.vertex_count = vertex_count;
    if (!create_buffer_and_copy(b, verts, vertex_bytes)) return;
    batches_[v].push_back(b);
}

void EntityRenderer::draw_variant(VkCommandBuffer cmd, Variant v) {
    auto& list = batches_[v];
    if (list.empty()) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines_[v]);

    // LINE_LIST is a vertex-list topology, not quads — no index buffer.
    // Other variants are TRIANGLE_LIST sourced from QUADS (4-vert blocks),
    // so we use the shared {0,1,2, 2,3,0} index buffer.
    const bool is_lines = (v == VAR_LINES);
    if (!is_lines) {
        vkCmdBindIndexBuffer(cmd, bvh().shared_quad_index_buffer(), 0, VK_INDEX_TYPE_UINT32);
    }
    const uint32_t max_indices = bvh().shared_quad_index_capacity_indices();

    uint32_t drawn = 0, skipped = 0;
    for (auto& b : list) {
        if (is_lines) {
            // Direct vertex draw — line list.
            { VkDeviceSize offset = 0; vkCmdBindVertexBuffers(cmd, 0, 1, &b.buffer, &offset); }
            vkCmdDraw(cmd, b.vertex_count, 1, 0, 0);
            ++drawn;
        } else {
            const uint32_t needed = (b.vertex_count / 4) * 6;
            if (needed > max_indices) {
                bvh().queue_buffer_delete(b.buffer, b.memory);
                ++skipped;
                continue;
            }
            { VkDeviceSize offset = 0; vkCmdBindVertexBuffers(cmd, 0, 1, &b.buffer, &offset); }
            vkCmdDrawIndexed(cmd, needed, 1, 0, 0, 0);
            ++drawn;
        }
        bvh().queue_buffer_delete(b.buffer, b.memory);
    }
    list.clear();

    if (log_budget_-- > 0 && (drawn > 0 || skipped > 0)) {
        log("entity draw %s: %u batches, skipped %u", variant_name(v), drawn, skipped);
    }
}

void EntityRenderer::record_and_consume(VkCommandBuffer cmd,
                                        const float view_rot[16],
                                        const float proj[16]) {
    const bool any = !batches_[VAR_ENTITY].empty()    ||
                     !batches_[VAR_GLINT].empty()     ||
                     !batches_[VAR_LINES].empty()     ||
                     !batches_[VAR_DEBUG_BOX].empty();
    if (!any) return;

    if (atlas_dset_ == VK_NULL_HANDLE) {
        // No atlas yet → drop everything accumulated; pipelines that don't
        // sample (lines, debug_box) still expect a bound dset because they
        // share the layout. Simpler to just defer drawing one more frame.
        for (uint32_t v = 0; v < VAR_COUNT; ++v) {
            for (auto& b : batches_[v]) bvh().queue_buffer_delete(b.buffer, b.memory);
            batches_[v].clear();
        }
        return;
    }

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_,
                            0, 1, &atlas_dset_, 0, nullptr);

    struct {
        float viewRot[16];
        float proj[16];
    } pc;
    std::memcpy(pc.viewRot, view_rot, sizeof(pc.viewRot));
    std::memcpy(pc.proj,    proj,     sizeof(pc.proj));
    vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

    // Order: solid-ish first, debug boxes on top, lines on top of those,
    // glint last (additive overlay).
    draw_variant(cmd, VAR_ENTITY);
    draw_variant(cmd, VAR_DEBUG_BOX);
    draw_variant(cmd, VAR_LINES);
    draw_variant(cmd, VAR_GLINT);
}

void EntityRenderer::destroy() {
    auto& c = ctx();
    for (uint32_t v = 0; v < VAR_COUNT; ++v) {
        for (auto& b : batches_[v]) {
            if (b.buffer) vkDestroyBuffer(c.device, b.buffer, nullptr);
            if (b.memory) vkFreeMemory(c.device, b.memory, nullptr);
        }
        batches_[v].clear();
        if (pipelines_[v]) {
            vkDestroyPipeline(c.device, pipelines_[v], nullptr);
            pipelines_[v] = VK_NULL_HANDLE;
        }
    }
    if (layout_) { vkDestroyPipelineLayout(c.device, layout_, nullptr); layout_ = VK_NULL_HANDLE; }
    atlas_dset_ = VK_NULL_HANDLE;
}

} // namespace rtxmc
