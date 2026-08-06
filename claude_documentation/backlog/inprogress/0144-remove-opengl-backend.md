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

- [ ] 144.1 **Remove the public API.** `RenderBackend::OpenGL` and
      `WindowConfig::openGLContext` go. **This is a breaking change** —
      `RenderBackend::Default` no longer means "try Vulkan, fall back to GL", and
      its documentation must say what it does mean.
- [ ] 144.2 **Remove GL device creation and window setup** in `Render.cpp` and
      `Window.cpp`. `RenderLayer` must fail with a clear logged message when
      Vulkan is unavailable — **never a crash, never silence**.
- [ ] 144.3 **Simplify `DepthConvention`.** The `[-1, 1]` clip case exists only
      for GL. **Reverse-Z (T0130) must survive unchanged** — verify with the
      existing depth tests before and after, and do not fold the two changes
      together. If the simplification looks like it alters depth behaviour at
      all, stop and record rather than proceeding.
- [ ] 144.4 **Remove the GL branches in the shader path** —
      `combinedSamplers = IsGLDevice()` in `SurfacePipeline.cpp` and
      `ShaderSources.cpp`, and the dual-compiler selection D28 introduced.
      **After this, `HpSurface.slang` is no longer constrained to the HLSL subset**
      — but do **not** start using Slang-only constructs here. That is T0142.2,
      and it must be a separate ticket with its own verification.
- [ ] 144.5 **Drop `Diligent-GraphicsEngineOpenGL`** from the link in
      `engine/CMakeLists.txt`, and any GL staging from `dist`.
- [ ] 144.6 **Remove GL cases from the tests.** Several GPU tests parameterise
      over both backends; `gpu_adapter_report_test.cpp` reports both. Remove the
      GL half rather than leaving skipped cases.
- [ ] 144.7 **Update the documents.** `01-project-overview.md`'s backend table
      says "Vulkan, OpenGL" for both targets. `Render.hpp`'s own comments
      describe the fallback. `05-verification-status.md` may reference GL
      coverage. D1 is amended by D29 — add the cross-reference **in D1**, not
      only in D29.
- [ ] 144.8 **Check the sysroot.** `third_party/sysroot/` carries X11/GL for the
      hermetic Linux build (see CLAUDE.md's trap about SDL auto-detecting the
      host). Removing the GL *backend* does not necessarily mean removing GL from
      the sysroot — SDL may still need it to create a window. **Verify before
      deleting anything there**; a broken sysroot fails in a way that looks
      unrelated.

## Notes / findings

### The one risk that was checked and cleared

Hyper-V's GPU-P supports DX11 and OpenGL but **not** Vulkan; RDP forwards no
acceleration; Windows has no ubiquitous software-Vulkan fallback the way GL has
GDI-generic. So a team developing or testing inside VMs would lose the ability to
run the engine at all. **The owner confirmed nobody does**, which is what made
the decision safe.

Related and still true: `gpu_adapter_report_test.cpp` records the CI Windows host
as *"gdi generic — Windows' generic OpenGL 1.1"*. GPU tests are built but never
run in CI, so this does not block — but **GPU tests can never run on that runner
after this ticket** until it has a real Vulkan device.

### What is lost

Intel Haswell and Broadwell on Windows (2013–14) never had an official Vulkan
driver, and are lost outright. So is any machine whose OEM driver package never
registered a Vulkan ICD on otherwise-capable hardware.
