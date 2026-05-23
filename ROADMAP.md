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

### 1.5 — Entities + particles + block entities

#### 1.5.1 — Discovery  ✓ done
- [x] Found the centralised batched-draw chokepoint: `VertexConsumerProvider$Immediate.draw(RenderLayer)`
- [x] Mixin observes (without consuming) — log-only
- [x] **Cataloged:** 54 unique render layers in a basic creative session, 5+ vertex format variants, ~100 draws/frame. Documented in memory.

#### 1.5.2 — First mob (~multi-session)

##### 1.5.2a — Steal entity bytes  ✓ done
- [x] `EntityBatchContext` ThreadLocal differentiates Immediate.draw batches from chunk-mesher `endNullable()` calls
- [x] `VertexConsumerProviderMixin` sets / clears context on `Immediate.draw(RenderLayer)` HEAD / RETURN
- [x] `BufferBuilderEndMixin` `@At("RETURN")` of both `end()` AND `endNullable()` (vanilla uses the latter), copies bytes when context is set, returns BuiltBuffer untouched (vanilla unaffected)
- [x] `NativeBridge.uploadEntityBatch(layerHash, bytes)` + native log
- [x] **Validated:** entity_translucent layer flowing with format `[Position, Color, UV0, UV1, UV2, Normal]` (36 B/vert)

##### 1.5.2b — First mob visible  ✓ done
- [x] Native side: per-frame transient `VkBuffer` per batch (HOST_VISIBLE, freed via `BvhStore::queue_buffer_delete` + 3-frame safety margin)
- [x] Entity vertex format: 36 B = chunk format + UV1 (overlay coords) at attrs 0–5
- [x] New `entity_renderer.{h,cpp}` with 36 B pipeline (alpha-blend ON, depth test+write ON), shares ChunkRenderer's atlas DSL + descriptor set
- [x] Render order: chunks (opaque) → chunks (translucent) → entities → triangle overlay
- [x] `viewRot` (rotation-only positionMatrix) plumbed through `FrameParams` — MC's MatrixStack pre-translates entity verts by -camPos, so full `view` would double-shift
- [x] `uploadEntityBatch(layerHash, vertexCount, ByteBuffer)` — count needed for native stride filter (only 36 B accepted; LINES/GLINT/debug dropped)
- [x] **Exit validated in MC:** mobs render at correct world positions (textures wrong as expected — atlas-reuse first light)

##### 1.5.2c — Vertex format variants  ⚠ partial — full validation blocked on 1.6 HUD
- [x] Glint pipeline (Position + UV0 = 20 B, additive blend, depth-test only)
- [x] Lines pipeline (Position + Color + Normal + LineWidth = 24 B, `LINE_LIST` topology, fixed width=1.0; per-vertex width attribute bound for layout but unused)
- [x] Debug filled box pipeline (Position + Color = 16 B, alpha-blended triangles)
- [x] EntityRenderer refactored to dispatch by stride; all four variants share one pipeline layout (push constants + atlas DSL); lines/debug shaders simply don't declare the sampler
- [x] Draw order: entity → debug_box → lines → glint (additive overlay last)
- [x] **Partial exit:** native pipelines + stride dispatch verified non-crashing in-MC; entity (36 B) variant regression-checked — mobs still render at correct positions
- [ ] **Blocked:** glint needs enchanted item in inventory (no HUD = no inventory access); lines need block-aim hover (no crosshair); debug_box needs F3+G/B which works without HUD but requires the tester to actually press it. Revisit after 1.6 HUD lands so a normal play session naturally exercises all three.
- [ ] _Deferred:_ 12 B position-only (water_mask, end_portal, end_gateway) — needs bespoke shaders

