# mc-rtx-renderer — Architecture

A from-scratch rewrite of Minecraft Java Edition's renderer targeting **Vulkan 1.3 + full path tracing + DLSS 4** (Super Resolution, Ray Reconstruction, Multi-Frame Generation). Built as a Fabric mod that mixin-replaces the vanilla render path, with the hot loop in native C++.

Target: Minecraft 1.21.11 (Java), NVIDIA RTX 40/50-series (Ada/Blackwell), Windows 10/11 x64.

---

## 1. Why this is harder than it looks

1. **Voxels are not a free win for RT.** Naively triangulating each face per block gives ~30M+ triangles per render distance of 12. BVH build time dominates. Strategy must be: per-chunk BLAS + TLAS, lazy rebuild only on chunk mesh dirty, geometry deduplication across chunks via instancing where possible.
2. **MC's world is mutable every tick.** Block edits, lighting updates, redstone, fluids, entities, particles. The TLAS and per-chunk BLAS need cheap incremental updates, not full rebuilds.
3. **Java↔native boundary is a hot path.** Per-frame JNI calls must be minimized — batch everything, use direct `ByteBuffer`s for zero-copy uploads, pin world data in off-heap memory.
4. **DLSS-RR needs a *real* G-buffer.** Albedo, normal, roughness, metallic, motion vectors, specular hit distance — all per pixel, all temporally stable. Vanilla MC has none of this. We synthesize it.
5. **Motion vectors are everywhere.** Camera, entities, leaves with wind, water surface, particles, even animated textures. DLSS quality lives and dies on MV correctness.
6. **MC is obfuscated.** We use Fabric Loom + Mojang official mappings + Mixin to surgically replace `LevelRenderer`, `GameRenderer`, `WorldRenderer`. No vanilla jar editing.
7. **Streamline is C++.** Streamline 2.x SDK is the supported NVIDIA path for SR/RR/FG. We can't call it from Java directly with reasonable performance — hence the native backend.

---

## 2. High-level pipeline

```
                   ┌──────────────────────────────────────────────────────────┐
                   │                  MINECRAFT (Java, Fabric)                │
                   │                                                          │
   World tick ───► │  WorldDataExtractor (mixin)                              │
                   │    • chunk mesh dirty set                                │
                   │    • entity transform list                               │
                   │    • light data (sun angle, sky color, block light)      │
                   │    • camera pose + projection                            │
                   │                                                          │
                   │  VulkanRenderer (replaces LevelRenderer.renderLevel)     │
                   │    • marshals deltas into off-heap ByteBuffers           │
                   │    • single JNI call per frame: rtx_render_frame(...)    │
                   └────────────────────────┬─────────────────────────────────┘
                                            │ JNI (1 call/frame + async uploads)
                   ┌────────────────────────▼─────────────────────────────────┐
                   │           rtx_renderer.dll  (C++, Vulkan 1.3)            │
                   │                                                          │
                   │  ┌─────────────────┐   ┌─────────────────────────────┐   │
                   │  │ ChunkBLASCache  │   │ TLAS (rebuild/refit)        │   │
                   │  │ • BLAS per      │──►│ • chunks + entities +       │   │
                   │  │   16³ subchunk  │   │   particles + items         │   │
                   │  │ • compaction    │   └──────────┬──────────────────┘   │
                   │  └─────────────────┘              │                      │
                   │                                   ▼                      │
                   │  ┌──────────────────────────────────────────────────┐    │
                   │  │  Path tracer (VK_KHR_ray_tracing_pipeline)       │    │
                   │  │   render at upscaler input res (e.g. 1280×720)   │    │
                   │  │   1 spp primary + NEE + 1–2 bounce GI            │    │
                   │  │   outputs: color, albedo, normal, motion,        │    │
                   │  │            depth, roughness, specular hit dist   │    │
                   │  └────────────────────────┬─────────────────────────┘    │
                   │                           ▼                              │
                   │  ┌──────────────────────────────────────────────────┐    │
                   │  │  Streamline:  sl::DLSS_RR  (denoise+upscale)     │    │
                   │  │  → output at native res (e.g. 3840×2160)         │    │
                   │  └────────────────────────┬─────────────────────────┘    │
                   │                           ▼                              │
                   │  ┌──────────────────────────────────────────────────┐    │
                   │  │  Post: tonemap (ACES), bloom, exposure,          │    │
                   │  │  vignette, optional CAS sharpen                  │    │
                   │  └────────────────────────┬─────────────────────────┘    │
                   │                           ▼                              │
                   │  ┌──────────────────────────────────────────────────┐    │
                   │  │  Streamline:  sl::DLSS_G  (Multi-Frame Gen, 4x)  │    │
                   │  │  needs: HUD-less color, depth, MVs               │    │
                   │  └────────────────────────┬─────────────────────────┘    │
                   │                           ▼                              │
                   │  HUD compositor (regular raster) → swapchain present     │
                   └──────────────────────────────────────────────────────────┘
```

