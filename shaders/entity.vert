#version 460

// Phase 1.5.2b — first-light entity rasterisation. MC 1.21.11 entity layer
// blessed format (36 B):
//
//   offset 0  : position    vec3 f32       (12 B)  — camera-relative-world coords
//   offset 12 : color       uvec4 u8 RGBA  ( 4 B)  — tint
//   offset 16 : uv0         vec2 f32       ( 8 B)  — entity skin atlas (wrong tex
//                                                   in 1.5.2b; 1.5.2d wires per-mob)
//   offset 24 : uv1         uvec2 u16      ( 4 B)  — overlay (damage flash)
//   offset 28 : uv2         uvec2 u16      ( 4 B)  — block + sky light
//   offset 32 : normal      uvec4 u8       ( 4 B)  — packed normal + pad
//
// CRITICAL: positions are ALREADY camera-translated by MC's MatrixStack at
// batch time, so we use viewRot (rotation only) NOT the full view matrix.

layout(push_constant) uniform PC {
    mat4 viewRot;     // rotation-only positionMatrix
    mat4 proj;        // perspective projection
} pc;

layout(location = 0) in vec3  in_pos;
layout(location = 1) in uvec4 in_color;
layout(location = 2) in vec2  in_uv0;
layout(location = 3) in uvec2 in_uv1;
layout(location = 4) in uvec2 in_uv2;
layout(location = 5) in uvec4 in_normal;

layout(location = 0) out vec3 v_color;
layout(location = 1) out vec2 v_uv0;
layout(location = 2) out vec3 v_normal;
layout(location = 3) out float v_lightCombined;

// Standard GL → VK clip-space conversion.
const mat4 GL_TO_VK_CLIP = mat4(
    1.0, 0.0, 0.0, 0.0,
    0.0,-1.0, 0.0, 0.0,
    0.0, 0.0, 0.5, 0.0,
    0.0, 0.0, 0.5, 1.0
);

void main() {
    gl_Position = GL_TO_VK_CLIP * pc.proj * pc.viewRot * vec4(in_pos, 1.0);

    v_color = vec3(in_color.rgb) * (1.0 / 255.0);
    v_uv0   = in_uv0;

    vec3 n = vec3(in_normal.xyz);
    n = mix(n, n - 256.0, greaterThan(n, vec3(127.0)));
    v_normal = normalize(n);

    float blockLight = float(in_uv2.x & 0xFu) / 15.0;
    float skyLight   = float(in_uv2.y & 0xFu) / 15.0;
    v_lightCombined = max(blockLight, skyLight);
}
