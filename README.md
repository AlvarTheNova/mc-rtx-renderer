# mc-rtx-renderer

Fork-and-rewrite of Minecraft Java 1.21.x renderer using **Vulkan 1.3 + full path tracing + DLSS 4** (Super Resolution, Ray Reconstruction, Multi-Frame Generation). Target hardware: NVIDIA RTX 40/50-series.

> **State:** Phase 0 scaffolding only. The project compiles in shape — it does not yet render anything. See [ROADMAP.md](ROADMAP.md) for what's next.

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

## What works today

Nothing visible. This commit is project scaffolding: build system, mixin targets, JNI bridge, native module skeleton, shader skeletons. Phase 1 (Vulkan rasterizer at vanilla parity) is where actual pixels start showing up.

## What this is NOT

- Not a shaderpack — incompatible with Iris/OptiFine shaders. PBR resource packs (LabPBR/SEUS) *will* be consumed once material loading lands.
- Not a Bedrock/RTX port — pure Java Edition.
- Not server-side. Client-render-only mod.
- Not mod-compatible yet — vanilla blocks/entities only in v1.
