#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

namespace rtxmc {

// Per-frame inputs the path tracer hands to Streamline. Pointers are owned
// by PathTracer::gbuffer(); we just bind them in tag form.
struct DlssInputs {
    VkImage  color_hdr;            // pre-tonemap, internal res
    VkImage  albedo;
    VkImage  normal_ws;
    VkImage  roughness_metallic;
    VkImage  specular_hit_dist;
    VkImage  motion_vectors;
    VkImage  depth_internal;
    VkImage  depth_native;
    VkImage  hudless_color;        // post-tonemap, native res — FG input
    VkImage  ui_color;             // HUD render target — FG ignores movement

    VkExtent2D internal_extent;
    VkExtent2D native_extent;

    float jitter_x, jitter_y;      // pixels, Halton sequence
    float view[16];
    float proj[16];
    float view_prev[16];
    float proj_prev[16];
    float near_plane;
    float far_plane;
    float fov_y_radians;
    float ms_since_last_frame;
};

class Streamline {
public:
    bool init(VkInstance, VkDevice, VkPhysicalDevice);
    void destroy();

    void set_sr_preset(int preset);   // 0=off,1=DLAA,2=Q,3=B,4=P,5=UP
    void set_rr_enabled(int enabled); // 0/1
    void set_fg_factor(int factor);   // 0=off,2,3,4

    // Tag G-buffer inputs for current frame and dispatch DLSS-RR / SR.
    // Must be called between path-trace record and post-process.
    void evaluate_rr(VkCommandBuffer cmd, const DlssInputs& in);

    // Dispatch DLSS Frame Generation. Must be called after tonemap and
    // before HUD composite.
    void evaluate_fg(VkCommandBuffer cmd, const DlssInputs& in);

    bool rr_enabled() const { return rr_enabled_; }
    int  fg_factor() const  { return fg_factor_; }

private:
    bool sl_loaded_  = false;
    int  sr_preset_  = 0;
    bool rr_enabled_ = false;
    int  fg_factor_  = 0;
    uint32_t viewport_id_ = 0;
};

Streamline& sl();

} // namespace rtxmc
