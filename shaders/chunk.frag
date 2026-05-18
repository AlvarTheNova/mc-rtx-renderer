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
    // Diagnostic: paint the ENTIRE atlas across the screen, ignoring vertex UVs.
    // Maps screen pixel (x, y) to atlas UV. If we see actual MC block textures
    // tiled across visible chunks, atlas reconstruction is correct and the bug
    // is elsewhere. If we see mostly black with one lava patch, atlas content
    // itself is bad.
    vec2 atlas_uv = gl_FragCoord.xy / vec2(2048.0);
    vec4 tex = texture(u_atlas, atlas_uv);
    out_color = vec4(tex.rgb, 1.0);
}
