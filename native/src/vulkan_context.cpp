#include "vulkan_context.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>

#ifdef VK_USE_PLATFORM_WIN32_KHR
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
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

void log(const char* fmt, ...) {
    std::fprintf(stderr, "[rtxmc native] ");
    va_list ap; va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

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

    VkResult r = vkCreateInstance(&ci, nullptr, &c.instance);
    if (r != VK_SUCCESS) { log("vkCreateInstance failed (%d)", r); return false; }
    log("instance created (VK 1.3, %u extensions)", ci.enabledExtensionCount);
    return true;
}

bool create_surface(VulkanContext& c, void* hwnd_void) {
#ifdef VK_USE_PLATFORM_WIN32_KHR
    HWND hwnd = (HWND)hwnd_void;
    VkWin32SurfaceCreateInfoKHR si{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    si.hinstance = GetModuleHandle(nullptr);
    si.hwnd      = hwnd;
    VkResult r = vkCreateWin32SurfaceKHR(c.instance, &si, nullptr, &c.surface);
    if (r != VK_SUCCESS) { log("vkCreateWin32SurfaceKHR failed (%d)", r); return false; }
    log("Win32 surface created (hwnd=%p)", (void*)hwnd);
    return true;
#else
    (void)c; (void)hwnd_void;
    log("non-Windows platform: surface creation not implemented");
    return false;
#endif
}

bool pick_physical_device(VulkanContext& c) {
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(c.instance, &n, nullptr);
    if (!n) { log("no Vulkan devices enumerated"); return false; }
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(c.instance, &n, devs.data());

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
        if (has_rt) {
            c.phys = d;
            std::snprintf(c.phys_name, sizeof(c.phys_name), "%s", props.deviceName);
            log("picked physical device: %s (driver %u.%u.%u, API %u.%u)",
                props.deviceName,
                VK_API_VERSION_MAJOR(props.driverVersion),
                VK_API_VERSION_MINOR(props.driverVersion),
                VK_API_VERSION_PATCH(props.driverVersion),
                VK_API_VERSION_MAJOR(props.apiVersion),
                VK_API_VERSION_MINOR(props.apiVersion));
            return true;
        }
    }
    log("no discrete GPU with VK_KHR_ray_tracing_pipeline found");
    return false;
}

