#version 460

// Phase 1.4.2 — first-light chunk rasterisation. Consumes MC 1.21.11's blessed
// 32-byte SOLID-layer vertex format verbatim:
//
//   offset 0  : position    vec3 f32       (12 B)
//   offset 12 : color       uvec4 u8 RGBA  ( 4 B)   — biome tint, white otherwise
//   offset 16 : uv0         vec2 f32       ( 8 B)   — block atlas (ignored for now)
//   offset 24 : uv2         uvec2 u16      ( 4 B)   — block-light + sky-light (low nibble)
//   offset 28 : normal      uvec4 u8       ( 4 B)   — packed normal
//
// Section-local positions are in block units [0, 16]. We add the section's
// world offset (sectionPos.xyz * 16) before applying view+proj.

layout(push_constant) uniform PC {
    mat4  view;
    mat4  proj;
    ivec4 sectionPos;     // .xyz = chunk-section coords, .w = unused
} pc;

layout(location = 0) in vec3  in_pos;
layout(location = 1) in uvec4 in_color;
layout(location = 2) in vec2  in_uv0;
layout(location = 3) in uvec2 in_uv2;
layout(location = 4) in uvec4 in_normal;

layout(location = 0) out vec3 v_color;
layout(location = 1) out vec2 v_uv0;
layout(location = 2) out vec3 v_normal;
layout(location = 3) out float v_lightCombined;

// Standard GL → VK clip-space conversion. Same as triangle.vert.
const mat4 GL_TO_VK_CLIP = mat4(
    1.0, 0.0, 0.0, 0.0,
    0.0,-1.0, 0.0, 0.0,
    0.0, 0.0, 0.5, 0.0,
    0.0, 0.0, 0.5, 1.0
);

void main() {
    // Section-local block coords → world coords.
    vec3 world = in_pos + vec3(pc.sectionPos.xyz * 16);
    gl_Position = GL_TO_VK_CLIP * pc.proj * pc.view * vec4(world, 1.0);

    // Color: u8 RGBA → linear [0,1].
    v_color = vec3(in_color.rgb) * (1.0 / 255.0);
    v_uv0   = in_uv0;

    // Normal: i8 packed in low 24 bits, treat as signed byte → [-1, 1].
    // (We treat the input as uvec4, so reinterpret components > 127 as negative.)
    vec3 n = vec3(in_normal.xyz);
    n = mix(n, n - 256.0, greaterThan(n, vec3(127.0)));
    v_normal = normalize(n);

    // UV2 holds block-light (low nibble of .x) and sky-light (low nibble of .y),
    // each in [0, 15]. We collapse to a single value for now — proper lightmap
    // sampling comes when we ingest MC's lightmap texture.
    float blockLight = float(in_uv2.x & 0xFu) / 15.0;
    float skyLight   = float(in_uv2.y & 0xFu) / 15.0;
    v_lightCombined = max(blockLight, skyLight);
}
