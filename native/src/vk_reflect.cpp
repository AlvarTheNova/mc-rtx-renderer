#include "vk_reflect.h"

#include <spirv_cross/spirv_cross.hpp>

#include <cstdarg>
#include <cstdio>

namespace rtxmc {
namespace {

void log(const char* fmt, ...) {
    std::fprintf(stderr, "[rtxmc reflect] ");
    va_list ap; va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

uint32_t array_size_for(const spirv_cross::Compiler& comp, const spirv_cross::Resource& r) {
    const auto& t = comp.get_type(r.type_id);
    if (t.array.empty()) return 1;
    // First-dim array size. Runtime-sized (length 0) → treat as 1 for now;
    // bindless uses descriptor indexing which we won't support in 1.6.1e.
    return t.array[0] == 0 ? 1 : t.array[0];
}

} // namespace

ReflectionResult reflect_spirv(const uint32_t* spirv, size_t words, const char* label) {
    ReflectionResult out;
    out.ok = false;
    if (!spirv || words == 0) {
        log("reflect: empty SPIR-V (%s)", label ? label : "?");
        return out;
    }

    try {
        spirv_cross::Compiler comp(spirv, words);
        spirv_cross::ShaderResources res = comp.get_shader_resources();

        for (const auto& r : res.sampled_images) {
            ReflectedBinding b{};
            b.name            = r.name;
            b.kind            = ResourceKind::SampledImage;
            b.descriptor_set  = comp.get_decoration(r.id, spv::DecorationDescriptorSet);
            b.binding         = comp.get_decoration(r.id, spv::DecorationBinding);
            b.array_size      = array_size_for(comp, r);
            out.bindings.push_back(std::move(b));
        }
        for (const auto& r : res.uniform_buffers) {
            ReflectedBinding b{};
            b.name            = r.name;
            b.kind            = ResourceKind::UniformBuffer;
            b.descriptor_set  = comp.get_decoration(r.id, spv::DecorationDescriptorSet);
            b.binding         = comp.get_decoration(r.id, spv::DecorationBinding);
            b.array_size      = array_size_for(comp, r);
            out.bindings.push_back(std::move(b));
        }
        for (const auto& r : res.storage_buffers) {
            ReflectedBinding b{};
            b.name            = r.name;
            b.kind            = ResourceKind::StorageBuffer;
            b.descriptor_set  = comp.get_decoration(r.id, spv::DecorationDescriptorSet);
            b.binding         = comp.get_decoration(r.id, spv::DecorationBinding);
            b.array_size      = array_size_for(comp, r);
            out.bindings.push_back(std::move(b));
        }
        for (const auto& r : res.push_constant_buffers) {
            ReflectedBinding b{};
            b.name            = r.name;
            b.kind            = ResourceKind::PushConstant;
            b.descriptor_set  = 0; // n/a
            b.binding         = 0; // n/a
            b.array_size      = 1;
            out.bindings.push_back(std::move(b));
        }

        out.ok = true;
        return out;
    } catch (const std::exception& e) {
        log("reflect: spirv-cross threw on '%s': %s", label ? label : "?", e.what());
        return out;
    } catch (...) {
        log("reflect: spirv-cross threw unknown exception on '%s'", label ? label : "?");
        return out;
    }
}

} // namespace rtxmc
