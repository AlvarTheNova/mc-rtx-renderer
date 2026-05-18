# Roadmap

Phased plan. Each phase is shippable on its own — even if you stop at phase 3 you have a working Vulkan renderer with DLSS-SR.

## Phase 0 — Scaffolding (this commit)
- [x] Project layout
- [x] Architecture doc (DESIGN.md)
- [x] Fabric Loom build config
- [x] Mixin skeletons for `LevelRenderer`, `MinecraftClient`
- [x] JNI bridge header + Java side
- [x] Native CMake project
- [x] Streamline integration skeleton
- [x] RT shader skeletons (rgen/rmiss/rchit)

## Phase 1 — Vulkan substitution, raster only

Realistic subdivision — original "Phase 1" was 2-3 months of work, not weeks.

### 1.1 — VK plumbing proof-of-life (~1 day)  ◄ current
- [x] Extract HWND from GLFW window via `GLFWNativeWin32`
- [x] Per-frame swapchain acquire / clear / present with proper semaphores + fences
- [x] Animated clear color so a visual artifact would prove VK is alive *if* GL weren't fighting it
- [x] Verbose init logging (device name, queue families, swapchain format, image count)
- [x] **Exit:** MC boots, native log shows full VK init success, swapchain frames are dispatched every game frame (GL still wins the screen — that's 1.2's job)

### 1.2 — GL suppression  ✓ done
- [x] `WindowMixin` redirects `glfwCreateWindow` → main window with `GLFW_NO_API`
- [x] Hidden 1×1 dummy GL window absorbs MC's `GL.createCapabilities()` + every subsequent `RenderSystem`/`GlStateManager` call (avoids the comprehensive shim)
- [x] `glfwMakeContextCurrent` redirected to bind dummy context
- [x] `Window.swapBuffers` → `glfwSwapBuffers` no-op'd; VK present (from `LevelRendererMixin`) becomes the only present path
- [x] `CANCEL_VANILLA = true` in `LevelRendererMixin` so vanilla world render doesn't waste cycles painting to the invisible dummy framebuffer
- [x] **First-boot validated:** MC boots cleanly, three mixins (`WindowMixin`, `GlBackendMixin`, `RenderSystemMixin`) all apply, dummy ctx absorbs GL traffic, dark slate clear color visible everywhere, no crashes. Mojang refactored GL bring-up across `Window`/`GlBackend`/`RenderSystem` in 1.21.11 — three mixins now do the work that one tried to in earlier drafts.

### 1.3 — Raster triangle  ✓ done
- [x] GLSL → SPIR-V at native build time (CMake `add_spirv_shader` via `glslangValidator --vn`, embedded as C arrays)
- [x] VK 1.3 dynamic rendering — no render pass / framebuffer needed
- [x] Push-constant view + proj matrices (avoids descriptor-set boilerplate; 128 bytes, sits right at the minimum guarantee)
- [x] Vertices baked into shader via `gl_VertexIndex` (no vertex buffer yet)
- [x] Two-sided triangle at world `(0, 100, 0)`, ~10m wide, RGB-shaded
- [x] NDC-space sentinel triangle in top-left for pipeline-vs-matrix disambiguation
- [x] `dynamicRendering` + `synchronization2` enabled at device creation (VkPhysicalDeviceVulkan13Features)
- [x] GL→VK clip-space conversion (Y flip + Z [-1,1]→[0,1]) in shader
- [x] View matrix reconstructed Java-side as `positionMatrix * translate(-camPos)` — MC 1.21.11's `positionMatrix` is rotation-only, doesn't include camera translation (Mojang does that per-chunk)
- [x] **First-boot validated:** NDC sentinel + world triangle both visible from MC camera at `/tp @s 0 95 5`

### 1.4 — Chunk rasterizer

#### 1.4.1 — Section mesh plumbing  ✓ done
- [x] Identify 1.21.11's mesh emit point: `SectionBuilder.build(...) → RenderData{ Map<BlockRenderLayer, BuiltBuffer> }`
- [x] `SectionBuilderMixin` `@Inject` at RETURN; copy SOLID-layer bytes (BuiltBuffer is allocator-backed and freed on `RenderData.close`)
- [x] Forward through JNI to `BvhStore::upload_chunk` per-section
- [x] Native side thread-safe (`std::mutex` — meshing runs on worker threads)
- [x] One-shot log budget (8 sections) on both sides
- [x] **First-boot validated:** worker threads delivered 8 sections, each 1024 verts × 32 B (vertex format `[Position, Color, UV0, UV2, Normal]`, QUADS mode, index count 1.5× vert count). Per-section coords sensible (-6..0 around player position).
- [x] **Gotcha learned:** `CANCEL_VANILLA = true` in `LevelRendererMixin` is too aggressive — `WorldRenderer.render` is where vanilla schedules rebuild tasks. Reverted to `false` until we own chunk rendering and can schedule rebuilds independently.

