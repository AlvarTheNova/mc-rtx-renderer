#version 460

// Plain vertex-color passthrough with alpha modulation knocked down so debug
// boxes don't completely occlude the geometry they're highlighting.

layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 out_color;

void main() {
    out_color = vec4(v_color.rgb, v_color.a * 0.6);
}