**Critical ordering:** RR runs *before* SR-style upscale (RR includes upscale). Tonemap runs after DLSS. Frame Gen runs *after* tonemap but *before* HUD composite — HUD is rasterized at native rate on top of generated frames to avoid HUD ghosting/jitter.

---

## 3. Subsystem design

### 3.1 World → geometry

- **Chunk decomposition.** Each 16×16×16 subchunk = one BLAS. ~24 BLAS per full column at default world height. World render dist 12 → ~9000 BLAS in flight.
- **Greedy meshing.** Coplanar same-texture faces merged into rectangles. ~5-10x triangle reduction vs. naive per-face. Reuse Sodium's algorithm as reference; reimplement in C++ for SIMD.
- **Vertex format.** Position (int16×3, chunk-local), normal (oct8×2), UV (uint16×2 atlas-relative), tangent (oct8×2 for normal-mapped textures), block-light + sky-light (uint8×2, used as emissive seed for block-light, ignored for path tracing of sky).
- **Material indirection.** Bindless `VK_EXT_descriptor_indexing` texture array. Each triangle carries a material ID → fetches albedo, normal, MER (metal/emissive/roughness) from arrays. PBR maps come from the loaded resource pack (LabPBR/SEUS-style) when present; default fallback values when absent.
- **Animated/dynamic geometry.** Water surface and leaves get vertex-shader-style displacement during BLAS *refit* (not rebuild) every frame. Refit is ~10x cheaper than rebuild.

### 3.2 BVH strategy

| Geometry class       | BLAS cadence              | TLAS treatment                            |
|----------------------|---------------------------|-------------------------------------------|
| Static chunk solids  | Build on chunk mesh dirty | Static instance, transform = chunk origin |
| Water / lava surface | Refit every frame         | Dynamic instance, flag `OPAQUE=false`     |
| Leaves (wind)        | Refit every 4 frames      | Dynamic instance                          |
| Entities (mobs)      | Skinned BLAS refit        | Dynamic, rebuilt TLAS entry per frame     |
| Items / particles    | Shared unit-quad BLAS     | Instanced with per-particle transforms    |

TLAS is *refit* when only transforms change; rebuilt when instance set changes (chunk loaded/unloaded, entity spawn/despawn). Budget: ≤2ms/frame for all AS work on 5080.

### 3.3 Lighting model

- **Direct sun/moon.** One ray to the sun cone (NEE) per pixel. Sun angle from MC's `getSunAngle()`. Atmospheric scattering via a small precomputed LUT (Bruneton-style or Hillaire's analytic).
- **Sky.** Procedural sky in miss shader, sampled by environment IS.
- **Block light.** Emissive materials (torches, lava, glowstone, beacons, redstone-on, etc.) participate in the path trace directly via an emissive triangle set sampled via Resampled Importance Sampling (ReSTIR DI). Vanilla `blockLight` channel is ignored for path-traced surfaces; we recover the look from real emitters.
- **GI.** 1 diffuse bounce + 1 specular bounce baseline. Optional 2nd bounce toggle. ReSTIR GI for temporal/spatial reuse keeps sample count at 1 spp while staying clean — DLSS-RR cleans the rest.
- **Caves/dark areas.** A floor of low-intensity ambient from sky probe to avoid pure-black artifacts when no NEE light reaches surface — kept low so torches actually matter.

### 3.4 DLSS integration (Streamline 2.x)

