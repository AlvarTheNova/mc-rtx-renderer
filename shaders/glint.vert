#version 460

// Phase 1.5.2c — glint variant. Format (20 B):
//   offset 0  : position vec3 f32  (12 B)  — camera-relative-world coords
//   offset 12 : uv0      vec2 f32  ( 8 B)  — scrolling glint texture
//
// Vanilla scrolls the UVs with a time uniform and stacks two passes for the
// hatched diagonal effect. For first-light we just pass UVs through and
// sample the atlas (wrong texture; correct tex is textures/misc/enchanted_glint_*).

layout(push_constant) uniform PC {
    mat4 viewRot;
    mat4 proj;
} pc;

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv0;

layout(location = 0) out vec2 v_uv0;

const mat4 GL_TO_VK_CLIP = mat4(
    1.0, 0.0, 0.0, 0.0,
    0.0,-1.0, 0.0, 0.0,
    0.0, 0.0, 0.5, 0.0,
    0.0, 0.0, 0.5, 1.0
);

void main() {
    gl_Position = GL_TO_VK_CLIP * pc.proj * pc.viewRot * vec4(in_pos, 1.0);
    v_uv0 = in_uv0;
}