bool create_device(VulkanContext& c) {
    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(c.phys, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qs(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(c.phys, &qn, qs.data());
    bool found_q = false;
    for (uint32_t i = 0; i < qn; ++i) {
        if (qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { c.gfx_family = i; found_q = true; break; }
    }
    if (!found_q) { log("no graphics queue family"); return false; }

    // Verify present support on the surface from that queue family.
    VkBool32 present_supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(c.phys, c.gfx_family, c.surface, &present_supported);
    if (!present_supported) {
        log("graphics queue family %u cannot present to surface — needs split-queue path (TODO)", c.gfx_family);
        return false;
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

    // VK 1.3 dynamic rendering — lets us skip render passes / framebuffers.
    // Synchronization2 is the modern barrier API we already use.
    VkPhysicalDeviceVulkan13Features v13{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    v13.dynamicRendering = VK_TRUE;
    v13.synchronization2 = VK_TRUE;
    v13.pNext = &dif;

    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &v13;

    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.pNext                   = &f2;
    di.queueCreateInfoCount    = 1;
    di.pQueueCreateInfos       = &qi;
    di.enabledExtensionCount   = sizeof(kDeviceExts)/sizeof(kDeviceExts[0]);
    di.ppEnabledExtensionNames = kDeviceExts;

    VkResult r = vkCreateDevice(c.phys, &di, nullptr, &c.device);
    if (r != VK_SUCCESS) { log("vkCreateDevice failed (%d)", r); return false; }
    vkGetDeviceQueue(c.device, c.gfx_family, 0, &c.gfx_queue);

    VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    c.rt_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
    p2.pNext = &c.rt_props;
    vkGetPhysicalDeviceProperties2(c.phys, &p2);

    // Load extension entry points
    #define LOAD(name) c.ext.name = (PFN_##name)vkGetDeviceProcAddr(c.device, #name); \
        if (!c.ext.name) log("warning: failed to load " #name)
    LOAD(vkCreateAccelerationStructureKHR);
    LOAD(vkDestroyAccelerationStructureKHR);
    LOAD(vkGetAccelerationStructureBuildSizesKHR);
    LOAD(vkCmdBuildAccelerationStructuresKHR);
    LOAD(vkGetAccelerationStructureDeviceAddressKHR);
    LOAD(vkCreateRayTracingPipelinesKHR);
    LOAD(vkCmdTraceRaysKHR);
    LOAD(vkGetRayTracingShaderGroupHandlesKHR);
    #undef LOAD

    log("device created, graphics queue family %u, RT max recursion depth %u",
        c.gfx_family, c.rt_props.maxRayRecursionDepth);
    return true;
}
} // namespace

VulkanContext& ctx() { return g_ctx; }

bool VulkanContext::create_swapchain_with_views(int w, int h) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps);

    // Clamp requested extent to surface capabilities. currentExtent of (UINT32_MAX,
    // UINT32_MAX) means "you choose."
    if (caps.currentExtent.width != UINT32_MAX) {
        swap_extent = caps.currentExtent;
    } else {
        swap_extent.width  = std::clamp((uint32_t)w, caps.minImageExtent.width,  caps.maxImageExtent.width);
        swap_extent.height = std::clamp((uint32_t)h, caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    // Prefer BGRA8_SRGB if available, else first format.
    uint32_t fn = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fn, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fn);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fn, fmts.data());
    VkSurfaceFormatKHR chosen = fmts[0];
    for (auto& f : fmts) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f; break;
        }
    }
    swap_format = chosen.format;

    // Prefer MAILBOX (low-latency tear-free); fall back to FIFO (always supported).
    uint32_t pn = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &pn, nullptr);
    std::vector<VkPresentModeKHR> pms(pn);
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &pn, pms.data());
    VkPresentModeKHR pmode = VK_PRESENT_MODE_FIFO_KHR;
    for (auto p : pms) if (p == VK_PRESENT_MODE_MAILBOX_KHR) { pmode = p; break; }

    uint32_t min_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && min_count > caps.maxImageCount) min_count = caps.maxImageCount;

    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface          = surface;
    sci.minImageCount    = min_count;
    sci.imageFormat      = chosen.format;
    sci.imageColorSpace  = chosen.colorSpace;
    sci.imageExtent      = swap_extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT;     // for vkCmdClearColorImage
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform     = caps.currentTransform;
    sci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode      = pmode;
    sci.clipped          = VK_TRUE;

    VkResult r = vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain);
    if (r != VK_SUCCESS) { log("vkCreateSwapchainKHR failed (%d)", r); return false; }

    uint32_t n = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &n, nullptr);
    swap_images.resize(n);
    vkGetSwapchainImagesKHR(device, swapchain, &n, swap_images.data());

    swap_views.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image    = swap_images[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format   = swap_format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(device, &vi, nullptr, &swap_views[i]);
    }

    log("swapchain created: %ux%u, %u images, format=%d, present=%s",
        swap_extent.width, swap_extent.height, n, (int)swap_format,
        pmode == VK_PRESENT_MODE_MAILBOX_KHR ? "MAILBOX" : "FIFO");
    return true;
}

void VulkanContext::destroy_swapchain() {
    for (auto v : swap_views) if (v) vkDestroyImageView(device, v, nullptr);
    swap_views.clear();
    swap_images.clear();
    if (swapchain) { vkDestroySwapchainKHR(device, swapchain, nullptr); swapchain = VK_NULL_HANDLE; }
}

bool VulkanContext::init(void* hwnd, int w, int h) {
    if (!create_instance(*this))               return false;
    if (!create_surface(*this, hwnd))          return false;
    if (!pick_physical_device(*this))          return false;
    if (!create_device(*this))                 return false;
    if (!create_swapchain_with_views(w, h))    return false;
    return true;
}

void VulkanContext::resize(int w, int h) {
    if (device) vkDeviceWaitIdle(device);
    destroy_swapchain();
    create_swapchain_with_views(w, h);
}

void VulkanContext::destroy() {
    if (device) vkDeviceWaitIdle(device);
    destroy_swapchain();
    if (device)   vkDestroyDevice(device, nullptr);
    if (surface)  vkDestroySurfaceKHR(instance, surface, nullptr);
    if (instance) vkDestroyInstance(instance, nullptr);
    *this = {};
}

} // namespace rtxmc
