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
    // Atlas painted-to-screen diagnostic confirmed atlas content is fine.
    // Bug must be in sampling — most likely V-flip between MC's GL-convention
    // UVs (origin bottom-left) and Vulkan's texture sampling (origin top-left).
    //
    // Sample with V flipped. If textures look right, V-flip is the fix.
    vec2 uv = vec2(v_uv0.x, 1.0 - v_uv0.y);
    vec4 tex = texture(u_atlas, uv);

    float ndotl = max(dot(v_normal, SUN_DIR), 0.0);
    float lighting = 0.25 + 0.5 * ndotl + 0.25 * v_lightCombined;
    out_color = vec4(tex.rgb * v_color * lighting, 1.0);
}
