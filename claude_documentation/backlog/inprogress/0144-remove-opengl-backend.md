# T0144 — Remove the OpenGL backend; Vulkan only

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 412 |
| **Created** | 2026-08-06 |
| **Refs** | **D29** (the decision), **amends D1**; [T0142](../inprogress/0142-slang-shader-language.md) — **this unblocks 142.2**, which cannot proceed while GL constrains the shader source; T0130 — reverse-Z, which `DepthConvention` implements and which must survive the simplification; T0025, T0027, T0135 — GL paths they created |

## Why

**The owner's decision, D29:** a small studio cannot support two backends, and
the second one is worth low single digits of players. Unreal made the same cut in
4.26 (December 2020). Full reasoning, the hardware floor and what was rejected
are in the decision log.

**Two things make this a gain rather than a loss.**

**It removes a class of silent bug.** Vulkan clips Z to `[0, 1]` and OpenGL to
`[-1, 1]`, and `DepthConvention.hpp` exists to paper over it — its own comment
warns of *"a projection matrix that is right on Vulkan and silently wrong on
OpenGL."* A second path exercised half as often, failing quietly, is exactly the
shape that cost a full session on 2026-08-06.

**It unblocks D28.** Slang renames every resource in its HLSL and GLSL output
(`cbFrameAttribs` → `cbFrameAttribs_0`) and GL binds by name; Diligent's GL
backend refuses bytecode outright (`ShaderGLImpl.cpp:221`). So GL pins
`HpSurface.slang` to the subset both compilers accept, and `IHpMaterial` —
interfaces, defaults, `override`, the whole reason D28 was adopted — stays
unreachable until GL is gone.

## Done when

- [ ] No OpenGL device can be created, and nothing references one.
- [ ] `zig build test -Dtest=all` and `-Dtest=gpu` pass on both targets. **The GPU
      case count will drop** — the suite currently runs many cases twice, once
      per backend. That is the expected outcome, not a regression; record the
      before and after numbers.
- [ ] A machine without Vulkan gets a clear logged message, never a crash.
- [ ] `zig build docs` clean, `check_backlog.py` clean, `dist` still runs with
      the build tree deleted.

## Subtasks

- [x] 144.1 **Remove the public API.** `RenderBackend::OpenGL` and
      `WindowConfig::openGLContext` go. **This is a breaking change** —
      `RenderBackend::Default` no longer means "try Vulkan, fall back to GL", and
      its documentation must say what it does mean.
- [x] 144.2 **Remove GL device creation and window setup** in `Render.cpp` and
      `Window.cpp`. `RenderLayer` must fail with a clear logged message when
      Vulkan is unavailable — **never a crash, never silence**.
- [ ] 144.3 **Simplify `DepthConvention`.** The `[-1, 1]` clip case exists only
      for GL. **Reverse-Z (T0130) must survive unchanged** — verify with the
      existing depth tests before and after, and do not fold the two changes
      together. If the simplification looks like it alters depth behaviour at
      all, stop and record rather than proceeding.
- [x] 144.4 **Remove the GL branches in the shader path** —
      `combinedSamplers = IsGLDevice()` in `SurfacePipeline.cpp` and
      `ShaderSources.cpp`, and the dual-compiler selection D28 introduced.
      **After this, `HpSurface.slang` is no longer constrained to the HLSL subset**
      — but do **not** start using Slang-only constructs here. That is T0142.2,
      and it must be a separate ticket with its own verification.
- [x] 144.5 **Drop `Diligent-GraphicsEngineOpenGL`** from the link in
      `engine/CMakeLists.txt`, and any GL staging from `dist`.
- [x] 144.6 **Remove GL cases from the tests.** Several GPU tests parameterise
      over both backends; `gpu_adapter_report_test.cpp` reports both. Remove the
      GL half rather than leaving skipped cases.
- [ ] 144.7 **Update the documents.** `01-project-overview.md`'s backend table
      says "Vulkan, OpenGL" for both targets. `Render.hpp`'s own comments
      describe the fallback. `05-verification-status.md` may reference GL
      coverage. D1 is amended by D29 — add the cross-reference **in D1**, not
      only in D29.
- [x] 144.8 **Check the sysroot.** `third_party/sysroot/` carries X11/GL for the
      hermetic Linux build (see CLAUDE.md's trap about SDL auto-detecting the
      host). Removing the GL *backend* does not necessarily mean removing GL from
      the sysroot — SDL may still need it to create a window. **Verify before
      deleting anything there**; a broken sysroot fails in a way that looks
      unrelated.

## Notes / findings

### Baseline, measured on the tree at 15f4735 before any removal (2026-08-06)

Both targets, RTX 2080, wine running the Windows suite:

- fast: **302 cases / 214,663 assertions**, integration: **89 / 515** — all green
- gpu: **22 cases / 803 assertions** — all green
- Guard pixels (Linux / Windows-under-wine where they differ in the last digit):
  base colour centre (116, 108, 69) var 16.3817/16.382; mesh normal var
  7.10259e-11; shading normal var 12.1651/12.165; occlusion var 6.3732/6.3734;
  rock metallic (0,0,0), metal metallic (249,249,249); lit rock (85, 80, 57);
  metal (12, 12, 11); lit quad (211, 144, 144)

### How the removal is shaped (2026-08-06)

- **`DILIGENT_NO_OPENGL ON` in the root CMakeLists**, not merely an unlinked
  target: the GL backend's ~100 targets stop existing in the build, so nothing
  can quietly depend on one. Both targets reconfigure and build clean with it.
