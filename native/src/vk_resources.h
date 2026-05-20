#pragma once

#include <cstdint>

// Phase 1.6.1b — Vulkan-backed resource primitives that fulfil Mojang's
// Blaze3D GpuDevice contract. Each function returns an opaque uint64_t
// handle (a pointer-sized identifier) that Java holds as a `long`.
//
// Thread-safety: createX/destroyX are guarded by an internal mutex so the
// Java side can call from any thread (MC's texture workers, render thread,
// etc.). Map/unmap are NOT mutex-guarded — caller's responsibility, since
// the GpuBuffer.MappedView API in MC is single-threaded by contract.
//
// Format/mode codes mirror the Blaze3D enum ordinal:
//   TextureFormat: 0=RGBA8, 1=RED8, 2=RED8I, 3=DEPTH32
//   AddressMode:   0=REPEAT, 1=CLAMP_TO_EDGE
//   FilterMode:    0=NEAREST, 1=LINEAR

namespace rtxmc {

// ---- Buffers ---------------------------------------------------------------

// usage: bitmask of GpuBuffer.USAGE_* constants from Mojang. We honour the
// VERTEX / INDEX / UNIFORM / MAP_READ / MAP_WRITE / COPY_DST / COPY_SRC bits.
// If `initial_data` is non-null, the buffer is created HOST_VISIBLE and the
// bytes are copied in immediately. Otherwise HOST_VISIBLE allocation only.
uint64_t vkres_create_buffer(uint32_t usage, uint64_t size, const void* initial_data);
void     vkres_destroy_buffer(uint64_t handle);

// Returns CPU-mappable pointer + maps the underlying VkDeviceMemory.
// `length` 0 means "to end of buffer".
void*    vkres_map_buffer(uint64_t handle, uint64_t offset, uint64_t length);
void     vkres_unmap_buffer(uint64_t handle);

// Accessors used by the encoder/render-pass to bind buffers to draw commands.
struct VkBufferRef {
    void* vk_buffer;       // VkBuffer
    uint64_t size;
    uint32_t usage;
};
VkBufferRef vkres_buffer_ref(uint64_t handle);

// ---- Textures + Views + Samplers ------------------------------------------

uint64_t vkres_create_texture(uint32_t usage, uint32_t format_code,
                              uint32_t width, uint32_t height,
                              uint32_t depth_or_layers, uint32_t mip_levels);
void     vkres_destroy_texture(uint64_t handle);

// Whole-texture view; alias for create_texture_view(handle, 0, mip_levels).
// Returns its own handle; destroy via vkres_destroy_texture_view.
uint64_t vkres_create_texture_view(uint64_t texture_handle,
                                   uint32_t base_mip, uint32_t mip_levels);
void     vkres_destroy_texture_view(uint64_t handle);

uint64_t vkres_create_sampler(uint32_t addr_u, uint32_t addr_v,
                              uint32_t min_filter, uint32_t mag_filter,
                              uint32_t max_aniso);
void     vkres_destroy_sampler(uint64_t handle);

// ---- Diagnostics -----------------------------------------------------------

// Returns the current live-resource counts. Useful for leak checks at
// shutdown (should drop to zero once MC closes all its GpuBuffer/Texture
// handles). Phase 1.6.1b uses these for logging.
struct VkResStats {
    uint32_t live_buffers;
    uint32_t live_textures;
    uint32_t live_views;
    uint32_t live_samplers;
};
VkResStats vkres_stats();

} // namespace rtxmc
