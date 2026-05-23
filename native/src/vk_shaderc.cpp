#include "vk_shaderc.h"

#include <shaderc/shaderc.hpp>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>

namespace rtxmc {
namespace {

void log(const char* fmt, ...) {
    std::fprintf(stderr, "[rtxmc shaderc] ");
    va_list ap; va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

// 64-bit FNV-1a — fast, low collision for our use. Cache key combines this
// hash + the stage tag so vertex/fragment versions of the same source can
// coexist (paranoia: identical strings shouldn't happen across stages but
// the cost is one byte of key).
uint64_t hash_source(std::string_view s) {
    constexpr uint64_t FNV_OFFSET = 0xCBF29CE484222325ull;
    constexpr uint64_t FNV_PRIME  = 0x100000001B3ull;
    uint64_t h = FNV_OFFSET;
    for (unsigned char c : s) {
        h ^= c;
        h *= FNV_PRIME;
    }
    return h;
}

struct CacheKey {
    uint64_t hash;
    uint32_t stage;
    bool operator==(const CacheKey& o) const noexcept {
        return hash == o.hash && stage == o.stage;
    }
};
struct CacheKeyHash {
    size_t operator()(const CacheKey& k) const noexcept {
        return (size_t)(k.hash ^ ((uint64_t)k.stage * 0x9E3779B97F4A7C15ull));
    }
};

class ShadercCache {
public:
    ShadercCache() : compiler_() {
        // Default optimization = none (faster compile, slightly larger SPIR-V).
        // OK for first-light; switch to Performance later if shader-bound.
        options_.SetSourceLanguage(shaderc_source_language_glsl);
        options_.SetTargetEnvironment(shaderc_target_env_vulkan,
                                      shaderc_env_version_vulkan_1_3);
        options_.SetTargetSpirv(shaderc_spirv_version_1_6);

        // Phase 1.6.1d step 5 — Mojang's GLSL is GL-style: no explicit
        // layout(binding=) on samplers/uniforms, no layout(location=) on
        // in/out varyings. SPIR-V demands both. These flags tell shaderc
        // to auto-assign them sequentially in declaration order. Java side
        // will need to read these back via reflection if we ever care about
        // exact slot numbers, but for now sequential is consistent.
        options_.SetAutoBindUniforms(true);
        options_.SetAutoMapLocations(true);
    }

    std::vector<uint32_t> compile(std::string_view glsl, ShaderStage stage,
                                  std::string_view label) {
        const uint64_t h = hash_source(glsl);
        const CacheKey k{h, (uint32_t)stage};

        {
            std::lock_guard<std::mutex> g(mutex_);
            auto it = cache_.find(k);
            if (it != cache_.end()) {
                cache_hits_.fetch_add(1, std::memory_order_relaxed);
                return it->second;
            }
        }

        const shaderc_shader_kind kind = (stage == ShaderStage::Vertex)
            ? shaderc_glsl_vertex_shader
            : shaderc_glsl_fragment_shader;

        const std::string label_s(label);
        compile_count_.fetch_add(1, std::memory_order_relaxed);

        shaderc::SpvCompilationResult result;
        {
            std::lock_guard<std::mutex> g(mutex_); // shaderc::Compiler isn't thread-safe
            result = compiler_.CompileGlslToSpv(glsl.data(), glsl.size(),
                                                kind, label_s.c_str(),
                                                "main", options_);
        }

        if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
            compile_failures_.fetch_add(1, std::memory_order_relaxed);
            log("compile FAILED for '%s' (%s): %s",
                label_s.c_str(),
                stage == ShaderStage::Vertex ? "vert" : "frag",
                result.GetErrorMessage().c_str());
            return {};
        }

        std::vector<uint32_t> spirv(result.cbegin(), result.cend());
        log("compile ok: %s (%s) → %zu SPIR-V words",
            label_s.c_str(),
            stage == ShaderStage::Vertex ? "vert" : "frag",
            spirv.size());

        {
            std::lock_guard<std::mutex> g(mutex_);
            cache_[k] = spirv;
        }
        return spirv;
    }

    ShadercStats stats() const {
        return {
            compile_count_.load(std::memory_order_relaxed),
            cache_hits_.load(std::memory_order_relaxed),
            compile_failures_.load(std::memory_order_relaxed),
        };
    }

private:
    shaderc::Compiler       compiler_;
    shaderc::CompileOptions options_;
    std::mutex              mutex_;
    std::unordered_map<CacheKey, std::vector<uint32_t>, CacheKeyHash> cache_;
    std::atomic<uint32_t>   compile_count_{0};
    std::atomic<uint32_t>   cache_hits_{0};
    std::atomic<uint32_t>   compile_failures_{0};
};

ShadercCache& cache() {
    static ShadercCache c;
    return c;
}

} // namespace

std::vector<uint32_t> shaderc_compile(std::string_view glsl, ShaderStage stage,
                                       std::string_view label) {
    return cache().compile(glsl, stage, label);
}

ShadercStats shaderc_stats() { return cache().stats(); }

} // namespace rtxmc