##### 1.5.2d — Per-layer textures (the real work)
- [ ] Resolve each `RenderLayer`'s `Sampler0` texture binding
- [ ] Upload per-mob skin textures (most are loaded lazily into MC's TextureManager)
- [ ] Descriptor pool / indexing strategy for many distinct textures
- [ ] **Exit Phase 1.5.2:** mobs rendered with correct skins

#### 1.5.3+ — Coverage
- [ ] All vertex format variants (glint additive, lines, etc.)
- [ ] Particles (separate mesh path — TBD)
- [ ] Block entities (chests, signs — partial overlap with entity renderer)
- [ ] **Exit Phase 1.5:** parity with vanilla entity rendering

### 1.6 — `GpuDevice` replacement: HUD + Screens via Vulkan (multi-session, est. 5-7 sessions)

> **Pivot rationale:** earlier estimate was "3 days" assuming HUD reuses our entity-batch hook. Investigation revealed HUD + Screens flow through the new `com.mojang.blaze3d.systems.GpuDevice` abstraction (`DrawContext → RenderPipeline → CommandEncoder → RenderPass`), not `VertexConsumerProvider.Immediate`. The architecturally pure path is to implement our own `GpuDevice` impl (`VkBackend`) that replaces `GlBackend`. Side benefit: this same path captures Screens, post-process, and main menu — far more than just HUD. Chunk + entity work from 1.4/1.5 is additive (those subsystems are NOT on GpuDevice yet).

#### 1.6.1a — VkBackend wrapper scaffold (intercept + log, delegate to GL)  ✓ done
- [x] `com.rtxmc.gpu.VkBackend implements GpuDevice` — 22 methods. Wraps Mojang's GlBackend instead of replacing it (so MC keeps running). Each method logs first 3 calls with a per-method counter.
- [x] `RenderSystemDeviceAccessor` widens private static `DEVICE` field
- [x] `RenderSystemInitMixin` @Inject TAIL of `initRenderer` reads DEVICE, wraps in VkBackend, writes back. Idempotent.
- [x] **Validated in-MC (7-min session):** F3 reads `rtxmc-vk(opengl)`. Tier 1 methods (createBuffer/Texture/TextureView/CommandEncoder/precompilePipeline) all saturated 3-call log budget = called many times. createSampler fires once per world load. Pure-query methods forwarded fine. No crashes.

#### 1.6.1b — Minimum-viable Vulkan resource impls (foundations + smoke test)  ✓ done (foundations)
- [x] `VkGpuBuffer extends GpuBuffer` backed by VkBuffer (HOST_VISIBLE) + JNI `createBuffer`/`destroyBuffer`/`mapBuffer`
- [x] `VkGpuTexture extends GpuTexture` backed by VkImage + JNI `createTexture`
- [x] `VkGpuTextureView extends GpuTextureView` — VkImageView wrapper
- [x] `VkGpuSampler extends GpuSampler` — VkSampler wrapper
- [x] `VkResNative` JNI surface (10 methods) using JNI-standard symbol naming (RegisterNatives via FindClass fails under Fabric's Knot classloader)
- [x] shaderc_combined linked from Vulkan SDK for 1.6.1c GLSL→SPIR-V
- [x] Shadow-exec in VkBackend.createBuffer/Texture/Sampler (budget=3 per method) to smoke-test the native paths
- [x] **Validated in-MC:** sampler created cleanly (h=0x1 returned by vkCreateSampler), JNI binding works, device-readiness guard prevents pre-rtx_init crashes
- [ ] **Surfaced architectural blocker:** MC creates Tier-1-heavy resources (GUI buffers + atlas textures) DURING RenderSystem.initRenderer — BEFORE our rtx_init() fires from RtxMod.onWindowCreated. Shadow paths for buffer + texture observed but bailed early via device-guard. **Fix is 1.6.1c.**

#### 1.6.1c — Init-order restructure  ✓ done
- [x] Added `@Inject @At("HEAD")` to `RenderSystem.initRenderer` that fires `RtxMod.renderer().init()` BEFORE Mojang creates the first GpuBuffer/Texture
- [x] `VulkanRenderer.init()` is idempotent — old MinecraftClientMixin TAIL call still fires but no-ops
- [x] **Validated in-MC:** 12 distinct VK resources created cleanly during initRenderer (handles 0x1–0xb for buffers+textures, 0xc for sampler at world-load). All translation tables verified against real Mojang usage patterns.

#### 1.6.1d — Pipeline compilation (GLSL → SPIR-V → VkGraphicsPipeline)
- [x] **Step 1 ✓ shipped:** shaderc wrapper (vk_shaderc.h/cpp) with thread-safe `shaderc::Compiler`, FNV-1a source-hash cache, vk1.3/SPIR-V 1.6 target. Validated in-MC: trivial vert/frag compiled to 184/100 SPIR-V words (4.5 MB dll growth from glslang+SPIRV-Tools bundling).
- [x] **Step 2 ✓ shipped:** vk_pipeline.cpp — full state translation tables (DrawMode→Topology, PolygonMode, DepthTestFunction, SourceFactor/DestFactor, VertexFormatElement.Type+count→VkFormat). Validated in-MC: hardcoded Position-only/TRIANGLES/alpha-blend spec built handle h=0x1 cleanly via shaderc→VkShaderModule→VkGraphicsPipeline. JNI: testCreatePipeline / destroyPipeline.
- [ ] **Step 3 next:** `precompilePipeline(RenderPipeline, ShaderSourceGetter)` Java path — extract state from RenderPipeline + GLSL via ShaderSourceGetter, call native createPipeline (real, not test)
- [ ] **Step 4:** `VkCompiledRenderPipeline implements CompiledRenderPipeline` holds the VkPipeline handle
- [ ] **Step 5:** Cache pipelines by RenderPipeline.getLocation() so identical specs don't recompile

#### 1.6.1e — CommandEncoder + RenderPass
- [ ] `VkCommandEncoder implements CommandEncoder` — records into our existing per-frame VkCommandBuffer
- [ ] `VkRenderPass implements RenderPass` — vkCmdBeginRendering / vkCmdSetPipeline / vkCmdDraw state machine
- [ ] Frame timing tweak: open cmd buf at WorldRenderer.render HEAD (not in our existing rtx_render_frame which currently both opens and submits)
- [ ] MC's HUD encoder/pass calls then record into our active cmd buf naturally

#### 1.6.1f — Swap from shadow to real Vk* returns
- [ ] VkBackend.createBuffer/Texture/Sampler return our Vk* instances instead of GL ones (no more shadow-discard)
- [ ] Why all-at-once: incremental replacement requires consumers to handle mixed GL/VK resource types — tightly-coupled cluster.

#### 1.6.1b — Buffer/texture/sampler primitives
- [ ] Implement `createBuffer` family → VkBuffer-backed `GpuBuffer` subclass with `slice()` support
- [ ] Implement `createTexture` family → VkImage-backed `GpuTexture` subclass
- [ ] Implement `createTextureView` → VkImageView wrapper
- [ ] Implement `createSampler` → VkSampler wrapper
- [ ] Implement `GpuFence` via VkFence

#### 1.6.1c — Pipeline compilation
- [ ] `precompilePipeline(RenderPipeline)` → translate vertex format + blend/depth/cull state to VkGraphicsPipeline
- [ ] GLSL → SPIR-V translation. Options: bundle `shaderc` for runtime, OR pre-compile and cache by hash
- [ ] Cache compiled pipelines keyed by `RenderPipeline.getLocation()`

#### 1.6.1d — CommandEncoder + RenderPass
- [ ] `CommandEncoder` impl: tracks current command buffer; methods open/close render passes, transfer buffers
- [ ] `RenderPass` impl: per-active-pass state machine; setPipeline + bindTexture + setVertexBuffer + drawIndexed
- [ ] Integrate with existing per-frame command buffer / sync primitives
- [ ] Handle `enableScissor` (vkCmdSetScissor)

#### 1.6.1e — Swapchain integration + HUD first-light
- [ ] Hook `CommandEncoder.createRenderPass(...)` targeting the swapchain → record into our active frame's cmd buffer
- [ ] Render order: world (chunks → entities → triangle) → HUD draws via VkBackend → present
- [ ] **Exit:** crosshair + hotbar + hearts visible in MC. Inventory screen opens via E.

#### 1.6.1f — Screens + post-process
- [ ] Validate inventory / chat / pause menu render correctly through the same path
- [ ] Confirm main menu / pause screens work (may still be on legacy GL — defer if so)

#### 1.6.2 — Backflow validation (1.5.2c completion)
- [ ] Now that crosshair exists: confirm selection box outlines render (lines variant)
- [ ] Equip enchanted item via newly-working inventory → glint shimmer visible
- [ ] F3+G chunk borders, F3+B hitboxes → debug_box variant exercised
- [ ] Mark 1.5.2c fully validated

### 1.6.x — Other vanilla overlays (post-HUD)
- [ ] Entity outline + fog passes
- [ ] Particles, weather overlays

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
