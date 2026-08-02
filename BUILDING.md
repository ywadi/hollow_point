# Building HollowPoint

Zig is the compiler for every target. CMake configures DiligentEngine and Ninja
does the incremental work, so only what actually changed gets rebuilt.

## Setup

```sh
./bootstrap.sh        # Linux host
.\bootstrap.ps1       # Windows host
```

That installs the whole toolchain into `.harness/` — **Zig 0.16.0, CMake 3.31.12
and Ninja 1.13.2**, each checksum-verified. Nothing else needs to be on the
host. Anything already on PATH is used only as a fallback.

CMake is held on the 3.x line deliberately: CMake 4 rejects
`cmake_minimum_required(VERSION <3.5)` and DiligentEngine's vendored
third-party libraries still declare 2.8. 3.31 is also new enough for
ozz-animation, which requires 3.30.

## Building

```sh
zig build             # the host's OS
zig build linux       # x86_64 Linux   (glibc 2.28)
zig build windows     # x86_64 Windows (MinGW ABI)
zig build all         # both
zig build dist        # stage into dist/<target>/
zig build clean
```

Either host builds either target — the Windows and Linux binaries are identical
whether they were produced on Windows or on Linux.

Options:

| Flag | Meaning |
|---|---|
| `-Dconfig=Release\|Debug\|RelWithDebInfo\|MinSizeRel` | CMake build type (default `Release`) |
| `-Djobs=N` | parallel compile jobs |
| `-Dverbose` | echo every compiler command line |
| `-Dccache=false` | ignore ccache even if installed |
| `-Dtarget=linux\|windows` | restrict `all`/`dist` to one target |

Each target and config gets its own tree under `build/`, so switching between
them never invalidates the other. `zig build all` runs the two targets one after
the other rather than at once — each already saturates the machine, and serial
output is readable.

## What gets built

The engine libraries, plus the `GraphicsEngineOpenGL` and `GraphicsEngineVk`
shared libraries (`.so` on Linux, `.dll` on Windows). There is no application
target yet.

To add one, create `apps/<name>/CMakeLists.txt` using Diligent's
`add_sample_app()` — it wires the platform entry points and copies the required
DLLs next to the executable. The root `CMakeLists.txt` picks up any directory
under `apps/` containing a `CMakeLists.txt`, so no other edit is needed.

## Libraries available to link

| Library | Target(s) | Where it comes from |
|---|---|---|
| Dear ImGui (docking) | `Diligent-Imgui` | `third_party/imgui` — ocornut `docking` branch |
| EnTT 3.16.0 | `EnTT::EnTT` | fetched by DiligentFX |
| enkiTS | `enkiTS` | `third_party/enkiTS` |
| meshoptimizer | `meshoptimizer` | `third_party/meshoptimizer` |
| ozz-animation 0.17.0 | `ozz_base`, `ozz_animation`, `ozz_geometry`, `ozz_animation_offline` | `third_party/ozz-animation` |

### Dear ImGui and docking

Link **`Diligent-Imgui`**, not raw ImGui — it carries the Diligent RHI renderer
backend (`ImGuiDiligentRenderer`) plus the per-platform impls. Then enable dock
panels the usual way:

```cpp
ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
```

Note that ImGui is not optional if you use DiligentFX: `DiligentFX` links
`Diligent-Imgui` **PUBLIC**, and its post-process components (SSR, SSAO, Bloom,
ToneMapping, CoordinateGridRenderer) call `ImGui::` directly for their settings
panels. DiligentCore itself has no ImGui dependency at all.

DiligentEngine bundles ImGui 1.92.1 as a submodule without docking. Rather than
patch the engine tree, the root `CMakeLists.txt` sets Diligent's own
`DILIGENT_DEAR_IMGUI_PATH` at `third_party/imgui`. Diligent's fork carried a
single commit over upstream — commenting out an `InputScalar` assert and
initialising a `stb_textedit` local — and neither is needed: upstream has since
commented out that same assert, and the local was refactored away.

One incompatibility does come with the newer ImGui. It renamed the modifier keys
in 1.89 (`ImGuiKey_ModCtrl` → `ImGuiMod_Ctrl`) and kept obsolete aliases; those
aliases were still live in 1.92.1 but are removed by 1.92.9, and Diligent's Linux
and Emscripten ImGui impls still use the old spellings. Since these are plain
enum renames, the root `CMakeLists.txt` maps them back with compile definitions
on `Diligent-Imgui` rather than patching the engine. The Win32 impl is
unaffected — it comes from ImGui's own up-to-date backend. Drop that block once
DiligentEngine updates those files.

`apps/imgui_probe/` is a copy of Tutorial10 that exercises all of this; see its
readme.

ozz's samples, tools, tests and the FBX/glTF importers are off: the samples need
GLFW/OpenGL and the FBX pipeline needs the proprietary FBX SDK, neither of which
survives a cross-compile. Flip the `ozz_build_*` options in the root
`CMakeLists.txt` if you need the offline importers on a host build.

## Graphics backends

| Target | Backends |
|---|---|
| Linux | Vulkan, OpenGL |
| Windows | Vulkan, OpenGL |