Streamline plugins used:
- `sl.common.dll` — interposer
- `sl.dlss.dll` — Super Resolution (only used if RR is unavailable)
- `sl.dlss_d.dll` — Ray Reconstruction (preferred path for path-traced output)
- `sl.dlss_g.dll` — Frame Generation (multi-frame on Blackwell)
- `sl.reflex.dll` — required dependency for FG; reduces input latency
- `sl.pcl.dll` — PC Latency markers

**Per-frame tag set fed to Streamline:**

| Tag                         | Source                                   | Notes |
|-----------------------------|------------------------------------------|-------|
| `kBufferTypeScalingInputColor`     | Path tracer color (HDR, pre-tonemap)     | RR wants noisy linear input |
| `kBufferTypeAlbedo`         | First-hit albedo                         | RR demodulation |
| `kBufferTypeNormals`        | First-hit world-space normal             | RR feature |
| `kBufferTypeRoughness`      | First-hit roughness                      | RR feature |
| `kBufferTypeSpecularHitDistance` | Length of first specular bounce    | RR feature |
| `kBufferTypeDepth`          | Hardware depth from raster prepass       | Native res for FG, internal res for RR |
| `kBufferTypeMotionVectors`  | Camera + per-instance motion             | Critical for both RR and FG |
| `kBufferTypeHUDLessColor`   | Tonemapped color sans HUD                | FG only |
| `kBufferTypeUIColorAndAlpha`| HUD render target                        | FG only — kept native |

`sl::Constants` filled with: jitter offset (Halton 2,3 for SR/RR, no jitter for FG-only frames), camera matrices (current + previous), near/far, FoV. `motionVectorsInvalidValue = NaN` and we clear MV with NaN — Streamline treats NaN as "no MV available."

### 3.5 Mixin surface in Java

| Vanilla class            | Replacement                              |
|--------------------------|------------------------------------------|
| `LevelRenderer`          | `@Inject(HEAD)` `renderLevel` → cancel original, dispatch to `VulkanRenderer.renderFrame()` |
| `GameRenderer`           | Bypass `renderLevel` call chain; we still want GUI/HUD to render normally |
| `WorldRenderer` chunk pipeline | Replaced wholesale; we still consume `ChunkBuilder` outputs but reroute mesh data into our own format |
| `Minecraft` (window init) | Mixin to create the VK swapchain on the GLFW window instead of GL context |
| `RenderTarget` / `Framebuffer` | Shimmed so vanilla post (entity outline, glint) renders into a VK-backed image we composite |

**Hard problem:** vanilla uses an OpenGL context bound to the GLFW window. We need to *not* create that GL context and instead create a `VK_KHR_surface` on the same `HWND`. Achievable by hooking `Window.<init>` (or `MinecraftClient.<init>`) earlier than GL setup. Mojang's `BlazeRender` / `RenderSystem` calls become no-ops or are intercepted.

### 3.6 What still renders the old way

- Main menu, loading screens, F3 overlay, inventory GUI, chat — all 2D and trivial. Keep a tiny GL or VK raster path just for `GuiGraphics`. Easiest: render GUI to a CPU-side buffer or a tiny VK pipeline and composite on top.

### 3.7 GL/VK coexistence problem (Phase 1.1 → 1.2 gate)

Vanilla MC creates an OpenGL context on the GLFW window during `Window.<init>` via `glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API)`. That HWND can host both a GL context *and* a `VK_KHR_surface` simultaneously — but only one of them can drive what's actually on screen, because both end up calling into the same window-system swap path. Last present wins. Vanilla calls `Window.swapBuffers` (→ `glfwSwapBuffers` → `SwapBuffers(hdc)`) every game frame; if we present a VK image but vanilla's GL `SwapBuffers` lands after ours, GL wins. We're invisible.

**Three possible resolutions:**

1. **Full suppress (chosen).** Mixin into `Window` early — *before* `glfwCreateWindow` is called — and force `glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API)`. No GL context ever exists. Then shim every static call in `RenderSystem`, `BufferRenderer`, `GlStateManager` to a no-op or VK redirect. Replace `Window.swapBuffers` with a VK present. This is what VulkanMod does. Cost: large mixin surface (~50–100 static methods) but mechanically straightforward.
2. **GL/VK interop.** Render VK into an external image (`VK_KHR_external_memory_win32`), import on the GL side with `GL_EXT_memory_object_win32`, blit, let GL composite + present. Cost: extra blit, dual driver memory tracking, weirdness on driver edge cases. Punted.
3. **Frame-end mixin race.** Cancel `GameRenderer.render` entirely, do everything ourselves including GUI. Cost: equivalent to option 1 plus reimplementing all of MC's 2D GUI path in VK from day one.

