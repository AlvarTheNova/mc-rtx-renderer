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
    vec4 tex = texture(u_atlas, v_uv0);
    // Drop fully transparent texels — keeps cutouts (when CUTOUT layer is
    // later wired) from polluting depth. SOLID layer should be opaque.
    if (tex.a < 0.05) discard;

    float ndotl = max(dot(v_normal, SUN_DIR), 0.0);
    float lighting = 0.25 + 0.5 * ndotl + 0.25 * v_lightCombined;
    out_color = vec4(tex.rgb * v_color * lighting, tex.a);
}
