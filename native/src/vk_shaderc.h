#pragma once

#include <cstdint>
#include <vector>
#include <string_view>

// Phase 1.6.1d — runtime GLSL → SPIR-V via shaderc (bundled in Vulkan SDK).
// Mojang's RenderPipelines reference shaders by Identifier; on the Java side
// we use ShaderSourceGetter to pull the GLSL string, then hand it here to
// compile + cache. Subsequent calls with the same source-hash return the
// cached SPIR-V immediately.

namespace rtxmc {

enum class ShaderStage : uint32_t {
    Vertex   = 0,
    Fragment = 1,
};

// Compiles GLSL → SPIR-V words. Caches by (source-hash, stage). Returns
// empty vector on compile error and logs the shaderc error message.
// Thread-safe: protected by an internal mutex.
std::vector<uint32_t> shaderc_compile(std::string_view glsl,
                                       ShaderStage stage,
                                       std::string_view debug_label);

// Diagnostic.
struct ShadercStats {
    uint32_t compile_count;
    uint32_t cache_hits;
    uint32_t compile_failures;
};
ShadercStats shaderc_stats();

} // namespace rtxmc
