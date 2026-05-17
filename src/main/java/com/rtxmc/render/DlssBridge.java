package com.rtxmc.render;

/**
 * Java-side configuration of DLSS settings. The actual Streamline calls live
 * in {@code native/src/streamline_integration.cpp}; this class only forwards
 * user choices.
 */
public final class DlssBridge {

    public enum SrPreset { OFF, DLAA, QUALITY, BALANCED, PERFORMANCE, ULTRA_PERFORMANCE }
    public enum RrPreset { OFF, ON }
    public enum FgFactor { OFF, X2, X3, X4 }

    private DlssBridge() {}

    public static native void setSuperResolution(int preset);
    public static native void setRayReconstruction(int preset);
    public static native void setFrameGeneration(int factor);

    public static void apply(SrPreset sr, RrPreset rr, FgFactor fg) {
        setSuperResolution(sr.ordinal());
        setRayReconstruction(rr.ordinal());
        setFrameGeneration(fg.ordinal());
    }
}
