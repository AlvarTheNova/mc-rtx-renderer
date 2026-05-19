#version 460

// Phase 1.5.2b — entity fragment. Reuses the block atlas sampler for first
// light (textures will be wrong since entities have their own skins — that
// gets fixed in 1.5.2d). For now we just want SOMETHING rasterised on screen.

layout(set = 0, binding = 0) uniform sampler2D u_atlas;

layout(location = 0) in vec3  v_color;
layout(location = 1) in vec2  v_uv0;
layout(location = 2) in vec3  v_normal;
layout(location = 3) in float v_lightCombined;

layout(location = 0) out vec4 out_color;

const vec3 SUN_DIR = normalize(vec3(0.4, 1.0, 0.3));

void main() {
    vec4 tex = texture(u_atlas, v_uv0);
    // Entity UVs that fall outside the block-atlas range will sample whatever
    // is at the wrapped coord — that's expected. We skip the alpha cutout the
    // chunk shader uses; entities use real alpha-blend.
    float ndotl = max(dot(v_normal, SUN_DIR), 0.0);
    float lighting = 0.35 + 0.5 * ndotl + 0.15 * v_lightCombined;
    out_color = vec4(tex.rgb * v_color * lighting, tex.a);
}
