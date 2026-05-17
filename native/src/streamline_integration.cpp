#include "streamline_integration.h"
#include "vulkan_context.h"
#include "path_tracer.h"

#include <cstdio>

#if RTXMC_HAVE_STREAMLINE
  #include <sl.h>
  #include <sl_consts.h>
  #include <sl_dlss.h>
  #include <sl_dlss_d.h>
  #include <sl_dlss_g.h>
  #include <sl_reflex.h>
#endif

namespace rtxmc {

namespace {
Streamline g_sl;
} // namespace

Streamline& sl() { return g_sl; }

bool Streamline::init(VkInstance inst, VkDevice dev, VkPhysicalDevice phys) {
    (void)inst; (void)dev; (void)phys;
#if RTXMC_HAVE_STREAMLINE
    sl::Preferences prefs{};
    prefs.showConsole = false;
    prefs.logLevel    = sl::LogLevel::eDefault;
    prefs.numPathsToPlugins = 0;
    prefs.flags = sl::PreferenceFlags::eUseManualHooking
                | sl::PreferenceFlags::eAllowOTA;
    prefs.renderAPI = sl::RenderAPI::eVulkan;
    prefs.engine    = sl::EngineType::eCustom;
    prefs.engineVersion = "0.0.1";
    prefs.projectId     = "com.rtxmc.minecraft";

    static const sl::Feature feats[] = {
        sl::kFeatureDLSS,
        sl::kFeatureDLSS_RR,
        sl::kFeatureDLSS_G,
        sl::kFeatureReflex,
        sl::kFeaturePCL,
    };
    prefs.featuresToLoad     = feats;
    prefs.numFeaturesToLoad  = sizeof(feats)/sizeof(feats[0]);

    if (SL_FAILED(res, slInit(prefs))) {
        std::fprintf(stderr, "rtxmc: slInit failed: %d\n", (int)res);
        return false;
    }

    // Plug Streamline into Vulkan device/instance creation hooks. With
    // eUseManualHooking we hand it the already-created handles so MC's window
    // isn't disturbed.
    if (SL_FAILED(res, slSetVulkanInfo(inst, dev, phys))) {
        std::fprintf(stderr, "rtxmc: slSetVulkanInfo failed: %d\n", (int)res);
        return false;
    }

    sl_loaded_ = true;
    return true;
#else
    std::fprintf(stderr, "rtxmc: built without Streamline — DLSS disabled\n");
    return false;
#endif
}

void Streamline::set_sr_preset(int preset)   { sr_preset_  = preset; }
void Streamline::set_rr_enabled(int enabled) { rr_enabled_ = enabled != 0; }
void Streamline::set_fg_factor(int factor)   { fg_factor_  = factor; }

void Streamline::evaluate_rr(VkCommandBuffer cmd, const DlssInputs& in) {
    (void)cmd; (void)in;
#if RTXMC_HAVE_STREAMLINE
    if (!sl_loaded_ || !rr_enabled_) return;

    // TODO Phase 4:
    //   1. Fill sl::Constants:
    //        cameraViewToClip, clipToCameraView, prev*, jitterOffset, MVP,
    //        cameraPinholeOffset, cameraPos, cameraFwd, cameraUp,
    //        cameraNear, cameraFar, cameraFOV, motionVectorsInvalidValue=NaN,
    //        reset = (first frame after resize/teleport).
    //   2. slSetConstants(consts, frameToken, viewport_id_);
    //   3. Tag buffers: color, albedo, normals, roughness, specular hit dist,
    //      motion vectors, depth — via slSetTag with kBufferType* enums.
    //   4. sl::DLSSDOptions for output res, preset, sharpness=0.
    //      slDLSSDSetOptions(viewport_id_, opts);
    //   5. slEvaluateFeature(sl::kFeatureDLSS_RR, frameToken, viewport_id_, cmd).
#endif
}

void Streamline::evaluate_fg(VkCommandBuffer cmd, const DlssInputs& in) {
    (void)cmd; (void)in;
#if RTXMC_HAVE_STREAMLINE
    if (!sl_loaded_ || fg_factor_ < 2) return;

    // TODO Phase 6:
    //   1. Tag kBufferTypeHUDLessColor, kBufferTypeUIColorAndAlpha,
    //      kBufferTypeDepth (native), kBufferTypeMotionVectors (native).
    //   2. sl::DLSSGOptions opts; opts.mode = eOn; opts.numFramesToGenerate = fg_factor_ - 1;
    //      slDLSSGSetOptions(viewport_id_, opts).
    //   3. slEvaluateFeature(sl::kFeatureDLSS_G, frameToken, viewport_id_, cmd).
    //   4. Reflex markers around the present call (sl::ReflexMarker::ePresentStart/End).
#endif
}

void Streamline::destroy() {
#if RTXMC_HAVE_STREAMLINE
    if (sl_loaded_) slShutdown();
    sl_loaded_ = false;
#endif
}

} // namespace rtxmc
