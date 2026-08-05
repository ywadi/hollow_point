# Verification status

Last updated: 2026-08-03

Status markers: ✅ VERIFIED (observed, with evidence) · ⚠️ UNVERIFIED (believed,
never exercised) · ❌ BROKEN · 🔜 TODO

---

## Done and verified

### Build harness ✅
Zig-based, incremental, cross-compiling from either host. See
`03-build-harness.md`.

- ✅ **Linux x86_64** — full build, exit 0, ~1111 targets
- ✅ **Windows x86_64, cross-compiled from Linux** — full build, exit 0,
  1105/1105 targets. `ImGuiProbe.exe` is `PE32+ executable (console) x86-64`;
  `GraphicsEngineVk_64r.dll`, `GraphicsEngineOpenGL_64r.dll`,
  `Archiver_64r.dll`, `SuperResolution_64r.dll` are `PE32+ (DLL)`
- ✅ **Windows x86_64, built on a Windows host** — full build, exit 0,
  1089/1089 targets (T0004). `Diligent-RenderStatePackager.exe` was **run
  natively**, the first execution of this project's output on Windows without
  wine. All four DLLs `PE32+ (DLL)`, 92 static libraries
- ✅ **Linux x86_64, cross-compiled from Windows** — the last quadrant of the
  D3 matrix, full build, exit 0, 1095/1095 targets (T0004). Produced
  `Diligent-RenderStatePackager` (`ELF 64-bit LSB executable, x86-64`) which
  **runs under WSL**; max symbol requirement `GLIBC_2.27`, no RPATH — matching
  the Linux-host build exactly. Required two fixes, both landed: sysroot stubs
  are copies rather than symlinks, and precompiled headers are disabled for
  ELF targets on a Windows host
- ✅ **Incremental exact** — no-op → `ninja: no work to do`; one `.cpp` → 8 steps;
  hot header → 246 of ~1100