#### 1.4.2 — First-light chunk render  ✓ done
- [x] Consume MC's vertex format directly (no repack): pos f32×3 / color u8×4 / UV0 f32×2 / UV2 u16×2 light / normal u8×4 = 32 B
- [x] Per-section VkBuffer (HOST_VISIBLE + COHERENT for simplicity; staging upload deferred to 1.4.3)
- [x] Chunk pipeline (depth-tested, GL→VK clip in shader, push-constant section world-offset)
- [x] D32_SFLOAT depth attachment threaded through dynamic rendering
- [x] Per-frame draw loop iterates loaded sections, untextured (vertex color × NdotL + light)
- [x] **First-boot validated:** chunks visible (garbled topology — see 1.4.2.5)
- [x] **Hazard discovered:** use-after-free on VkBuffer when sections re-mesh on workers — render thread still held handle. Fixed by leak-on-replace; proper deferred deletion in 1.4.3.

#### 1.4.2.5 — Honor quad index buffer  ✓ done
- [x] Shared u32 index buffer at init: `{0,1,2, 2,3,0}` × 16384 quads = 96K indices, 384 KB
- [x] Renderer binds shared index buffer once per render pass; `vkCmdDrawIndexed` per section
- [x] **First-boot validated:** chunks now render as **recognisable terrain** — hill shapes, dirt/stone patches, vertex-shaded by sun. Confirmed first true Minecraft-world output through our custom Vulkan pipeline.

#### 1.4.3 — Deferred deletion + lifecycle (split from original)

##### 1.4.3a — Deferred deletion  ◄ current
- [ ] Atomic frame counter in `BvhStore`, render thread ticks each frame
- [ ] `pending_` queue of `(buffer, memory, safe_at_frame)` triples replaces leak-on-replace
- [ ] Render thread calls `flush_pending_deletes()` after `vkWaitForFences` (guarantees GPU done with `frame - FRAMES_IN_FLIGHT`)
- [ ] Worker sets `safe_at = current_frame + FRAMES_IN_FLIGHT + 1` when queueing
- [ ] **Exit:** long play sessions no longer leak; pending queue size stays bounded by re-mesh rate

##### 1.4.3b — Section lifecycle (future)
- [ ] Hook MC's chunk unload notifications → call `rtx_remove_chunk`
- [ ] Per-section transform via push-constant world offset (already done in 1.4.2)
- [ ] Draw indirect (future optimisation)

##### 1.4.3c — DEVICE_LOCAL staging (deferred)
- [ ] Per-section buffer in DEVICE_LOCAL memory + transient staging buffer
- [ ] Render-thread upload queue (workers enqueue, render thread drains)
- [ ] Rationale for deferral: on ReBAR systems, HOST_VISIBLE memory is effectively device-local; perf cost of current path is negligible at typical section counts.

#### 1.4.4 — Texture atlas  ✓ done
- [x] Capture MC's block atlas on `SpriteAtlasTexture.create` HEAD; iterate `StitchResult.sprites()`
- [x] Rebuild atlas pixels client-side (`copyPixelsArgb`, ARGB→RGBA shuffle into direct ByteBuffer)
- [x] VkImage (DEVICE_LOCAL R8G8B8A8_UNORM) + VkImageView + VkSampler (NEAREST/REPEAT)
- [x] Staging buffer + one-shot transfer command (dedicated upload command pool, vkQueueWaitIdle)
- [x] Descriptor set layout (set 0, combined image sampler) + pool + allocated set
- [x] `bind_atlas()` updates descriptor set with image view + sampler; `bind_descriptor_set()` per render pass
- [x] chunk.frag samples atlas at v_uv0, multiplies by vertex color × NdotL × stored light
- [x] **Gotcha discovered:** `Sprite.getX()/getY()` is 16 px before actual sprite content (mipmap border). Use `round(getMinU() * atlasW)` to get the position MC's chunk UVs actually point to. Documented in memory for future renderer work.
- [x] **First-boot validated:** proper textured Minecraft terrain — grass / stone / dirt / wood all visible through our VK path.

