#version 460
#include "common.glsl"

layout(location = 0) rayPayloadInEXT RayPayload pl;

// Cheap procedural sky. Replaceable with Hillaire's analytic atmosphere in
// Phase 7 polish — for now this gives us something that looks like sky and
// includes a sun disc for primary visibility.
vec3 sample_sky(vec3 dir) {
    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 sky = mix(U.sky_color_horizon.rgb, U.sky_color_zenith.rgb, pow(t, 0.4));
    float sun = smoothstep(0.9995, 0.9999, dot(dir, U.sun_dir_ws.xyz));
    sky += vec3(15.0, 12.0, 9.0) * sun;
    return sky;
}

void main() {
    pl.radiance += pl.throughput * sample_sky(gl_WorldRayDirectionEXT);
}
