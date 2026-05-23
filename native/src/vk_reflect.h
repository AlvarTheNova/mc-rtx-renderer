#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Phase 1.6.1e — SPIR-V reflection via spirv-cross. shaderc auto-assigns
// binding numbers to Mojang's bindless GLSL (see vk_shaderc.cpp's
// SetAutoBindUniforms). To bind real textures/uniforms at draw time we need
// to know what numbers got assigned to which name. spirv-cross walks the
// SPIR-V module and surfaces that mapping.

namespace rtxmc {

enum class ResourceKind : uint32_t {
    SampledImage = 0,   // combined image+sampler (vk: COMBINED_IMAGE_SAMPLER)
    UniformBuffer = 1,  // vk: UNIFORM_BUFFER
    StorageBuffer = 2,  // vk: STORAGE_BUFFER (rare in MC but possible)
    PushConstant  = 3,  // not a descriptor but reported separately
};

struct ReflectedBinding {
    std::string name;       // shader-side identifier (matches setUniform/bindTexture)
    ResourceKind kind;
    uint32_t descriptor_set;  // usually 0 for our auto-bound shaders
    uint32_t binding;         // slot number
    uint32_t array_size;      // 1 unless explicitly sized array
};

struct ReflectionResult {
    std::vector<ReflectedBinding> bindings;
    // True iff the SPIR-V module was successfully parsed. False indicates
    // a malformed module or spirv-cross internal error — bindings vector
    // may be empty or partial.
    bool ok;
};

// Reflect a single SPIR-V module. Stage doesn't change the output but lets
// us log meaningful messages.
ReflectionResult reflect_spirv(const uint32_t* spirv, size_t word_count,
                                const char* debug_label);

} // namespace rtxmc
