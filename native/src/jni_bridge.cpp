#include "jni_bridge.h"
#include "rtx_renderer.h"

#include <cstring>
#include <cstdio>

namespace {
const JNINativeMethod kBridgeMethods[] = {
    {(char*)"init",         (char*)"(JII)I",                          (void*)rtxmc_native_init},
    {(char*)"resize",       (char*)"(II)V",                           (void*)rtxmc_native_resize},
    {(char*)"uploadChunk",  (char*)"(IIIILjava/nio/ByteBuffer;)V",   (void*)rtxmc_native_uploadChunk},
    {(char*)"removeChunk",  (char*)"(III)V",                          (void*)rtxmc_native_removeChunk},
    {(char*)"uploadBlockAtlas", (char*)"(IILjava/nio/ByteBuffer;)V",  (void*)rtxmc_native_uploadBlockAtlas},
    {(char*)"renderFrame",  (char*)"(Ljava/nio/ByteBuffer;)V",        (void*)rtxmc_native_renderFrame},
    {(char*)"shutdown",     (char*)"()V",                             (void*)rtxmc_native_shutdown},
};

const JNINativeMethod kDlssMethods[] = {
    {(char*)"setSuperResolution",   (char*)"(I)V", (void*)rtxmc_dlss_setSr},
    {(char*)"setRayReconstruction", (char*)"(I)V", (void*)rtxmc_dlss_setRr},
    {(char*)"setFrameGeneration",   (char*)"(I)V", (void*)rtxmc_dlss_setFg},
};
} // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_21) != JNI_OK) return JNI_ERR;

    if (jclass c = env->FindClass("com/rtxmc/render/NativeBridge")) {
        env->RegisterNatives(c, kBridgeMethods, sizeof(kBridgeMethods) / sizeof(JNINativeMethod));
    } else {
        std::fprintf(stderr, "rtxmc: NativeBridge class not found\n");
        return JNI_ERR;
    }
    if (jclass c = env->FindClass("com/rtxmc/render/DlssBridge")) {
        env->RegisterNatives(c, kDlssMethods, sizeof(kDlssMethods) / sizeof(JNINativeMethod));
    }
    return JNI_VERSION_21;
}

// ---- Lifecycle -------------------------------------------------------------

JNIEXPORT jint JNICALL rtxmc_native_init(JNIEnv*, jclass, jlong hwnd, jint w, jint h) {
    return rtxmc::rtx_init(reinterpret_cast<void*>(hwnd), w, h);
}

JNIEXPORT void JNICALL rtxmc_native_resize(JNIEnv*, jclass, jint w, jint h) {
    rtxmc::rtx_resize(w, h);
}

JNIEXPORT void JNICALL rtxmc_native_shutdown(JNIEnv*, jclass) {
    rtxmc::rtx_shutdown();
}

// ---- Per-frame -------------------------------------------------------------

JNIEXPORT void JNICALL rtxmc_native_renderFrame(JNIEnv* env, jclass, jobject params) {
    void* ptr = env->GetDirectBufferAddress(params);
    if (!ptr) return;
    rtxmc::rtx_render_frame(*reinterpret_cast<const rtxmc::FrameParams*>(ptr));
}

// ---- Chunk uploads ---------------------------------------------------------

JNIEXPORT void JNICALL rtxmc_native_uploadChunk(
        JNIEnv* env, jclass,
        jint cx, jint cy, jint cz, jint layer,
        jobject verts) {
    void* vp = env->GetDirectBufferAddress(verts);
    auto vlen = (uint32_t)env->GetDirectBufferCapacity(verts);
    rtxmc::rtx_upload_chunk(cx, cy, cz, layer, vp, vlen);
}

JNIEXPORT void JNICALL rtxmc_native_removeChunk(JNIEnv*, jclass, jint cx, jint cy, jint cz) {
    rtxmc::rtx_remove_chunk(cx, cy, cz);
}

JNIEXPORT void JNICALL rtxmc_native_uploadBlockAtlas(JNIEnv* env, jclass,
                                                     jint w, jint h, jobject pixels) {
    void* p = env->GetDirectBufferAddress(pixels);
    auto bytes = (uint32_t)env->GetDirectBufferCapacity(pixels);
    rtxmc::rtx_upload_block_atlas(w, h, p, bytes);
}

// ---- DLSS ------------------------------------------------------------------

JNIEXPORT void JNICALL rtxmc_dlss_setSr(JNIEnv*, jclass, jint p) { rtxmc::rtx_set_super_resolution(p); }
JNIEXPORT void JNICALL rtxmc_dlss_setRr(JNIEnv*, jclass, jint p) { rtxmc::rtx_set_ray_reconstruction(p); }
JNIEXPORT void JNICALL rtxmc_dlss_setFg(JNIEnv*, jclass, jint f) { rtxmc::rtx_set_frame_generation(f); }
