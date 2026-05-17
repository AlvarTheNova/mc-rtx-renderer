#version 460
#include "common.glsl"

layout(set = 0, binding = 0) uniform accelerationStructureEXT TLAS;

// Bindless textures: albedo (RGB) + alpha (A), normal (RG octahedral) + AO (B),
// MER (metal/emissive/rough RGB).
layout(set = 0, binding = 3) uniform sampler2D tex_albedo[];
layout(set = 0, binding = 4) uniform sampler2D tex_normal[];
layout(set = 0, binding = 5) uniform sampler2D tex_mer[];

// Material table indexed by gl_InstanceCustomIndexEXT + primitive
struct Material {
    uint  albedo_idx;
    uint  normal_idx;
    uint  mer_idx;
    float emissive_strength;
};
layout(set = 0, binding = 6, std430) readonly buffer MaterialTable {
    Material mats[];
} M;

layout(location = 0) rayPayloadInEXT RayPayload pl;
hitAttributeEXT vec2 baryAttr;

void main() {
    // TODO Phase 3:
    //   - Fetch triangle vertices via buffer_reference + gl_InstanceID +
    //     gl_PrimitiveID to recover position, normal, UV, material id.
    //   - Sample tex_albedo[mat.albedo_idx] at interpolated UV.
    //   - Sample tex_normal -> world-space normal via TBN.
    //   - Sample tex_mer -> metal/emissive/rough.
    //   - NEE: shoot one shadow ray toward sun cone (importance-sampled).
    //   - NEE: shoot one shadow ray toward sampled emissive (ReSTIR DI later).
    //   - Russian-roulette continue: bounce one diffuse + one specular,
    //     up to maxPipelineRayRecursionDepth = 2 (or use loop-based PT in
    //     rgen for deeper bounces — recursive trace is expensive on RT cores).
    //   - Write first-hit albedo/normal/rough-met/spec-hit-dist to the
    //     G-buffer images bound in rgen.

    pl.radiance += pl.throughput * vec3(0.5); // placeholder grey hit
}
