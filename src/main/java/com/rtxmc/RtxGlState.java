package com.rtxmc;

/**
 * Shared state between {@code WindowMixin} (creates the dummy GL window) and
 * {@code GlBackendMixin} (binds it as the current GL context). Lives outside
 * a mixin class because mixin-private fields are awkward to read from other
 * mixin classes.
 */
public final class RtxGlState {
    private static volatile long dummyGlWindow = 0L;

    private RtxGlState() {}

    public static long dummyGlWindow()        { return dummyGlWindow; }
    public static void setDummyGlWindow(long h) { dummyGlWindow = h; }
}
