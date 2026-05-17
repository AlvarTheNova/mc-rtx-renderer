// Shared between rgen / rmiss / rchit.
// Compile target: GLSL 460, Vulkan 1.3, ray tracing pipeline.

#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

// Per-frame UBO at set 0, binding 2 — see PathTracer::build_descriptor_set
layout(set = 0, binding = 2, std140) uniform FrameUbo {
    mat4  view;
    mat4  proj;
    mat4  view_inv;
    mat4  proj_inv;
    mat4  view_prev;
    mat4  proj_prev;
    vec4  cam_pos_ws;            // .w = time of day [0..1]
    vec4  sun_dir_ws;            // .w = sun radius (apparent angular)
    vec4  sky_color_zenith;
    vec4  sky_color_horizon;
    vec2  jitter_pixels;         // Halton 2,3 in screen pixels for RR
    uvec2 internal_resolution;
    uint  frame_index;
    uint  flags;                 // bit 0 = first frame after reset
} U;

// Ray payload — kept tight, 32 bytes
struct RayPayload {
    vec3 radiance;
    uint depth;
    vec3 throughput;
    uint seed;
};

// Hash-based PRNG seeded per pixel + frame (PCG)
uint pcg_hash(uint v) {
    uint s = v * 747796405u + 2891336453u;
    uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}
float rand01(inout uint s) { s = pcg_hash(s); return float(s) * (1.0 / 4294967296.0); }
