#pragma once

#include <vulkan/vulkan.h>

namespace rtxmc {

// Phase 1.3 sanity test: a single triangle at world (0, 100, 0). Proves the
// VK graphics pipeline path, push-constant flow for view/proj matrices,
// and dynamic-rendering integration. Goes away in Phase 1.4 when the chunk
// rasterizer takes over.
class Triangle {
public:
    bool init(VkFormat color_format);
    void destroy();

    // Record draw commands into `cmd`. Caller is responsible for layout
    // transitions and vkCmdBeginRendering / vkCmdEndRendering.
    // `view` and `proj` are column-major mat4 in float[16] form.
    void record(VkCommandBuffer cmd,
                const float view[16],
                const float proj[16],
                VkExtent2D viewport_extent);

private:
    VkPipelineLayout layout_   = VK_NULL_HANDLE;
    VkPipeline       pipeline_ = VK_NULL_HANDLE;
};

} // namespace rtxmc
