#version 460

// First-light fragment rite. No texture sampling yet — Phase 1.4.4 will bring
// the blessed block atlas. For now we modulate vertex color by the combined
// light value and a cheap dot-NdotL against a fixed sun direction so the
// geometry isn't an unreadable flat smear.

layout(location = 0) in vec3  v_color;
layout(location = 1) in vec2  v_uv0;
layout(location = 2) in vec3  v_normal;
layout(location = 3) in float v_lightCombined;

layout(location = 0) out vec4 out_color;

const vec3 SUN_DIR = normalize(vec3(0.4, 1.0, 0.3));

void main() {
    float ndotl = max(dot(v_normal, SUN_DIR), 0.0);
    float lighting = 0.25 + 0.5 * ndotl + 0.25 * v_lightCombined;
    out_color = vec4(v_color * lighting, 1.0);
}
