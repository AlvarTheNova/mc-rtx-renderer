#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace rtxmc {

// Owns instance / device / swapchain. Single global because MC has one window.
struct VulkanContext {
    VkInstance       instance       = VK_NULL_HANDLE;
    VkPhysicalDevice phys           = VK_NULL_HANDLE;
    char             phys_name[256] = {};
    VkDevice         device         = VK_NULL_HANDLE;
    VkQueue          gfx_queue      = VK_NULL_HANDLE;
    uint32_t         gfx_family     = 0;
    VkSurfaceKHR     surface        = VK_NULL_HANDLE;
    VkSwapchainKHR   swapchain      = VK_NULL_HANDLE;
    VkFormat         swap_format    = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D       swap_extent    = {0, 0};
    std::vector<VkImage>     swap_images;
    std::vector<VkImageView> swap_views;

    // RT pipeline properties (filled after device creation)
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rt_props{};

    // Extension function pointers — KHR/EXT entry points aren't exported
    // from vulkan-1.lib, must be loaded via vkGetDeviceProcAddr.
    struct ExtFns {
        PFN_vkCreateAccelerationStructureKHR        vkCreateAccelerationStructureKHR        = nullptr;
        PFN_vkDestroyAccelerationStructureKHR       vkDestroyAccelerationStructureKHR       = nullptr;
        PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = nullptr;
        PFN_vkCmdBuildAccelerationStructuresKHR     vkCmdBuildAccelerationStructuresKHR     = nullptr;
        PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = nullptr;
        PFN_vkCreateRayTracingPipelinesKHR          vkCreateRayTracingPipelinesKHR          = nullptr;
        PFN_vkCmdTraceRaysKHR                       vkCmdTraceRaysKHR                       = nullptr;
        PFN_vkGetRayTracingShaderGroupHandlesKHR    vkGetRayTracingShaderGroupHandlesKHR    = nullptr;
    } ext;

    bool init(void* hwnd, int w, int h);
    void resize(int w, int h);
    void destroy();

private:
    void destroy_swapchain();
    bool create_swapchain_with_views(int w, int h);
};

VulkanContext& ctx();

} // namespace rtxmc
