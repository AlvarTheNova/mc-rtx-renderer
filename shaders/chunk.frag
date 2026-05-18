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
    // Diagnostic: visualize UV0 magnitude. If UV0 is in [0,1] this renders
    // dark blocks (low values). If UV0 is in pixel space [0, 2048] this
    // renders bright/saturated gradients across faces. Reveals which.
    //
    // To re-enable atlas sampling once UV space is understood, replace
    // the discard/out_color below with:
    //    vec4 tex = texture(u_atlas, v_uv0);  // or v_uv0 / vec2(2048.0)
    //    if (tex.a < 0.05) discard;
    //    float ndotl = max(dot(v_normal, SUN_DIR), 0.0);
    //    float lighting = 0.25 + 0.5 * ndotl + 0.25 * v_lightCombined;
    //    out_color = vec4(tex.rgb * v_color * lighting, tex.a);

    // R = U mapped through fract(), G = V mapped through fract(), B = 0
    //  → If UV0 already in [0,1], we see fract(uv) ≈ uv = smooth gradient
    //    on each face from (0,0) to (1,1).
    //  → If UV0 in pixel space (0-2048), fract(uv) = uv mod 1.0 is jagged
    //    pseudo-random per face. Looks noisy/wrong.
    // We also show the raw UV magnitude in the alpha channel via discard.
    vec2 uv = v_uv0;
    float dist_from_origin = length(uv);

    if (dist_from_origin > 10.0) {
        // UV is way bigger than [0,1] range → MUST be pixel space
        // Show normalized to atlas (assuming 2048) in green
        vec2 norm_uv = uv / 2048.0;
        out_color = vec4(0.0, fract(norm_uv.x + norm_uv.y), 0.0, 1.0);
    } else {
        // UV is small → likely already [0,1]. Show as RG gradient.
        out_color = vec4(uv.x, uv.y, 0.5, 1.0);
    }
}
