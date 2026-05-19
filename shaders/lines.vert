#version 460

// Phase 1.5.2c — lines variant (selection outline, debug grid). Format (24 B):
//   offset 0  : position   vec3 f32       (12 B)  — camera-relative-world coords
//   offset 12 : color      uvec4 u8 RGBA  ( 4 B)  — line color
//   offset 16 : normal     uvec4 u8       ( 4 B)  — packed normal (ignored)
//   offset 20 : line_width f32            ( 4 B)  — per-vertex width (ignored;
//                                                   VK uses vkCmdSetLineWidth)

layout(push_constant) uniform PC {
    mat4 viewRot;
    mat4 proj;
} pc;

layout(location = 0) in vec3  in_pos;
layout(location = 1) in uvec4 in_color;
layout(location = 2) in uvec4 in_normal;     // unused but bound for layout
layout(location = 3) in float in_line_width; // unused but bound for layout

layout(location = 0) out vec4 v_color;

const mat4 GL_TO_VK_CLIP = mat4(
    1.0, 0.0, 0.0, 0.0,
    0.0,-1.0, 0.0, 0.0,
    0.0, 0.0, 0.5, 0.0,
    0.0, 0.0, 0.5, 1.0
);

void main() {
    gl_Position = GL_TO_VK_CLIP * pc.proj * pc.viewRot * vec4(in_pos, 1.0);
    v_color = vec4(in_color) * (1.0 / 255.0);
}
