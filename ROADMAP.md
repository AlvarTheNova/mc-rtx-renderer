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

#### 1.4.2 — Single-section render (~few days)
- [ ] Parse MC's vertex format (POSITION + COLOR + UV + LIGHT + NORMAL, packed)
- [ ] Translate to our packed format (see DESIGN §3.1)
- [ ] Per-section VK vertex buffer (device-local, staging upload)
- [ ] Chunk pipeline (vert+frag, depth test enabled, GL→VK clip applied to proj)
- [ ] Render one section in the test world, untextured (flat color from vertex)

#### 1.4.3 — All loaded sections
- [ ] Section lifecycle: add/remove on world load/unload
- [ ] Per-section transform = world position offset
- [ ] Draw indirect or simple per-section draw call loop

#### 1.4.4 — Texture atlas
- [ ] Extract MC's stitched block atlas (PNG)
- [ ] Upload as VkImage + VkSampler
- [ ] Bindless via `VK_EXT_descriptor_indexing` (already enabled in device features)
- [ ] Map UVs from MC's vertex stream to atlas coords

#### 1.4.5 — Frustum culling
- [ ] Per-section AABB
- [ ] Frustum extraction from MC's projection (we have it via FrameParams)
- [ ] Skip sections fully outside frustum

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