**Chosen implementation (Phase 1.2):** option 1 — full suppress, with a refinement to avoid the massive `RenderSystem` shim:

- `WindowMixin` redirects the `glfwCreateWindow` call inside `Window.<init>`. Before creating MC's main window with `GLFW_NO_API`, it first creates a **hidden 1×1 dummy GL window** with the same GL 3.2 core profile MC requests.
- The mixin also redirects `glfwMakeContextCurrent` in `Window.<init>` to bind the *dummy* window's GL context instead of the main window's (which has no context). MC's subsequent `GL.createCapabilities()` call succeeds normally — LWJGL's function pointers are populated against the dummy's context.
- Every `RenderSystem.*` / `GlStateManager.*` call that vanilla makes thereafter is a real GL call that lands in the dummy's 1×1 invisible framebuffer. No NPEs, no shim work, no behavior changes to vanilla code paths.
- `Window.swapBuffers` has its `glfwSwapBuffers` call no-op'd. VK present runs from `LevelRendererMixin` every game frame and now owns the visible window uncontested.

**Caveats of the dummy-context approach:**
- Vanilla `glViewport(0, 0, mainW, mainH)` calls into a 1×1 framebuffer. Undefined per spec, but every desktop GL driver tolerates oversized viewports (output gets clipped). On obscure drivers this could misbehave.
- `glReadPixels` for screenshots returns the dummy's garbage — broken until we wire MC's screenshot path through VK. Not a P0.
- We pay the cost of MC's full GL command stream every frame for nothing. Phase 1.4+ should `CANCEL_VANILLA` deeper into `GameRenderer` to short-circuit the work.

---

## 4. Performance budget (RTX 5080, 4K output, DLSS Performance = 1080p internal)

Targeting 60+ "native" fps before MFG, so MFG 4x lands at 240+.

| Stage                                | Budget    |
|--------------------------------------|-----------|
| Java side: world delta extraction    | 1.0 ms    |
| JNI + upload (async overlap)         | 0.3 ms    |
| BLAS build/refit (amortized)         | 1.5 ms    |
| TLAS rebuild/refit                   | 0.5 ms    |
| Raster G-buffer prepass (depth + MV) | 1.0 ms    |
| Primary rays + NEE                   | 3.0 ms    |
| GI (ReSTIR, 1 bounce)                | 3.5 ms    |
| Specular bounce                      | 2.0 ms    |
| DLSS-RR                              | 1.8 ms    |
| Post (tonemap, bloom)                | 0.5 ms    |
| HUD composite                        | 0.2 ms    |
| **Total**                            | **~15.3 ms** → 65 fps base |
| With DLSS MFG 4x                     | ~240 fps perceived |

Render dist 12 assumed. RD 16+ will need streaming BLAS LOD (proxy boxes for distant chunks).

---

## 5. Phased delivery — see [ROADMAP.md](ROADMAP.md)

## 6. Open problems / known unknowns

- **Stained glass and translucents.** Order-independent or proper sorted any-hit. Currently planned: split into separate TLAS pass with `OPAQUE=false`, accumulate with peeled blending. Path-traced refractive correctness costs more rays.
- **Beacons / light shafts.** Volumetric scattering needs froxel or ray-marched volume — not in v1.
- **Shaderpack compatibility.** Iris-style `.shader` files are tied to a rasterizer. We won't be drop-in compatible. Resource packs (textures, PBR maps) we *do* consume.
- **Mod compatibility.** Any mod that registers custom block renderers via `BlockEntityRenderer` (e.g. chests, signs with rendered text, mod machines) needs adapter code to extract geometry. Start with vanilla-only; mod-compat is a v2 problem.
- **Memory.** A 5080 has 16 GB. World BVH + textures + framebuffers + Streamline working set: comfortably fits at RD 12. RD 24 with 4K textures may OOM — needs LOD/streaming.
- **Mojang ToS.** This is a client-side mod that loads alongside MC; it does not redistribute MC bytecode. Same legal footing as Sodium/Iris/OptiFine. Fine.
