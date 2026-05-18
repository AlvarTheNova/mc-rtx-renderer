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
    // Four-quadrant diagnostic. The screen is tiled in 256-pixel cells; within
    // each cell, the four corners run a different test. One observation tells
    // us what's wrong:
    //
    //   TL = solid color (whatever's at atlas (0.5, 0.5))
    //        → confirms atlas sampling works at all
    //   TR = sample at v_uv0 (the bug case)
    //        → mostly black means v_uv0 misses content
    //   BL = visualize v_uv0 as colors fract'd into [0,1]
    //        → smooth per-face gradient = UVs varying correctly,
    //          uniform per-face = UVs constant per face
    //   BR = sample at fract(v_uv0) (forces UV into [0,1])
    //        → if textures appear here but not TR, UV was >1 somehow

    bool top  = mod(gl_FragCoord.y, 256.0) < 128.0;
    bool left = mod(gl_FragCoord.x, 256.0) < 128.0;

    vec3 c;
    if (top && left) {
        c = texture(u_atlas, vec2(0.5, 0.5)).rgb;
    } else if (top && !left) {
        c = texture(u_atlas, v_uv0).rgb;
    } else if (!top && left) {
        c = vec3(fract(v_uv0.x), fract(v_uv0.y), 0.0);
    } else {
        c = texture(u_atlas, fract(v_uv0)).rgb;
    }
    out_color = vec4(c, 1.0);
}
