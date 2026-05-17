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

### 1.2 — GL suppression (~1 week)
- [ ] Mixin into `Window.<init>` before GLFW window creation: `glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API)`
- [ ] Comprehensive `RenderSystem` no-op shim (every static call MC makes — clear, blend, depth, viewport, etc. — redirected to a stub since there's no GL context)
- [ ] Replace `Window.swapBuffers` with VK present
- [ ] **Exit:** MC boots with no GL ctx, VK clear color is visible on screen, vanilla world rendering is bypassed

### 1.3 — Raster triangle (~2 days)
- [ ] Minimal VK graphics pipeline (vert + frag), one textured triangle from a vertex buffer
- [ ] Per-frame UBO with view/proj matrices wired to MC camera
- [ ] **Exit:** a debug triangle pinned at world origin, follows MC camera correctly

### 1.4 — Chunk rasterizer (~2 weeks)
- [ ] Hook `ChunkBuilder` mesh outputs, translate vanilla format → our packed vertex format
- [ ] Greedy meshing pass (port from Sodium reference)
- [ ] Bindless block texture atlas
- [ ] Frustum culling + per-section visibility
- [ ] **Exit:** chunks render at vanilla visual parity (no shadows/lighting beyond vanilla blocklight)

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
