#include "vk_resources.h"
#include "vulkan_context.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace rtxmc {
namespace {

void log(const char* fmt, ...) {
    std::fprintf(stderr, "[rtxmc vkres] ");
    va_list ap; va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

uint32_t find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags wanted) {
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

// MC GpuBuffer.USAGE_* bit values (from javap inspection):
//   USAGE_MAP_READ              = 1
//   USAGE_MAP_WRITE             = 2
//   USAGE_HINT_CLIENT_STORAGE   = 4
//   USAGE_COPY_DST              = 8
//   USAGE_COPY_SRC              = 16
//   USAGE_VERTEX                = 32
//   USAGE_INDEX                 = 64
//   USAGE_UNIFORM               = 128
//   USAGE_UNIFORM_TEXEL_BUFFER  = 256
constexpr uint32_t MC_USAGE_MAP_READ     = 1;
constexpr uint32_t MC_USAGE_MAP_WRITE    = 2;
constexpr uint32_t MC_USAGE_COPY_DST     = 8;
constexpr uint32_t MC_USAGE_COPY_SRC     = 16;
constexpr uint32_t MC_USAGE_VERTEX       = 32;
constexpr uint32_t MC_USAGE_INDEX        = 64;
constexpr uint32_t MC_USAGE_UNIFORM      = 128;
constexpr uint32_t MC_USAGE_TEXEL_BUFFER = 256;

VkBufferUsageFlags translate_buffer_usage(uint32_t mc_usage) {
    VkBufferUsageFlags out = 0;
    if (mc_usage & MC_USAGE_COPY_DST)     out |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (mc_usage & MC_USAGE_COPY_SRC)     out |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (mc_usage & MC_USAGE_VERTEX)       out |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (mc_usage & MC_USAGE_INDEX)        out |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (mc_usage & MC_USAGE_UNIFORM)      out |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (mc_usage & MC_USAGE_TEXEL_BUFFER) out |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
    // If nothing matched, default to vertex+transfer (common safe combo). MC
    // hands us usage=0 occasionally for "any-purpose scratch" buffers.
    if (out == 0) out = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    return out;
}

VkFormat translate_texture_format(uint32_t code) {
    switch (code) {
        case 0: return VK_FORMAT_R8G8B8A8_UNORM; // RGBA8
        case 1: return VK_FORMAT_R8_UNORM;       // RED8
        case 2: return VK_FORMAT_R8_SINT;        // RED8I
        case 3: return VK_FORMAT_D32_SFLOAT;     // DEPTH32
        default: return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

// MC GpuTexture.USAGE_* bits (from javap):
//   USAGE_COPY_DST            = 1
//   USAGE_COPY_SRC            = 2
//   USAGE_TEXTURE_BINDING     = 4
//   USAGE_RENDER_ATTACHMENT   = 8
//   USAGE_CUBEMAP_COMPATIBLE  = 16
constexpr uint32_t MC_TEX_COPY_DST          = 1;
constexpr uint32_t MC_TEX_COPY_SRC          = 2;
constexpr uint32_t MC_TEX_TEXTURE_BINDING   = 4;
constexpr uint32_t MC_TEX_RENDER_ATTACHMENT = 8;
constexpr uint32_t MC_TEX_CUBEMAP           = 16;

VkImageUsageFlags translate_texture_usage(uint32_t mc_usage, uint32_t format_code) {
    VkImageUsageFlags out = 0;
    if (mc_usage & MC_TEX_COPY_DST)        out |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (mc_usage & MC_TEX_COPY_SRC)        out |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (mc_usage & MC_TEX_TEXTURE_BINDING) out |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (mc_usage & MC_TEX_RENDER_ATTACHMENT) {
        out |= (format_code == 3) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                                  : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if (out == 0) out = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return out;
}

VkSamplerAddressMode translate_address_mode(uint32_t code) {
    return code == 1 ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                     : VK_SAMPLER_ADDRESS_MODE_REPEAT;
}
VkFilter translate_filter_mode(uint32_t code) {
    return code == 1 ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
}

// ---- Resource records + registry ------------------------------------------

struct BufferRec {
    VkBuffer       buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    uint64_t       size   = 0;
    uint32_t       usage  = 0;
    bool           mapped = false;
};

struct TextureRec {
    VkImage        image     = VK_NULL_HANDLE;
    VkDeviceMemory memory    = VK_NULL_HANDLE;
    VkFormat       format    = VK_FORMAT_UNDEFINED;
    uint32_t       width     = 0;
    uint32_t       height    = 0;
    uint32_t       mip_levels = 1;
    uint32_t       depth_or_layers = 1;
};

struct ViewRec {
    VkImageView    view = VK_NULL_HANDLE;
    uint64_t       texture_handle = 0;
    uint32_t       base_mip = 0;
    uint32_t       mip_levels = 1;
};

struct SamplerRec {
    VkSampler sampler = VK_NULL_HANDLE;
};

std::mutex g_mutex;
std::unordered_map<uint64_t, BufferRec>  g_buffers;
std::unordered_map<uint64_t, TextureRec> g_textures;
std::unordered_map<uint64_t, ViewRec>    g_views;
std::unordered_map<uint64_t, SamplerRec> g_samplers;
std::atomic<uint64_t> g_next_handle{1};

uint64_t alloc_handle() {
    return g_next_handle.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

// ---- Buffers ---------------------------------------------------------------

uint64_t vkres_create_buffer(uint32_t usage, uint64_t size, const void* initial_data) {
    if (size == 0) return 0;
    auto& c = ctx();

    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size        = size;
    bci.usage       = translate_buffer_usage(usage);
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    BufferRec r{};
    if (vkCreateBuffer(c.device, &bci, nullptr, &r.buffer) != VK_SUCCESS) {
        log("create_buffer: vkCreateBuffer failed (usage=0x%x size=%llu)",
            usage, (unsigned long long)size);
        return 0;
    }

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(c.device, r.buffer, &mr);

    // HOST_VISIBLE+COHERENT for everything in 1.6.1b. DEVICE_LOCAL + staging
    // copies come later when we optimise bandwidth.
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = find_memory_type(mr.memoryTypeBits,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(c.device, &ai, nullptr, &r.memory) != VK_SUCCESS) {
        vkDestroyBuffer(c.device, r.buffer, nullptr);
        log("create_buffer: memory alloc failed (%llu B)", (unsigned long long)size);
        return 0;
    }
    vkBindBufferMemory(c.device, r.buffer, r.memory, 0);

    if (initial_data) {
        void* mapped = nullptr;
        if (vkMapMemory(c.device, r.memory, 0, size, 0, &mapped) == VK_SUCCESS) {
            std::memcpy(mapped, initial_data, size);
            vkUnmapMemory(c.device, r.memory);
        }
    }

    r.size  = size;
    r.usage = usage;

    uint64_t h = alloc_handle();
    {
        std::lock_guard<std::mutex> g(g_mutex);
        g_buffers[h] = r;
    }
    return h;
}

void vkres_destroy_buffer(uint64_t handle) {
    BufferRec r{};
    {
        std::lock_guard<std::mutex> g(g_mutex);
        auto it = g_buffers.find(handle);
        if (it == g_buffers.end()) return;
        r = it->second;
        g_buffers.erase(it);
    }
    auto& c = ctx();
    if (r.mapped) vkUnmapMemory(c.device, r.memory);
    if (r.buffer) vkDestroyBuffer(c.device, r.buffer, nullptr);
    if (r.memory) vkFreeMemory(c.device, r.memory, nullptr);
}

void* vkres_map_buffer(uint64_t handle, uint64_t offset, uint64_t length) {
    std::lock_guard<std::mutex> g(g_mutex);
    auto it = g_buffers.find(handle);
    if (it == g_buffers.end()) return nullptr;
    auto& r = it->second;
    if (r.mapped) return nullptr; // double-map; MC contract forbids
    if (length == 0) length = r.size - offset;

    void* p = nullptr;
    if (vkMapMemory(ctx().device, r.memory, offset, length, 0, &p) != VK_SUCCESS) {
        return nullptr;
    }
    r.mapped = true;
    return p;
}

void vkres_unmap_buffer(uint64_t handle) {
    std::lock_guard<std::mutex> g(g_mutex);
    auto it = g_buffers.find(handle);
    if (it == g_buffers.end() || !it->second.mapped) return;
    vkUnmapMemory(ctx().device, it->second.memory);
    it->second.mapped = false;
}

VkBufferRef vkres_buffer_ref(uint64_t handle) {
    std::lock_guard<std::mutex> g(g_mutex);
    auto it = g_buffers.find(handle);
    if (it == g_buffers.end()) return {nullptr, 0, 0};
    return {(void*)it->second.buffer, it->second.size, it->second.usage};
}

// ---- Textures --------------------------------------------------------------

uint64_t vkres_create_texture(uint32_t usage, uint32_t format_code,
                              uint32_t width, uint32_t height,
                              uint32_t depth_or_layers, uint32_t mip_levels) {
    if (width == 0 || height == 0) return 0;
    auto& c = ctx();

    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = translate_texture_format(format_code);
    ici.extent        = {width, height, 1};
    ici.mipLevels     = mip_levels ? mip_levels : 1;
    ici.arrayLayers   = depth_or_layers ? depth_or_layers : 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = translate_texture_usage(usage, format_code);
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    if (usage & MC_TEX_CUBEMAP) {
        ici.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }

    TextureRec r{};
    if (vkCreateImage(c.device, &ici, nullptr, &r.image) != VK_SUCCESS) {
        log("create_texture: vkCreateImage failed (%ux%u fmt=%u usage=0x%x)",
            width, height, format_code, usage);
        return 0;
    }

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(c.device, r.image, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = find_memory_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(c.device, &ai, nullptr, &r.memory) != VK_SUCCESS) {
        vkDestroyImage(c.device, r.image, nullptr);
        log("create_texture: memory alloc failed");
        return 0;
    }
    vkBindImageMemory(c.device, r.image, r.memory, 0);

    r.format          = ici.format;
    r.width           = width;
    r.height          = height;
    r.mip_levels      = ici.mipLevels;
    r.depth_or_layers = ici.arrayLayers;

    uint64_t h = alloc_handle();
    {
        std::lock_guard<std::mutex> g(g_mutex);
        g_textures[h] = r;
    }
    return h;
}

void vkres_destroy_texture(uint64_t handle) {
    TextureRec r{};
    {
        std::lock_guard<std::mutex> g(g_mutex);
        auto it = g_textures.find(handle);
        if (it == g_textures.end()) return;
        r = it->second;
        g_textures.erase(it);
    }
    auto& c = ctx();
    if (r.image)  vkDestroyImage(c.device, r.image, nullptr);
    if (r.memory) vkFreeMemory(c.device, r.memory, nullptr);
}

// ---- Texture views ---------------------------------------------------------

uint64_t vkres_create_texture_view(uint64_t texture_handle,
                                   uint32_t base_mip, uint32_t mip_levels) {
    VkImage img = VK_NULL_HANDLE;
    VkFormat fmt = VK_FORMAT_UNDEFINED;
    {
        std::lock_guard<std::mutex> g(g_mutex);
        auto it = g_textures.find(texture_handle);
        if (it == g_textures.end()) return 0;
        img = it->second.image;
        fmt = it->second.format;
    }

    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image    = img;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format   = fmt;
    vi.subresourceRange.aspectMask = (fmt == VK_FORMAT_D32_SFLOAT)
                                     ? VK_IMAGE_ASPECT_DEPTH_BIT
                                     : VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.baseMipLevel   = base_mip;
    vi.subresourceRange.levelCount     = mip_levels ? mip_levels : 1;
    vi.subresourceRange.baseArrayLayer = 0;
    vi.subresourceRange.layerCount     = 1;

    ViewRec rec{};
    rec.texture_handle = texture_handle;
    rec.base_mip       = base_mip;
    rec.mip_levels     = vi.subresourceRange.levelCount;
    if (vkCreateImageView(ctx().device, &vi, nullptr, &rec.view) != VK_SUCCESS) {
        log("create_texture_view: failed (base_mip=%u levels=%u)", base_mip, mip_levels);
        return 0;
    }

    uint64_t h = alloc_handle();
    {
        std::lock_guard<std::mutex> g(g_mutex);
        g_views[h] = rec;
    }
    return h;
}

void vkres_destroy_texture_view(uint64_t handle) {
    ViewRec r{};
    {
        std::lock_guard<std::mutex> g(g_mutex);
        auto it = g_views.find(handle);
        if (it == g_views.end()) return;
        r = it->second;
        g_views.erase(it);
    }
    if (r.view) vkDestroyImageView(ctx().device, r.view, nullptr);
}

// ---- Samplers --------------------------------------------------------------

uint64_t vkres_create_sampler(uint32_t addr_u, uint32_t addr_v,
                              uint32_t min_filter, uint32_t mag_filter,
                              uint32_t max_aniso) {
    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter    = translate_filter_mode(mag_filter);
    sci.minFilter    = translate_filter_mode(min_filter);
    sci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.addressModeU = translate_address_mode(addr_u);
    sci.addressModeV = translate_address_mode(addr_v);
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.anisotropyEnable = max_aniso > 1 ? VK_TRUE : VK_FALSE;
    sci.maxAnisotropy    = (float)max_aniso;
    sci.borderColor      = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sci.maxLod           = VK_LOD_CLAMP_NONE;

    SamplerRec r{};
    if (vkCreateSampler(ctx().device, &sci, nullptr, &r.sampler) != VK_SUCCESS) {
        log("create_sampler: failed");
        return 0;
    }
    uint64_t h = alloc_handle();
    {
        std::lock_guard<std::mutex> g(g_mutex);
        g_samplers[h] = r;
    }
    return h;
}

void vkres_destroy_sampler(uint64_t handle) {
    SamplerRec r{};
    {
        std::lock_guard<std::mutex> g(g_mutex);
        auto it = g_samplers.find(handle);
        if (it == g_samplers.end()) return;
        r = it->second;
        g_samplers.erase(it);
    }
    if (r.sampler) vkDestroySampler(ctx().device, r.sampler, nullptr);
}

VkResStats vkres_stats() {
    std::lock_guard<std::mutex> g(g_mutex);
    return { (uint32_t)g_buffers.size(),  (uint32_t)g_textures.size(),
             (uint32_t)g_views.size(),    (uint32_t)g_samplers.size() };
}

} // namespace rtxmc
