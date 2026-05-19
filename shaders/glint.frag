#version 460

// Phase 1.5.2c — glint fragment. Additive blend (configured pipeline-side
// via ONE + ONE). We modulate the sampled colour by a fixed shimmer tint
// so the additive overlay reads as "magical" rather than just bright.

layout(set = 0, binding = 0) uniform sampler2D u_atlas;

layout(location = 0) in vec2 v_uv0;
layout(location = 0) out vec4 out_color;

const vec3 GLINT_TINT = vec3(0.55, 0.45, 1.00);   // violet shimmer

void main() {
    vec4 tex = texture(u_atlas, v_uv0);
    // Use the luminance of whatever the atlas gave us as the glint mask
    // (proper glint texture comes in 1.5.2d when per-layer textures land).
    float l = dot(tex.rgb, vec3(0.299, 0.587, 0.114));
    out_color = vec4(GLINT_TINT * l * 0.4, l);
}
