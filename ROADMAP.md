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

## Phase 1 — Vulkan substitution, raster only (~2 weeks)
- [ ] Suppress vanilla GL context creation; create VK swapchain on GLFW HWND
- [ ] Vanilla-equivalent rasterizer in Vulkan (chunk meshes, entities, particles)
- [ ] Vanilla post (entity outline, fog)
- [ ] HUD compositor
- [ ] **Exit criteria:** runs a vanilla world at vanilla visual parity, all in VK

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
