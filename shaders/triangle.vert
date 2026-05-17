#version 460

// Phase 1.3 sanity-check triangle. Vertices and colors are baked into the
// shader so we don't need to wire a vertex buffer yet; that lands in 1.4
// with the chunk rasterizer.
//
// Triangle sits at world (0, 100, 0), ~10m wide. Two-sided in the pipeline
// state so you can find it from any direction. The clear color is dark grey
// so the colored triangle pops.

layout(push_constant) uniform PC {
    mat4 view;
    mat4 proj;
} pc;

layout(location = 0) out vec3 v_color;

const vec3 POSITIONS[3] = vec3[](
    vec3(-5.0, 100.0,  0.0),
    vec3( 5.0, 100.0,  0.0),
    vec3( 0.0, 110.0,  0.0)
);

const vec3 COLORS[3] = vec3[](
    vec3(1.0, 0.2, 0.2),
    vec3(0.2, 1.0, 0.2),
    vec3(0.2, 0.4, 1.0)
);

void main() {
    vec4 world = vec4(POSITIONS[gl_VertexIndex], 1.0);
    gl_Position = pc.proj * pc.view * world;
    v_color = COLORS[gl_VertexIndex];
}
