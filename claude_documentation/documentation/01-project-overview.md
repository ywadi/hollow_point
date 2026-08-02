# Project overview

**HollowPoint** — a C++17 project built on [DiligentEngine](https://github.com/DiligentGraphics/DiligentEngine)
(cross-platform rendering). Not a git repository at the time of writing;
`third_party/DiligentEngine` and the vendored libraries have their own `.git`.

Root: `/media/ywadi/second/hollow_point`

## Tree

```
CMakeLists.txt              root build; adds third_party libs, then globs apps/*
build.zig                   the build harness -- single entry point, both hosts
bootstrap.sh / .ps1         install pinned zig + cmake + ninja into .harness/
BUILDING.md                 user-facing build instructions
cmake/
  toolchains/               zig toolchain files (see 03-build-harness.md)
  dist.cmake                stages a built tree into dist/
tools/
  mk_linux_sysroot.sh       regenerates the Linux stub sysroot
  find_win_header_case.sh   rescans for case-mismatched Windows headers/libs
apps/
  imgui_probe/              Tutorial10 copy -- build+run smoke test (disposable)
third_party/
  DiligentEngine/           the engine (submodules: Core, Tools, Samples, FX)
  imgui/                    ocornut/imgui `docking` -- overrides Diligent's copy
  enkiTS/  meshoptimizer/  ozz-animation/
  ufbx/                     v0.23.0 -- present but NOT wired into the build
  dxc/                      v1.8.2407 binaries -- present but NOT wired in
  sysroot/linux-x86_64/     X11/xcb/GL headers + generated link stubs
.harness/                   bootstrapped toolchain (gitignored)
build/<target>-<config>/    build trees
dist/<target>/              staged output
```

## What the build produces

Engine static libraries, the `GraphicsEngineOpenGL` / `GraphicsEngineVk` shared
libraries, and `apps/imgui_probe/ImGuiProbe`. **There is no real application
yet** — the probe is a disposable smoke test.

Roughly 1100 build targets per platform.

## History worth knowing

- There was an app at `apps/terrain_lab` (a derivative of Diligent's Atmosphere
  sample plus custom `Water/WaterLayer.cpp` and `Water/TerrainAS.cpp`). The user
  **deleted it deliberately** and asked for all traces removed; `build/apps` was
  removed accordingly. Its custom shader headers and terrain assets existed only
  inside the build tree and are gone. This is expected, not an accident.
- `ufbx` and `third_party/dxc` are staged for future use and referenced by
  nothing. Do not assume they work.

## Graphics backends

| Target | Backends |
|---|---|
| Linux x86_64 | Vulkan, OpenGL |
| Windows x86_64 | Vulkan, OpenGL |

**No Direct3D on Windows**, and this is by design, not an oversight. Zig targets
Windows through the MinGW-w64 ABI, MinGW has no `atlbase.h`, and DiligentCore
gates D3D11/D3D12 on ATL (`DiligentCore/CMakeLists.txt:160-181`). The engine
detects this and configures itself for Vulkan + GL. Direct3D would require the
MSVC ABI plus a real Windows SDK, which cannot be driven from a Linux host.
The user chose this trade-off explicitly.