- ✅ **Hermetic Linux** — X11/GL resolved from the vendored sysroot, not `/usr`
- ✅ **glibc pinned** — max symbol requirement `GLIBC_2.27` against a 2.28 target
- ✅ **RPATH clean** — no sysroot stub directory (G6)
- ✅ **Bootstrap** — installs zig + cmake + ninja, checksum-verified
- ✅ **Dual-host `.harness/`** — installs are keyed by host
  (`.harness/<tool>/<host-key>/<version>/`) and no longer destroy each other
  (T0102). A synthetic Windows toolchain survived a full Linux re-bootstrap
  intact, where the old shared layout deleted it; `bootstrap.ps1` was then run
  end to end on a real `windows-latest` runner under `pwsh 7` and installed to
  `.harness\zig\windows-x86_64\0.16.0\`. `tests/harness/pins_test.zig` fails if
  either script's pins or layout drift from `build.zig`, checked by mutating
  each and watching it catch them. **Not** proven: one physical machine running
  both bootstraps into one tree and building from each in turn — and as of
  **D18** (T0122) it never will be, because that configuration is no longer
  supported. Under WSL the tree must live on the Linux filesystem, so each host
  gets its own checkout. The host-keying stays because it is correct and cheap,
  not because a shared tree works. A retired scenario, not an open gap
- ✅ **Fully offline** — configure and build need no network. EnTT and abseil-cpp
  are vendored at Diligent's pinned refs and wired via `FETCHCONTENT_SOURCE_DIR_*`.
  Proven inside a network namespace (`unshare -r -n`) and with
  `FETCHCONTENT_FULLY_DISCONNECTED=ON`; `_deps/` contains no fetched sources

### Libraries ✅ (compile/link on both targets)
| Library | Version | Target(s) |
|---|---|---|
| Dear ImGui (docking) | 1.92.9b | `Diligent-Imgui` |
| EnTT | 3.16.0 | `EnTT::EnTT` (via DiligentFX) |
| enkiTS | e3329cc | `enkiTS` |
| meshoptimizer | 300f7d3 | `meshoptimizer` |
| ozz-animation | 0.17.0 | `ozz_base`, `ozz_animation`, `ozz_geometry`, `ozz_animation_offline` |

### Continuous integration ✅
GitHub Actions (T0084). Every workflow has run green at least once:

- ✅ `ci.yml` on push — Linux-host tests (both target suites, the Windows one
  under **wine on a real Linux runner**), Windows-host tests (`bootstrap.ps1`,
  the `.cmd` shims and the case probe's false branch, natively), and a
  configure that asserts no dependency was downloaded
- ✅ `full-build.yml` on dispatch — full ~1100-target build of both targets,
  `dist` staging, artifact upload
- ✅ **Failures block, and are diagnosable from the log alone.** The first run
  failed; the root cause (`build.zig` adopting an old ccache off the runner's
  PATH) was found from the CI output without reproducing locally

Not measured: job durations. The only datum is a 282s Windows configure on the
runner against ~28s locally, unexplained. Not set up: publishing to GitHub
Releases — `dist/` is a 7-day workflow artifact, which is a different thing.

### Test harness ✅
`zig build test` (T0012). Two runners: doctest 2.5.3 binaries per bucket for
project code, and Zig's own test runner for the build harness. Verified by
red-green, not by observing a pass:

- ✅ **A failing test fails the build.** Exit 1, and the output names the
  assertion with file:line (`tests/fast/…:4: ERROR: CHECK( answer == 42 )`).
  Confirmed independently for the C++ and Zig runners
- ✅ **The dist tests catch a real regression** — pointing `dist.cmake`'s
  Windows DLL destination at `lib/` instead of `bin/` failed exactly the test
  that claims to cover it, and only that test
- ✅ **Adding a test needs one file.** A new `.cpp` dropped into `tests/fast/`
  was picked up with no CMake edit and no manual reconfigure
- ✅ **Both targets run.** `test (linux-x86_64, fast)` and
  `test (windows-x86_64, fast)` both pass; the Windows suite executed through
  WSL interop as a real Windows process
- ✅ **Full suite green** — 12/12 steps, 10/10 tests across the fast and
  integration buckets

### enkiTS and meshoptimizer, exercised ✅
Previously recorded as "compile and link, but no code calls them". The first
C++ tests call them, **on both targets**: meshoptimizer's vertex remap welds a
6-vertex quad to 4 and the remapped indices still describe the original corners;
`meshopt_optimizeVertexCache` permutes triangles without changing which vertices
each references; enkiTS runs a 10,000-element `TaskSet` with every element
visited exactly once. 10,029 assertions, Linux and Windows.

### ImGui docking, end to end ✅
`apps/imgui_probe` built **and ran** (since retired — see T0007; the evidence
below stands but can no longer be reproduced without restoring it from git):

```
Diligent Engine: Info: Initialized OpenGL 4.5 context (llvmpipe)
HP_PROBE: 120 frames rendered, ImGui 1.92.9b, docking ON -- exiting
```

Screenshot confirmed the scene rendering, the Settings window, the Docking Probe
window reporting `docking enabled: yes`, and an Adapters window docked into the
dockspace. `1.92.9b` (not Diligent's 1.92.1) proves the swap took effect.

---

### Rendering, end to end ✅ (2026-08-05)

**A lit mesh reaches the screen.** Verified by reading pixels back, not by
watching a frame counter — the distinction matters here more than usual, because
three separate bugs have now survived a frame where every statistic agreed a draw
had been issued.

| Claim | Evidence |
|---|---|
| A glTF mesh loads through the VFS and rasterises | 65,536 / 65,536 pixels differ from the clear colour (T0028) |
| Reverse-Z is right | It draws at all. Under a mismatched comparison **nothing** would (T0130/T0028) |
| Layers composite in order | Right half is clear-colour with the world alone, covered with the HUD (T0027) |
| Object layer masks filter | One scene, two cameras, each drawing only its own layer (T0085) |
| **Surfaces are actually shaded** | Red quad + white light = `(211, 144, 144)`; no light = `(0, 0, 0)` (T0079) |
| Point-light attenuation | 2 units `(255,145,145)`, 9 units `(80,46,46)`, past range `(0,0,0)` |
| Spot cone falloff | Wide cone `(255,251,251)`, narrow cone `(0,0,0)` |

**Until T0079 landed, this engine rendered every mesh pure black** — no lights,
no IBL, no emissive — and it is worth knowing why that went unnoticed for three
tickets: black differs from a blue clear colour, so "did geometry arrive" passed
while nothing about shading was tested at all. T0028's headline evidence is still
true and still proves less than it reads.

### GPU testing — hardware coverage, and how to know what you got ✅/⚠️ (T0135)

**You no longer have to read this to find out what the gpu bucket ran on — the
bucket says so itself.** Every `-Dtest=gpu` run prints a banner naming each
backend, its adapter, and whether that adapter is a software rasteriser:

```
[hp gpu] ---- adapters this run (T0135) ----
[hp gpu] Vulkan  -> NVIDIA GeForce RTX 2080  [hardware]
[hp gpu] OpenGL  -> NVIDIA GeForce RTX 2080/PCIe/SSE2  [hardware]
[hp gpu] ------------------------------------
```

Add **`-Dgpu-require-hardware`** to turn a software adapter into a failure —
use it before closing a rendering ticket. It is opt-in rather than the default
because one of this project's two development machines has no Vulkan hardware
path at all, and a permanently-red bucket is one people stop running. The
reasoning and the rejected alternatives are on T0135.

**Coverage depends on the machine, and the two differ completely.** Both are
real and both are supported; the harness detects which it is on.

| Host | Backend | Device | Status |
|---|---|---|---|
| Pop!_OS desktop (bare metal) | Vulkan | `NVIDIA GeForce RTX 2080` | ✅ hardware |
| Pop!_OS desktop (bare metal) | OpenGL | `NVIDIA GeForce RTX 2080/PCIe/SSE2` | ✅ hardware |
| WSL laptop | OpenGL | `D3D12 (NVIDIA GeForce RTX 4070 Laptop GPU)` | ✅ hardware, needs the d3d12 path |
| WSL laptop | Vulkan | `llvmpipe` | ⚠️ **software — no hardware path exists** |

On the desktop, **both backends reach hardware with no environment variables at
all**; 14 gpu cases and 652 assertions pass there, which is the first hardware
evidence this project has had on the **Vulkan** backend.

On WSL, Mesa cannot find the GPU on its own — there is no `/dev/dri` node, so
the probe falls back to `swrast` — and the harness now sets `GALLIUM_DRIVER=d3d12`
automatically when it detects that host (`/dev/dxg` present, `/dev/dri` absent).
`-Dgpu-adapter=NVIDIA` names which GPU on a two-GPU laptop, where Mesa's default
is the integrated one. **Neither is applied on a host with a DRM node**, where
they would be wrong.

**Vulkan has no hardware path in WSL at all**, and it is not a misconfiguration:
NVIDIA's WSL package ships D3D12 and CUDA but **no Vulkan ICD**, and Mesa's Dozen
is not in Ubuntu's `mesa-vulkan-drivers`. The only loadable ICD is lavapipe.

**Two closed tickets claimed hardware they never had** (T0027 said "a real GPU",
T0028 said "an NVIDIA RTX 4070"); both are corrected in place.

**One thing the desktop cannot test:** forcing its *OpenGL* path to software.
`LIBGL_ALWAYS_SOFTWARE=1` is a Mesa variable and NVIDIA's proprietary GL driver
ignores it, so the software-classification path was proven on Vulkan (via
`VK_ICD_FILENAMES=…/lvp_icd.json`, which correctly failed under
`-Dgpu-require-hardware`) and not on OpenGL.

### What the editor shows ⚠️

`hp_editor --backend=opengl` displays a lit quad. **The `--backend` flag is not
optional**: the dev present path is a `CopyTexture` needing an exact format
match, and a Vulkan surface is BGRA against the scene target's RGBA, so Vulkan
shows a clear colour and looks broken. T0033 deletes that path by sampling the
texture in a shader.

The scene is a **throwaway generated at startup** into a temp directory — there
is no content pipeline and **no committed asset of any kind** in this repository.
Every mesh rendered to date was written by code at run time.

## Not verified — do not claim these work

- ⚠️ **Windows host — now largely proven; two caveats remain.** `bootstrap.ps1`,
  the `.cmd` shims and the case probe's false branch all executed correctly
  (T0004), so the D3 matrix is complete. But the working tree used was created
  by **WSL git**, not by a Windows clone, and the run was driven through WSL
  interop rather than from a Windows shell. A clone made with Git for Windows
  has not been built. Configure also requires **Python on `PATH`** (it runs
  `pip install jinja2` for DiligentCore's codegen), which `BUILDING.md` does
  not mention.
- ⚠️ **The Windows `.exe` has never been run.** It links and has the right
  format; that is all that is known. `wine` is available at `/usr/bin/wine` and
  was going to be tried.
- ⚠️ **Vulkan backend.** The headless run used OpenGL via llvmpipe. `--mode vk`
  is untested.
- ⚠️ **ozz** compiles and links, but **no code calls it**. Its API has never
  been exercised. (enkiTS and meshoptimizer no longer belong here — see below.)
- ⚠️ **`dist` on Windows** — the Linux path is verified (4 shared + 82 static);
  the Windows DLL staging is not.
- ⚠️ **aarch64 Linux host** — bootstrap has hashes for it; never run.

---

## Known incomplete / next steps

These moved to [backlog/](../backlog/README.md) — one file per task, with rationale
and subtasks:

| | |
|---|---|
| Run the Windows exe under wine | [T0001](../backlog/completed/0001-run-windows-exe-under-wine.md) |
| Verify Windows `dist` staging | [T0002](../backlog/completed/0002-verify-windows-dist-staging.md) |
| Verify the Vulkan backend | [T0003](../backlog/completed/0003-verify-vulkan-backend.md) |
| Verify building on a Windows host | [T0004](../backlog/completed/0004-verify-windows-host-build.md) |
| Actually call enkiTS / meshoptimizer / ozz | [T0005](../backlog/completed/0005-exercise-new-library-apis.md) |
| Define the real application | [T0006](../backlog/completed/0006-define-real-application.md) |
| Retire `apps/imgui_probe` | [T0007](../backlog/completed/0007-retire-imgui-probe.md) |
| Remove the `ImGuiKey_Mod*` shim | [T0008](../backlog/open/0008-remove-imgui-modifier-shim.md) |
| Wire up or drop `ufbx` | [T0009](../backlog/completed/0009-wire-up-ufbx.md) |
| Make `configure` work offline | [T0010](../backlog/completed/0010-offline-configure.md) |
| Add an aarch64 Linux target | [T0011](../backlog/open/0011-aarch64-linux-target.md) |

This file records **what is proven and what is not**. The backlog records **what
to do about it**. Keep the two separate: a task being open is not the same as a
capability being broken.
