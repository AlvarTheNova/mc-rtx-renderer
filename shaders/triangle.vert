#version 460

// Phase 1.3 sanity-check triangles. Two of them:
//
//   - gl_VertexIndex 0-2: world-space triangle at (0, 100, 0), driven by MC's
//     view + proj matrices through push constants. Pre-multiplied by the
//     standard GL→VK clip-space conversion (Y-flip + Z remap [-1,1]→[0,1])
//     since MC's projection is GL-convention.
//
//   - gl_VertexIndex 3-5: NDC-space triangle pinned to the top-left corner
//     regardless of camera. Acts as a fixed sentinel — if this is visible but
//     the world triangle isn't, the issue is matrix/position, not pipeline.

layout(push_constant) uniform PC {
    mat4 view;
    mat4 proj;
} pc;

layout(location = 0) out vec3 v_color;

// GL clip space → VK clip space. Constructor is column-major, so each
// indented line is a COLUMN of the matrix:
//   row 0: (1,  0,   0,   0)
//   row 1: (0, -1,   0,   0)   ← Y flip (VK NDC y points down)
//   row 2: (0,  0,  0.5, 0.5)  ← Z remap [-1,1] → [0,1]
//   row 3: (0,  0,   0,   1)
const mat4 GL_TO_VK_CLIP = mat4(
    1.0, 0.0, 0.0, 0.0,    // col 0
    0.0,-1.0, 0.0, 0.0,    // col 1
    0.0, 0.0, 0.5, 0.0,    // col 2
    0.0, 0.0, 0.5, 1.0     // col 3
);

const vec3 WORLD_POSITIONS[3] = vec3[](
    vec3(-5.0, 100.0,  0.0),
    vec3( 5.0, 100.0,  0.0),
    vec3( 0.0, 110.0,  0.0)
);
const vec3 WORLD_COLORS[3] = vec3[](
    vec3(1.0, 0.2, 0.2),
    vec3(0.2, 1.0, 0.2),
    vec3(0.2, 0.4, 1.0)
);

// VK NDC: x in [-1,1] left→right, y in [-1,1] top→bottom (down-positive).
// So (-0.95, -0.95) is top-left corner.
const vec2 NDC_POSITIONS[3] = vec2[](
    vec2(-0.95, -0.95),
    vec2(-0.45, -0.95),
    vec2(-0.70, -0.55)
);
const vec3 NDC_COLORS[3] = vec3[](
    vec3(1.0, 1.0, 0.0),
    vec3(1.0, 0.5, 0.0),
    vec3(1.0, 1.0, 1.0)
);

void main() {
    if (gl_VertexIndex < 3) {
        int i = gl_VertexIndex;
        vec4 world = vec4(WORLD_POSITIONS[i], 1.0);
        gl_Position = GL_TO_VK_CLIP * pc.proj * pc.view * world;
        v_color = WORLD_COLORS[i];
    } else {
        int i = gl_VertexIndex - 3;
        gl_Position = vec4(NDC_POSITIONS[i], 0.5, 1.0);
        v_color = NDC_COLORS[i];
    }
}