#### 1.4.x — All four render layers  ✓ done
- [x] Java mixin forwards SOLID + CUTOUT + TRANSLUCENT + TRIPWIRE with a layer ID through extended JNI
- [x] BvhStore tracks chunks per (layer, ChunkKey) — `std::array<unordered_map, LAYER_COUNT>`
- [x] ChunkRenderer builds two pipelines (opaque + translucent) sharing one layout/descriptor set
- [x] Translucent pipeline: depth-test on, depth-write **off**, standard alpha blend
- [x] Render-pass order: SOLID → CUTOUT → TRIPWIRE (opaque) → TRANSLUCENT
- [x] **Validated:** leaves on trees, water in oceans, tall grass / vines / flowers — visible. World looks visually complete (within "no sky / no entities" caveats).

#### 1.4.5 — Frustum culling  ✓ done
- [x] `frustum.h` header-only: Gribb-Hartmann 6-plane extraction from view*proj, normalize, AABB-outside test using "n-vertex" picking
- [x] Per-frame frustum compute in `rtx_render_frame`; per-section 16³ AABB at (cx*16, cy*16, cz*16)
- [x] Skip `chunk_renderer.record()` if AABB fully outside; otherwise draw
- [x] Periodic stats log (every 600 frames ≈ 10 s) shows drawn vs culled count
- [x] **Validated:** visually identical output, cull rate 63–85% depending on camera direction (FOV-cone expected ratio). 4× perf headroom unlocked for everything subsequent.

#### 1.4.6 — Greedy meshing (optional perf)
- [ ] Port Sodium's algorithm OR stay with MC's per-face meshes if perf is acceptable
- [ ] **Exit Phase 1.4:** chunks render at vanilla visual parity

### 1.5 — Entities + particles + block entities (~2 weeks)
- [ ] Entity model upload (vanilla model JSONs → VK mesh)
- [ ] Skeletal animation in vertex shader
- [ ] Particle batched sprites
- [ ] BlockEntity adapter (chests, signs, beds, banners)
- [ ] **Exit:** mobs animate, particles work, chests open

### 1.6 — HUD compositor + vanilla post (~3 days)
- [ ] VK reimplementation of `GuiGraphics` (sprite batcher, text via MC's font atlas)
- [ ] Entity outline + fog passes
- [ ] **Exit:** vanilla parity for one full play session — main menu through gameplay through pause

## Phase 2 — DLSS SR (~1 week)
- [ ] Streamline init, plugin discovery
- [ ] G-buffer additions: motion vectors, jittered camera
- [ ] DLSS-SR integration, quality/balanced/performance modes
- [ ] **Exit criteria:** clean upscaling, no ghosting on entities

## Phase 3 — Path tracer foundation (~3 weeks)
- [ ] BLAS-per-subchunk build, TLAS assembly
- [ ] Bindless material descriptors, PBR atlas loader (LabPBR support)
- [ ] Ray-gen → primary + NEE-to-sun
- [ ] Procedural sky in miss shader
- [ ] Emissive sampling (uniform — ReSTIR comes later)
- [ ] Simple temporal accumulation (pre-RR)
- [ ] **Exit criteria:** path-traced direct lighting + sun shadows, noisy but stable

## Phase 4 — DLSS Ray Reconstruction (~1 week)
- [ ] Albedo demodulation pre-RR
- [ ] Specular hit distance buffer
- [ ] Swap accumulation → RR
- [ ] **Exit criteria:** clean output at 1 spp + RR

## Phase 5 — GI (~2 weeks)
- [ ] One-bounce diffuse GI
- [ ] ReSTIR DI for emissives
- [ ] ReSTIR GI for indirect
- [ ] **Exit criteria:** indoor lighting from torches looks correct, color bleeding

## Phase 6 — DLSS MFG (~1 week)
- [ ] Reflex integration (prerequisite)
- [ ] HUD-less color output buffer
- [ ] Streamline FG tag-in
- [ ] **Exit criteria:** 4x frame gen working, HUD doesn't ghost

## Phase 7 — Polish (open-ended)
- [ ] Volumetric lighting
- [ ] Caustics for water
- [ ] Stochastic transparency for stained glass
- [ ] Distant-chunk LOD/proxy BVH for render dist 24+
- [ ] Settings menu (RT quality, GI bounces, DLSS preset, MFG factor)
- [ ] Mod-compat adapter for BlockEntityRenderer geometry
