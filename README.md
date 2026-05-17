# mc-rtx-renderer

[![build](https://github.com/AlvarTheNova/mc-rtx-renderer/actions/workflows/build.yml/badge.svg)](https://github.com/AlvarTheNova/mc-rtx-renderer/actions/workflows/build.yml)

Fork-and-rewrite of **Minecraft Java 1.21.11** renderer using **Vulkan 1.3 + full path tracing + DLSS 4** (Super Resolution, Ray Reconstruction, Multi-Frame Generation). Target hardware: NVIDIA RTX 40/50-series.

> **State:** Phase 1.3 — first real geometry. GLSL→SPIR-V build pipeline, VK 1.3 dynamic rendering, push-constant view/proj wired to MC camera. A two-sided RGB triangle now sits at world `(0, 100, 0)` and should follow camera motion. Phase 1.2 GL suppression still pending its first-boot validation in a live MC instance. See [ROADMAP.md](ROADMAP.md).

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

| Component | Used for | How to get |
|-----------|----------|------------|
| **JDK 21+** | Compiling the mod (Loom needs ≥ 21; JDK 25 also fine) | [Temurin](https://adoptium.net/) |
| **Vulkan SDK 1.3.275+** | Native lib headers, `glslangValidator`, validation layers | [LunarG SDK](https://vulkan.lunarg.com/) |
| **CMake 3.24+** and **MSVC 2022** | Native lib build (Windows only for now) | VS Installer with "Desktop development with C++" |
| **NVIDIA Streamline SDK 2.7+** | DLSS SR / RR / FG. Optional — without it the native lib builds with DLSS stubbed. | Clone https://github.com/NVIDIA-RTX/Streamline into `./streamline/` or pass `-DSTREAMLINE_DIR=<path>` |
| **Minecraft 1.21.11 dev jar** | Mapped MC for Loom dev environment | Loom downloads it automatically on first build |

Minecraft itself doesn't need to be installed separately for development — Loom handles it.

## Build

The mod jar builds with no native dependencies (Gradle/Loom does everything). The native lib build is opt-in via `-PwithNative=true` since it requires the Vulkan SDK.

```powershell
# Mod jar only — produces build/libs/mc-rtx-renderer-<version>.jar
./gradlew build

# Mod jar + native lib (requires Vulkan SDK, CMake, MSVC)
./gradlew build -PwithNative=true

# Native lib standalone
cd native
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

CI (`.github/workflows/build.yml`) builds them in two parallel jobs: mod jar on Linux, native lib on Windows.

### Notes on local Windows + JDK 25

JDK 25's bundled cacerts can't validate Cloudflare-fronted maven repos (Fabric, Maven Central all go through Cloudflare). `settings.gradle.kts` works around this by switching to the Windows system trust store on Windows. If you hit `PKIX path building failed`, that's the symptom.

### Running in dev

```powershell
./gradlew runClient
```

Loom spins up Minecraft 1.21.11 with the mod loaded. First run downloads MC + assets (~1 GB).

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

**Phase 1.3 — first geometry:**
- GLSL shaders compiled to SPIR-V at CMake configure (via `glslangValidator --vn`), embedded as C arrays into the native lib
- VK 1.3 dynamic rendering (no render pass / framebuffer boilerplate)
- Push-constant `view * proj` carries MC's camera into the vertex shader
- Two-sided triangle at world `(0, 100, 0)`, ~10m tall, RGB-shaded
- Dark-slate clear color so the triangle is unmistakable

**Validation gap:** I haven't been able to run this against a real MC 1.21.11 instance yet. The mixin targets `glfwCreateWindow` / `glfwMakeContextCurrent` / `glfwSwapBuffers` call sites by descriptor in `Window.<init>` / `Window.swapBuffers`. If Mojang's bytecode for those methods has shifted, the mixin's `defaultRequire: 1` will fail loudly at startup (good — you'll see it in the log). First-boot will need a real `./gradlew runClient` to confirm. See [DESIGN.md §3.7](DESIGN.md) for the full reasoning.

When you do test, the triangle should appear at world `(0, 100, 0)` (a few blocks above sea level near spawn) and the per-vertex RGB colors should remain stable as you pan the camera. If colors swim or geometry warps with rotation, the matrix order / column-vs-row-major handling is off.

## What this is NOT

- Not a shaderpack — incompatible with Iris/OptiFine shaders. PBR resource packs (LabPBR/SEUS) *will* be consumed once material loading lands.
- Not a Bedrock/RTX port — pure Java Edition.
- Not server-side. Client-render-only mod.
- Not mod-compatible yet — vanilla blocks/entities only in v1.