- **The switch in `RenderLayer::onAttach` stays** with `Default` and `Vulkan`
  falling through to one `createVulkan` — a new enumerator cannot be added
  without deciding what it creates.
- **`compileEngineShader` keeps Diligent's HLSL path deliberately** (it tests
  the source factory through Diligent's own front end, not slang), but passes
  `UseCombinedTextureSamplers = false` now — behaviour-identical on Vulkan,
  where `IsGLDevice()` was already false.
- **A projection-pinning fast case was added _before_ the depth simplification**
  ("the projection is Diligent's [0, 1] mapping with the planes swapped, byte
  for byte") so 144.3 has a byte-exact guard that exists on both sides of the
  change.

### After the removal, before the depth simplification (2026-08-06)

Full verification on the removal commit, both targets:

- fast: **303 / 214,665** (302 + the new projection-pinning case) — green
- integration: **89 / 515**, unchanged — green
- gpu: **15 cases / 493 assertions** (was 22/803; the seven `on OpenGL` cases
  are gone) — green, RTX 2080, wine for the Windows suite
- Guard pixels, now identical on both targets to the printed digit: base
  colour (116, 108, 69) var 16.382; mesh normal var 7.10259e-11; shading
  normal var 12.165; occlusion var 6.3734; rock metallic (0,0,0), metal
  (249,249,249); lit rock (85, 80, 57) var 14.5219; metal (12, 12, 11) var
  6.8905; lit quad (211, 144, 144)
- `zig build docs` exit 0
- **The no-Vulkan machine, simulated** (`VK_ICD_FILENAMES=/nonexistent-icd.json`,
  editor under `timeout 12`): one ERROR naming D29, the missing-driver causes
  and the fix, then the app ran 716 frames with an inert renderer and shut
  down cleanly — 15 log lines total, exit 0 on close. Never a crash, never
  silence, never spam.

### Measured: a GL device in the process shifted the Vulkan pixels — evidence for the decision itself

After the removal, the Linux gpu run's frame-wide **variation statistics moved
in their 5th significant digit** (16.3817→16.382, 12.1651→12.165, 6.3732→6.3734,
14.5218→14.5219, 6.89045→6.8905) while every centre pixel stayed byte-identical.
That looked like the exact hazard 144.3 warns about, so it was chased to ground
before committing — three experiments, one binary:

1. **Not the CMake flag**: with the new sources but `DILIGENT_NO_OPENGL OFF`,
   the new values persisted.
2. **Not the sources**: the **pristine-HEAD binary** run with only the textured
   cases (`--test-case="*textured*..."`) prints the **new** values.
3. **The cause is process history**: the same pristine binary run with the
   OpenGL cases first prints the **old** values.

So: **once an OpenGL device has existed in the process, the next Vulkan
device's texture filtering shifts by ±1 LSB on scattered pixels** (NVIDIA
RTX 2080 on Linux; ~90–550 of 196,608 bytes per 256×256 frame,
max delta 1, only in texture-sampled channels — mesh-normal, texcoord and
constant channels are byte-identical). Diffed on the dumped PPMs, and each
state is perfectly deterministic run-to-run.

The values the suite settles on now are the *uncontaminated* Vulkan values —
they are what the Windows suite printed all along, and they match the guard
values in this ticket's baseline exactly. The pre-removal Linux "baseline" was
the contaminated reading. This is D29's "a second path failing quietly"
measured at 1 LSB: the fallback backend was perturbing the primary's output
merely by having existed in the same process.

Hyper-V's GPU-P supports DX11 and OpenGL but **not** Vulkan; RDP forwards no
acceleration; Windows has no ubiquitous software-Vulkan fallback the way GL has
GDI-generic. So a team developing or testing inside VMs would lose the ability to
run the engine at all. **The owner confirmed nobody does**, which is what made
the decision safe.

Related and still true: `gpu_adapter_report_test.cpp` records the CI Windows host
as *"gdi generic — Windows' generic OpenGL 1.1"*. GPU tests are built but never
run in CI, so this does not block — but **GPU tests can never run on that runner
after this ticket** until it has a real Vulkan device.

### 144.8 — the sysroot keeps its GL, and here is the measurement

`SDL_build_config.h` in the hermetic Linux tree says `SDL_VIDEO_OPENGL 1` and
`SDL_VIDEO_OPENGL_GLX 1`, and `CMakeCache.txt` has `HAVE_OPENGL=1` — SDL's
video driver **compiles its GLX support against the sysroot's GL headers**
(the build is hermetic, so those probes can only have hit
`third_party/sysroot/linux-x86_64/include/GL`). Deleting the GL headers would
change SDL's compiled feature set, exactly the "fails in a way that looks
unrelated" the subtask warns about. SDL resolves libGL at *run* time (dlopen),
and the engine never requests a GL context any more, so the code is dormant —
but it must still compile. **Nothing was deleted from the sysroot.** The GL
stub *libraries* are likely now link-time-unreferenced (Diligent's GL backend
was their consumer), but they cost nothing, `tools/mk_linux_sysroot.sh`
regenerates them as a set, and removing them buys nothing.

Same verdict for `cmake/toolchains/zig-common.cmake`'s import-library list on
Windows (`opengl32` among `dwmapi winmm shcore …`): it pre-generates MinGW
import libraries; the opengl32 one may now go unused, and it stays because the
loop is generic toolchain plumbing and the Windows build was not going to be
destabilised for one unused `.a`.

### What is lost

Intel Haswell and Broadwell on Windows (2013–14) never had an official Vulkan
driver, and are lost outright. So is any machine whose OEM driver package never
registered a Vulkan ICD on otherwise-capable hardware.
