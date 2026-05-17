# mc-rtx-renderer

Fork-and-rewrite of Minecraft Java 1.21.x renderer using **Vulkan 1.3 + full path tracing + DLSS 4** (Super Resolution, Ray Reconstruction, Multi-Frame Generation). Target hardware: NVIDIA RTX 40/50-series.

> **State:** Phase 1.2 — GL suppression mechanism in place. MC's main window now uses `GLFW_NO_API`; all `RenderSystem`/`GlStateManager` calls land in a hidden 1×1 dummy GL context. VK present is the only present path. Should make the animated clear color visible — but mixin signatures haven't been validated against a real MC boot yet. See [ROADMAP.md](ROADMAP.md).

## Read first

- [DESIGN.md](DESIGN.md) — architecture, pipeline, BVH strategy, DLSS integration, performance budget
- [ROADMAP.md](ROADMAP.md) — phased delivery plan

## Repo layout

```
src/main/java/com/rtxmc/     # Fabric mod: entry, mixins, JNI surface
src/main/resources/          # fabric.mod.json, mixins config, access widener
native/                      # C++ Vulkan + Streamline backend (CMake)
shaders/                     # GLSL ray tracing shaders (rgen/rmiss/rchit)
```

## Build prerequisites

1. **JDK 21+** (Minecraft 1.21 requires Java 21)
2. **Vulkan SDK 1.3.275+** from LunarG — needed for headers, `glslangValidator`, validation layers
3. **CMake 3.24+** and **MSVC 2022** (or clang-cl)
4. **NVIDIA Streamline SDK 2.7+** — clone https://github.com/NVIDIA-RTX/Streamline into `./streamline/` (or pass `-DSTREAMLINE_DIR=<path>` to CMake). If absent, native lib still builds but DLSS is stubbed out.
5. **Minecraft Java 1.21.5** dev jar — Loom downloads this automatically

## Build

```powershell
# Native lib (rtx_renderer.dll)
cd native
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# Mod jar (also kicks off native build via the buildNative task)
cd ..
./gradlew build
```

Output mod jar lands in `build/libs/`. Drop it into a Fabric 1.21.5 instance alongside Fabric API.

## What works today (in theory — see validation gap below)

**Phase 1.1 — VK pipeline:**
- Real HWND extracted from GLFW via `GLFWNativeWin32`
- VK 1.3 instance, Win32 surface, discrete RT-capable device picked, swapchain w/ MAILBOX/FIFO fallback
- Double-buffered per-frame sync (image-available + render-finished semaphores, in-flight fences)
- Per-frame: acquire → layout transition → animated clear → layout transition → present
- Out-of-date / suboptimal swapchain handling

**Phase 1.2 — GL suppression:**
- `WindowMixin` forces main window to `GLFW_NO_API`
- Hidden 1×1 dummy GL window absorbs all of MC's `RenderSystem` / `GlStateManager` calls — no crash from null function pointers, no comprehensive shim needed
- `glfwSwapBuffers` no-op'd; VK present from `LevelRendererMixin` is the only present
- Vanilla world render cancelled to save cycles

**Validation gap:** I haven't been able to run this against a real MC 1.21.5 instance yet. The mixin targets `glfwCreateWindow` / `glfwMakeContextCurrent` / `glfwSwapBuffers` call sites by descriptor in `Window.<init>` / `Window.swapBuffers`. If Mojang's bytecode for those methods has shifted, the mixin's `defaultRequire: 1` will fail loudly at startup (good — you'll see it in the log). First-boot will need a real `./gradlew runClient` to confirm. See [DESIGN.md §3.7](DESIGN.md) for the full reasoning.

## What this is NOT

- Not a shaderpack — incompatible with Iris/OptiFine shaders. PBR resource packs (LabPBR/SEUS) *will* be consumed once material loading lands.
- Not a Bedrock/RTX port — pure Java Edition.
- Not server-side. Client-render-only mod.
- Not mod-compatible yet — vanilla blocks/entities only in v1.
