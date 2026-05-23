#include "jni_bridge.h"
#include "rtx_renderer.h"
#include "vk_resources.h"
#include "vk_shaderc.h"
#include "vk_pipeline.h"

#include <cstring>
#include <cstdio>

namespace {
const JNINativeMethod kBridgeMethods[] = {
    {(char*)"init",         (char*)"(JII)I",                          (void*)rtxmc_native_init},
    {(char*)"resize",       (char*)"(II)V",                           (void*)rtxmc_native_resize},
    {(char*)"uploadChunk",  (char*)"(IIIILjava/nio/ByteBuffer;)V",   (void*)rtxmc_native_uploadChunk},
    {(char*)"removeChunk",  (char*)"(III)V",                          (void*)rtxmc_native_removeChunk},
    {(char*)"uploadBlockAtlas", (char*)"(IILjava/nio/ByteBuffer;)V",  (void*)rtxmc_native_uploadBlockAtlas},
    {(char*)"uploadEntityBatch", (char*)"(IILjava/nio/ByteBuffer;)V", (void*)rtxmc_native_uploadEntityBatch},
    {(char*)"renderFrame",  (char*)"(Ljava/nio/ByteBuffer;)V",        (void*)rtxmc_native_renderFrame},
    {(char*)"shutdown",     (char*)"()V",                             (void*)rtxmc_native_shutdown},
};

const JNINativeMethod kDlssMethods[] = {
    {(char*)"setSuperResolution",   (char*)"(I)V", (void*)rtxmc_dlss_setSr},
    {(char*)"setRayReconstruction", (char*)"(I)V", (void*)rtxmc_dlss_setRr},
    {(char*)"setFrameGeneration",   (char*)"(I)V", (void*)rtxmc_dlss_setFg},
};

// Note: VkResNative methods are NOT registered here. They use JNI-standard
// `Java_com_rtxmc_gpu_VkResNative_*` symbol names that the JVM auto-resolves
// on first call. Manual RegisterNatives via FindClass("com/rtxmc/gpu/...")
// returned null at JNI_OnLoad time under Fabric's Knot classloader (class
// not yet loaded). Standard-name binding sidesteps the chicken-and-egg.
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
    // VkResNative is bound by JNI-standard symbol naming, not RegisterNatives.
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

JNIEXPORT void JNICALL rtxmc_native_uploadEntityBatch(JNIEnv* env, jclass,
                                                      jint layer_hash, jint vertex_count,
                                                      jobject verts) {
    void* p = env->GetDirectBufferAddress(verts);
    auto bytes = (uint32_t)env->GetDirectBufferCapacity(verts);
    rtxmc::rtx_upload_entity_batch(layer_hash, (uint32_t)vertex_count, p, bytes);
}

// ---- DLSS ------------------------------------------------------------------

JNIEXPORT void JNICALL rtxmc_dlss_setSr(JNIEnv*, jclass, jint p) { rtxmc::rtx_set_super_resolution(p); }
JNIEXPORT void JNICALL rtxmc_dlss_setRr(JNIEnv*, jclass, jint p) { rtxmc::rtx_set_ray_reconstruction(p); }
JNIEXPORT void JNICALL rtxmc_dlss_setFg(JNIEnv*, jclass, jint f) { rtxmc::rtx_set_frame_generation(f); }

// ---- VkBackend resource primitives ----------------------------------------
// JNI-standard names so the JVM auto-binds by symbol lookup. See header
// comment on why these don't use the manual RegisterNatives path.

extern "C" JNIEXPORT jlong JNICALL
Java_com_rtxmc_gpu_VkResNative_createBuffer(JNIEnv* env, jclass,
                                            jint usage, jlong size, jobject initial) {
    const void* data = nullptr;
    if (initial) data = env->GetDirectBufferAddress(initial);
    return (jlong)rtxmc::vkres_create_buffer((uint32_t)usage, (uint64_t)size, data);
}
extern "C" JNIEXPORT void JNICALL
Java_com_rtxmc_gpu_VkResNative_destroyBuffer(JNIEnv*, jclass, jlong h) {
    rtxmc::vkres_destroy_buffer((uint64_t)h);
}
extern "C" JNIEXPORT jobject JNICALL
Java_com_rtxmc_gpu_VkResNative_mapBuffer(JNIEnv* env, jclass,
                                         jlong h, jlong off, jlong len) {
    void* p = rtxmc::vkres_map_buffer((uint64_t)h, (uint64_t)off, (uint64_t)len);
    if (!p) return nullptr;
    auto ref = rtxmc::vkres_buffer_ref((uint64_t)h);
    jlong cap = (len == 0) ? (jlong)(ref.size - off) : len;
    return env->NewDirectByteBuffer(p, cap);
}
extern "C" JNIEXPORT void JNICALL
Java_com_rtxmc_gpu_VkResNative_unmapBuffer(JNIEnv*, jclass, jlong h) {
    rtxmc::vkres_unmap_buffer((uint64_t)h);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_rtxmc_gpu_VkResNative_createTexture(JNIEnv*, jclass,
                                             jint usage, jint fmt,
                                             jint w, jint h, jint depth, jint mips) {
    return (jlong)rtxmc::vkres_create_texture((uint32_t)usage, (uint32_t)fmt,
                                              (uint32_t)w, (uint32_t)h,
                                              (uint32_t)depth, (uint32_t)mips);
}
extern "C" JNIEXPORT void JNICALL
Java_com_rtxmc_gpu_VkResNative_destroyTexture(JNIEnv*, jclass, jlong h) {
    rtxmc::vkres_destroy_texture((uint64_t)h);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_rtxmc_gpu_VkResNative_createTextureView(JNIEnv*, jclass,
                                                 jlong tex, jint baseMip, jint mips) {
    return (jlong)rtxmc::vkres_create_texture_view((uint64_t)tex,
                                                   (uint32_t)baseMip, (uint32_t)mips);
}
extern "C" JNIEXPORT void JNICALL
Java_com_rtxmc_gpu_VkResNative_destroyTextureView(JNIEnv*, jclass, jlong h) {
    rtxmc::vkres_destroy_texture_view((uint64_t)h);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_rtxmc_gpu_VkResNative_createSampler(JNIEnv*, jclass,
                                             jint u, jint v,
                                             jint minF, jint magF, jint aniso) {
    return (jlong)rtxmc::vkres_create_sampler((uint32_t)u, (uint32_t)v,
                                              (uint32_t)minF, (uint32_t)magF,
                                              (uint32_t)aniso);
}
extern "C" JNIEXPORT void JNICALL
Java_com_rtxmc_gpu_VkResNative_destroySampler(JNIEnv*, jclass, jlong h) {
    rtxmc::vkres_destroy_sampler((uint64_t)h);
}

// ---- shaderc test entry point (1.6.1d) ------------------------------------
// Compiles a GLSL source string to SPIR-V; returns the word count (or
// negative on failure). Used by the Java side to smoke-test the shaderc
// toolchain at startup. Returns -1 on compile error, -2 on bad stage.
extern "C" JNIEXPORT jint JNICALL
Java_com_rtxmc_gpu_VkResNative_testCompileShader(JNIEnv* env, jclass,
                                                  jstring jsource, jint stage,
                                                  jstring jlabel) {
    if (stage < 0 || stage > 1) return -2;
    const char* source = env->GetStringUTFChars(jsource, nullptr);
    const char* label  = jlabel ? env->GetStringUTFChars(jlabel, nullptr) : "test";
    auto spirv = rtxmc::shaderc_compile(
            source ? source : "",
            (rtxmc::ShaderStage)stage,
            label);
    if (source) env->ReleaseStringUTFChars(jsource, source);
    if (jlabel && label) env->ReleaseStringUTFChars(jlabel, label);
    return spirv.empty() ? -1 : (jint)spirv.size();
}

// ---- Pipeline smoke test (1.6.1d step 2) -----------------------------------
// Builds a tiny pipeline with hardcoded TRIANGLE_LIST + alpha blend + Position-
// only vertex format. Validates the full GLSL → SPIR-V → VkGraphicsPipeline
// path without needing real RenderPipeline state. Returns the handle on
// success, 0 on failure.
extern "C" JNIEXPORT jlong JNICALL
Java_com_rtxmc_gpu_VkResNative_testCreatePipeline(JNIEnv* env, jclass,
                                                  jstring jvert, jstring jfrag) {
    const char* vert = env->GetStringUTFChars(jvert, nullptr);
    const char* frag = env->GetStringUTFChars(jfrag, nullptr);

    rtxmc::VkPipelineSpec spec{};
    spec.topology         = 4;  // TRIANGLES
    spec.polygon_mode     = 0;  // FILL
    spec.cull             = 0;  // no cull
    spec.depth_test_func  = 0;  // NO_DEPTH_TEST
    spec.write_depth      = 0;
    spec.write_color      = 1;
    spec.write_alpha      = 1;
    spec.blend_enabled    = 1;
    spec.blend_src_color  = 11; // SRC_ALPHA
    spec.blend_dst_color  = 9;  // ONE_MINUS_SRC_ALPHA
    spec.blend_src_alpha  = 4;  // ONE
    spec.blend_dst_alpha  = 13; // ZERO (DstFactor ord 13)
    spec.vertex_stride    = 12; // vec3 position
    spec.attrs.push_back({0, 0, 3, 0}); // location 0, type=FLOAT, count=3, offset=0
    spec.color_format_code = 0;  // RGBA8
    spec.depth_attachment  = 0;  // no depth

    uint64_t h = rtxmc::vkpipe_create(
            vert ? vert : "", frag ? frag : "",
            spec, "smoke-test-pipeline");

    if (vert) env->ReleaseStringUTFChars(jvert, vert);
    if (frag) env->ReleaseStringUTFChars(jfrag, frag);
    return (jlong)h;
}
extern "C" JNIEXPORT void JNICALL
Java_com_rtxmc_gpu_VkResNative_destroyPipeline(JNIEnv*, jclass, jlong h) {
    rtxmc::vkpipe_destroy((uint64_t)h);
}
