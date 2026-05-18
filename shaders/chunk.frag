#version 460

// Phase 1.4.4b — sample the blessed block atlas at MC's UV0 coords, modulate
// by vertex color (biome tint for grass / leaves, white otherwise), then by
// sun NdotL and stored block+sky light.

layout(set = 0, binding = 0) uniform sampler2D u_atlas;

layout(location = 0) in vec3  v_color;
layout(location = 1) in vec2  v_uv0;
layout(location = 2) in vec3  v_normal;
layout(location = 3) in float v_lightCombined;

layout(location = 0) out vec4 out_color;

const vec3 SUN_DIR = normalize(vec3(0.4, 1.0, 0.3));

void main() {
    // v_uv0 here is now POSITION-DERIVED (see vert shader). If we see smooth
    // R→G gradients per chunk face, attribute interpolation works fine and
    // the specific in_uv0 attribute read in vert was the broken thing.
    out_color = vec4(v_uv0.x, v_uv0.y, 0.5, 1.0);
}
