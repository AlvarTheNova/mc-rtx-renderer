#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

namespace rtxmc {

// Owns instance / device / swapchain. Single global because MC has one window.
struct VulkanContext {
    VkInstance       instance       = VK_NULL_HANDLE;
    VkPhysicalDevice phys           = VK_NULL_HANDLE;
    VkDevice         device         = VK_NULL_HANDLE;
    VkQueue          gfx_queue      = VK_NULL_HANDLE;
    uint32_t         gfx_family     = 0;
    VkSurfaceKHR     surface        = VK_NULL_HANDLE;
    VkSwapchainKHR   swapchain      = VK_NULL_HANDLE;
    VkFormat         swap_format    = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D       swap_extent    = {0, 0};

    // RT pipeline properties (filled after device creation)
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rt_props{};

    bool init(void* hwnd, int w, int h);
    void resize(int w, int h);
    void destroy();
};

VulkanContext& ctx();

} // namespace rtxmc