**Windows builds have no Direct3D.** DiligentCore gates D3D11/D3D12 on
`atlbase.h`, which is MSVC-only and absent from the MinGW-w64 ABI that Zig
targets. The engine detects this and configures itself accordingly — it is not a
misconfiguration. Direct3D would require the MSVC ABI plus a real Windows SDK,
which cannot be driven from a Linux host.

## How it fits together

| Path | Role |
|---|---|
| `build.zig` | the harness — one entry point on both hosts |
| `bootstrap.sh`, `bootstrap.ps1` | install the pinned Zig, checksum-verified |
| `cmake/toolchains/zig-common.cmake` | generates the `zig cc`/`c++`/`ar`/`rc` shims CMake needs |
| `cmake/toolchains/x86_64-*.cmake` | per-target settings |
| `cmake/dist.cmake` | stages a built tree into `dist/` |
| `third_party/sysroot/linux-x86_64/` | X11/xcb/GL headers + link stubs |
| `tools/mk_linux_sysroot.sh` | regenerates that sysroot |
| `tools/find_win_header_case.sh` | rescans for capitalised Windows headers |

Nothing under `third_party/DiligentEngine/` is patched, so engine updates apply
cleanly.

CMake needs a compiler to be one executable, but a Zig invocation is
`zig cc -target <triple> …`. The toolchain files generate small shims into
`build/<target>/toolchain/` that bake in the target and forward the rest.

`build.zig` re-runs CMake configure only when the build tree is missing or was
configured with a different toolchain or build type. After that Ninja re-runs
CMake by itself whenever a `CMakeLists.txt` changes, so an unconditional
configure would only add latency.

`compile_commands.json` is exported into each build tree for clangd.

### WHOLE_ARCHIVE and `--push-state`

Diligent links its static engine libraries into the `GraphicsEngine*` shared
libraries with `$<LINK_LIBRARY:WHOLE_ARCHIVE,...>`. CMake implements that on
GNU-like linkers as `--push-state,--whole-archive … --pop-state`, and Zig's
linker driver rejects `--push-state` outright.

CMake picks between that and a plain `--whole-archive`/`--no-whole-archive` pair
by *probing* the linker — but the binary it probes is `CMAKE_LINKER`, which
resolves to the host `/usr/bin/ld`, while linking actually goes through `zig cc`.
The probe answers for the wrong program. The toolchain therefore declares
`CMAKE_{,C_,CXX_}LINKER_PUSHPOP_STATE_SUPPORTED FALSE` up front;
`Platform/Linker/GNU.cmake` only probes `if(NOT DEFINED …)`, so this both skips
the bogus probe and selects CMake's own fallback form.

Worth knowing: that answer is cached in
`build/<target>/CMakeFiles/<ver>/CMakeCXXCompiler.cmake`, which is written at
compiler-detection time and reloaded on every reconfigure. Changing the setting
therefore needs a **fresh** build tree, not just a reconfigure.

### Windows header capitalisation

Diligent spells SDK headers as Microsoft documents them (`<Windows.h>`,
`<SDKDDKVer.h>`, `<D3D12.h>`), but MinGW-w64 ships every header lowercase.
That is invisible on Windows and fatal when cross-compiling from a
case-sensitive filesystem. The toolchain generates forwarding headers into
`build/<target>/toolchain/wininc/` and puts that directory last on the include
path, so a real header of the same name always wins and `third_party` stays
unpatched.

The forwarders are generated only when the filesystem is genuinely
case-sensitive — probed, not inferred from the host OS, because where
`Windows.h` and `windows.h` are the same file a forwarder would include itself.

After updating DiligentEngine or Zig, re-run `tools/find_win_header_case.sh`
and fold any new names into `_hp_win_headers` in `zig-common.cmake`.

### Windows import-library capitalisation

The same mismatch applies to libraries: Diligent links `Shlwapi`, MinGW names it
`shlwapi`. Zig synthesises import libraries from `.def` files rather than
shipping `.a` files, so there is nothing to symlink — the toolchain generates the
capitalised import library with `zig dlltool` into
`build/<target>/toolchain/winlib/`.

That list is deliberately explicit rather than a sweep. The directory goes first
on the library search path, so auto-generating (say) `libSPIRV.a` there would
shadow the real glslang target of that name and silently produce a broken link.

Diligent also names some system libraries MSVC-style — `dwmapi.lib` on
`Diligent-Imgui` under MinGW. CMake passes such an item through verbatim, and a
bare filename is opened relative to the working directory (`-L` is not
consulted), so MSVC-named copies are generated into the build root, which is
where Ninja runs every command from.

## Known constraints

- **`configure` needs network access.** DiligentFX fetches `entt` and
  DiligentTools fetches `rapidjson` via CMake `FetchContent` at configure time.
  Once configured, builds are offline. To pre-populate, point
  `FETCHCONTENT_SOURCE_DIR_ENTT` at a local checkout.
- **The Linux sysroot is x86_64 only.** Add `third_party/sysroot/linux-aarch64/`
  and a matching toolchain file to target ARM.
- The vendored sysroot supplies link-time headers and stubs only. At runtime the
  binary resolves `libX11.so.6`, `libGL.so.1` and friends from the user's system
  by SONAME.
