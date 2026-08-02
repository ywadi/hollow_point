# Decision log

Every entry records what was rejected and why. If you are about to change one of
these, read the rationale first — most were chosen against a specific failure.

---

## D1 — Zig as the compiler, CMake + Ninja retained

**Decision:** `zig cc`/`c++`/`ar`/`rc` drive the compile via CMake toolchain
files; Ninja stays the incremental engine; `build.zig` orchestrates.

**Rejected — full `build.zig` rewrite:** would mean reimplementing ~40 targets of
Diligent's build (per-target defines, generated shader headers, the
RenderStatePackager step, vendored glslang/SPIRV-Tools/spirv-cross) and redoing
it on every engine update. Weeks of work, permanently fragile.

**Rejected — hybrid (CMake for engine, `build.zig` for the app):** marginal gain.
Ninja already rebuilds only the changed app files; the cost is exporting and
maintaining a 40-library link line.

**Consequence:** nothing under `third_party/DiligentEngine` is patched, so engine
updates apply cleanly. Every incompatibility is handled from the toolchain file
or the root `CMakeLists.txt` instead.

---

## D2 — Windows target is Vulkan + OpenGL, no Direct3D

Chosen by the user from three options. Pure Zig, one toolchain, cross-compiles
from either host. D3D would need the MSVC ABI and a real Windows SDK, which
cannot be driven from Linux.

Nothing had to be patched to achieve this: Diligent's own `try_compile` probes
find `d3d11.h`/`d3d12.h` present but `atlbase.h` absent and disable D3D
themselves. `HAS_D3D11=TRUE, HAS_D3D12=TRUE, HAS_ATL=FALSE, D3D11_SUPPORTED=FALSE,
D3D12_SUPPORTED=FALSE, MINGW_BUILD=TRUE`.

---

## D3 — Full 2×2 host/target matrix with a vendored Linux sysroot

Either host builds either target. Requires vendoring X11/xcb/GL for the Linux
target because Zig ships libc and the Windows SDK but not those.

Also buys: identical inputs on every machine, no `-dev` packages, and glibc
pinning (2.28) so the output runs on far older distributions than the build host.

---

## D4 — Sysroot libraries are generated stubs, not copies

**This was a correction.** The sysroot originally held *copies* of the host's
real `.so` files. That silently worked until the first executable link:

```
ld.lld: error: undefined reference: dlopen@GLIBC_2.34
```

A real `libX11.so.6` from a glibc-2.35 host carries undefined references to
symbols newer than the 2.28 target. Shared libraries tolerate unresolved
symbols, so only the *executable* link exposed it — meaning the engine libraries
built "successfully" for a long time while the sysroot was quietly wrong.

`tools/mk_linux_sysroot.sh` now generates stub `.so` files: same exported symbol
names, same SONAME, **zero `NEEDED` entries**. They satisfy the linker without
importing the build host's glibc version. At runtime the loader binds to the
user's real libraries by SONAME.

**Rejected — raising the glibc pin to 2.35:** would discard the old-distro
portability that motivated D3.

**Rejected — `--allow-shlib-undefined`:** a one-line fix, but it disables a real
check and leaves the sysroot semantically wrong.

**Consequence, and it bit immediately:** stubs must never be found at *runtime*
or the program calls empty function bodies. See G6.

---

## D5 — Pinned toolchain in `.harness/`, CMake held at 3.x

`bootstrap.sh`/`.ps1` install Zig 0.16.0, CMake 3.31.12 and Ninja 1.13.2, all
checksum-verified. No host prerequisites remain.

**CMake must not be upgraded to 4.x.** CMake 4 rejects
`cmake_minimum_required(VERSION <3.5)` and Diligent's vendored third-party
libraries still declare **2.8**. 3.31 is simultaneously new enough for
ozz-animation, which requires 3.30.

Triggered by ozz-animation 0.17.0 needing CMake 3.30 while the host had 3.22.1.
Downgrading ozz was considered and rejected: even ozz 0.14.3 requires 3.24, so a
CMake upgrade was unavoidable either way — the only question was how far.

---

## D6 — Dear ImGui: upstream `docking` branch via `DILIGENT_DEAR_IMGUI_PATH`

Requested for dock panels. `third_party/imgui` is ocornut/imgui `docking`
(**1.92.9b**), wired in through Diligent's own supported cache variable
(`DiligentTools/ThirdParty/CMakeLists.txt:88`), so the engine tree stays
unpatched.

**Rejected — DiligentGraphics' fork's `docking` branch:** last commit **2019,
ImGui 1.73 WIP**. Unusable. Note this is *the fork's* branch being stale;
upstream `docking` is current and merged with master continuously. Do not
conclude "docking is unmaintained" from the fork.

Diligent's fork carried exactly **one** commit over upstream v1.92.1 — commenting
out an `InputScalar` assert, and initialising a `stb_textedit` local. Neither is
needed: upstream has since commented out that same assert itself, and the local
was refactored away.

**ImGui is not optional.** `DiligentFX` links `Diligent-Imgui` **PUBLIC** and
calls `ImGui::` in `ScreenSpaceReflection`, `ScreenSpaceAmbientOcclusion`,
`Bloom`, `ToneMapping` and `CoordinateGridRenderer`. Only DiligentCore is
ImGui-free. Link `Diligent-Imgui`, not raw ImGui — it carries the RHI renderer
backend, which is the valuable part.

---

## D7 — EnTT is not vendored

EnTT 3.16.0 is already fetched by DiligentFX via `FetchContent`; link
`EnTT::EnTT`. Adding a second copy would collide on the target name. To control
the version, set `FETCHCONTENT_SOURCE_DIR_ENTT` to a local checkout rather than
adding a subdirectory.

---

## D8 — ozz-animation built as runtime libraries only

`ozz_build_tools/fbx/gltf/samples/howtos/tests/data` all **OFF**: the samples
need GLFW/OpenGL and the FBX pipeline needs the proprietary FBX SDK, neither of
which survives a cross-compile. `ozz_build_postfix` OFF too, so library files do
not get a per-config suffix (`ozz_base_r.a`).

---

## D9 — `apps/imgui_probe` is a deliberately disposable smoke test

A copy of `Tutorial10_DataStreaming`, chosen because it drives ImGui hard **and**
calls `ImGui::InputInt(..., ImGuiInputTextFlags_EnterReturnsTrue)` — the exact
path behind the assert Diligent's fork used to comment out.

One marked edit (`HP_DOCKING_PROBE`) enables docking, creates a
`DockSpaceOverViewport` and a second dockable window, and honours
`HP_PROBE_EXIT_FRAMES` for headless runs. Those docking APIs do not exist in
ImGui 1.92.1, so **the build itself proves the swap took effect**.

Delete the directory once a real app exists.
