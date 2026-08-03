# Verification status

Last updated: 2026-08-02

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
- ⚠️ **enkiTS / meshoptimizer / ozz** compile and link, but **no code calls
  them**. Their APIs have never been exercised.
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
| Verify building on a Windows host | [T0004](../backlog/open/0004-verify-windows-host-build.md) |
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
