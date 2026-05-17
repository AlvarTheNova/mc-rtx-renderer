#include "vulkan_context.h"

#include <cstdio>
#include <vector>

#ifdef VK_USE_PLATFORM_WIN32_KHR
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #define GLFW_EXPOSE_NATIVE_WIN32
  // Note: we don't depend on GLFW here; the Java side already passed us the HWND.
#endif

namespace rtxmc {

namespace {
VulkanContext g_ctx;

const char* kInstanceExts[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef VK_USE_PLATFORM_WIN32_KHR
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
    VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
};

const char* kDeviceExts[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
    VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
    VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
    VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
    VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
    VK_KHR_SPIRV_1_4_EXTENSION_NAME,
};

bool create_instance(VulkanContext& c) {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName   = "mc-rtx-renderer";
    app.applicationVersion = VK_MAKE_API_VERSION(0, 0, 0, 1);
    app.pEngineName        = "rtxmc";
    app.apiVersion         = VK_API_VERSION_1_3;

    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo        = &app;
    ci.enabledExtensionCount   = sizeof(kInstanceExts)/sizeof(kInstanceExts[0]);
    ci.ppEnabledExtensionNames = kInstanceExts;

    return vkCreateInstance(&ci, nullptr, &c.instance) == VK_SUCCESS;
}

bool create_surface(VulkanContext& c, void* hwnd_void) {
#ifdef VK_USE_PLATFORM_WIN32_KHR
    HWND hwnd = (HWND)hwnd_void;
    VkWin32SurfaceCreateInfoKHR si{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    si.hinstance = GetModuleHandle(nullptr);
    si.hwnd      = hwnd;
    return vkCreateWin32SurfaceKHR(c.instance, &si, nullptr, &c.surface) == VK_SUCCESS;
#else
    (void)c; (void)hwnd_void;
    return false; // non-Windows path not implemented
#endif
}

bool pick_physical_device(VulkanContext& c) {
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(c.instance, &n, nullptr);
    if (!n) return false;
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(c.instance, &n, devs.data());

    // Prefer discrete with RT support. Picking the first discrete that has
    // VK_KHR_ray_tracing_pipeline available is sufficient for our use.
    for (VkPhysicalDevice d : devs) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(d, &props);
        if (props.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) continue;

        uint32_t en = 0;
        vkEnumerateDeviceExtensionProperties(d, nullptr, &en, nullptr);
        std::vector<VkExtensionProperties> exts(en);
        vkEnumerateDeviceExtensionProperties(d, nullptr, &en, exts.data());

        bool has_rt = false;
        for (auto& e : exts) {
            if (!std::strcmp(e.extensionName, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)) {
                has_rt = true; break;
            }
        }
        if (has_rt) { c.phys = d; return true; }
    }
    return false;
}

bool create_device(VulkanContext& c) {
    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(c.phys, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qs(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(c.phys, &qn, qs.data());
    for (uint32_t i = 0; i < qn; ++i) {
        if (qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { c.gfx_family = i; break; }
    }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = c.gfx_family;
    qi.queueCount       = 1;
    qi.pQueuePriorities = &prio;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR asf{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    asf.accelerationStructure = VK_TRUE;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtf{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    rtf.rayTracingPipeline = VK_TRUE;
    rtf.pNext = &asf;

    VkPhysicalDeviceBufferDeviceAddressFeatures bdaf{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
    bdaf.bufferDeviceAddress = VK_TRUE;
    bdaf.pNext = &rtf;

    VkPhysicalDeviceDescriptorIndexingFeatures dif{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
    dif.runtimeDescriptorArray = VK_TRUE;
    dif.descriptorBindingPartiallyBound = VK_TRUE;
    dif.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    dif.pNext = &bdaf;

    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &dif;

    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.pNext                   = &f2;
    di.queueCreateInfoCount    = 1;
    di.pQueueCreateInfos       = &qi;
    di.enabledExtensionCount   = sizeof(kDeviceExts)/sizeof(kDeviceExts[0]);
    di.ppEnabledExtensionNames = kDeviceExts;

    if (vkCreateDevice(c.phys, &di, nullptr, &c.device) != VK_SUCCESS) return false;
    vkGetDeviceQueue(c.device, c.gfx_family, 0, &c.gfx_queue);

    // Cache RT pipeline properties — needed for shader binding table sizing.
    VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    c.rt_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
    p2.pNext = &c.rt_props;
    vkGetPhysicalDeviceProperties2(c.phys, &p2);
    return true;
}

bool create_swapchain(VulkanContext& c, int w, int h) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(c.phys, c.surface, &caps);
    c.swap_extent = {(uint32_t)w, (uint32_t)h};

    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface          = c.surface;
    sci.minImageCount    = std::max(2u, caps.minImageCount);
    sci.imageFormat      = c.swap_format;
    sci.imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sci.imageExtent      = c.swap_extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform     = caps.currentTransform;
    sci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode      = VK_PRESENT_MODE_MAILBOX_KHR; // tear-free with low latency
    sci.clipped          = VK_TRUE;

    return vkCreateSwapchainKHR(c.device, &sci, nullptr, &c.swapchain) == VK_SUCCESS;
}
} // namespace

VulkanContext& ctx() { return g_ctx; }

bool VulkanContext::init(void* hwnd, int w, int h) {
    if (!create_instance(*this))                { std::fprintf(stderr, "rtxmc: vkCreateInstance failed\n"); return false; }
    if (!create_surface(*this, hwnd))           { std::fprintf(stderr, "rtxmc: surface creation failed\n"); return false; }
    if (!pick_physical_device(*this))           { std::fprintf(stderr, "rtxmc: no RT-capable discrete GPU\n"); return false; }
    if (!create_device(*this))                  { std::fprintf(stderr, "rtxmc: vkCreateDevice failed\n"); return false; }
    if (!create_swapchain(*this, w, h))         { std::fprintf(stderr, "rtxmc: swapchain creation failed\n"); return false; }
    return true;
}

void VulkanContext::resize(int w, int h) {
    if (device) vkDeviceWaitIdle(device);
    if (swapchain) { vkDestroySwapchainKHR(device, swapchain, nullptr); swapchain = VK_NULL_HANDLE; }
    create_swapchain(*this, w, h);
}

void VulkanContext::destroy() {
    if (device) vkDeviceWaitIdle(device);
    if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
    if (device)    vkDestroyDevice(device, nullptr);
    if (surface)   vkDestroySurfaceKHR(instance, surface, nullptr);
    if (instance)  vkDestroyInstance(instance, nullptr);
    *this = {};
}

} // namespace rtxmc
