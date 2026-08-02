# Task log

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
- ✅ **Incremental exact** — no-op → `ninja: no work to do`; one `.cpp` → 8 steps;
  hot header → 246 of ~1100
- ✅ **Hermetic Linux** — X11/GL resolved from the vendored sysroot, not `/usr`
- ✅ **glibc pinned** — max symbol requirement `GLIBC_2.27` against a 2.28 target
- ✅ **RPATH clean** — no sysroot stub directory (G6)
- ✅ **Bootstrap** — installs zig + cmake + ninja, checksum-verified

### Libraries ✅ (compile/link on both targets)
| Library | Version | Target(s) |
|---|---|---|
| Dear ImGui (docking) | 1.92.9b | `Diligent-Imgui` |
| EnTT | 3.16.0 | `EnTT::EnTT` (via DiligentFX) |
| enkiTS | e3329cc | `enkiTS` |
| meshoptimizer | 300f7d3 | `meshoptimizer` |
| ozz-animation | 0.17.0 | `ozz_base`, `ozz_animation`, `ozz_geometry`, `ozz_animation_offline` |

### ImGui docking, end to end ✅
`apps/imgui_probe` built **and ran** under Xvfb:

```
Diligent Engine: Info: Initialized OpenGL 4.5 context (llvmpipe)
HP_PROBE: 120 frames rendered, ImGui 1.92.9b, docking ON -- exiting
```

Screenshot confirmed the scene rendering, the Settings window, the Docking Probe
window reporting `docking enabled: yes`, and an Adapters window docked into the
dockspace. `1.92.9b` (not Diligent's 1.92.1) proves the swap took effect.

---

## Not verified — do not claim these work

- ⚠️ **Windows host.** All Windows-host paths (`bootstrap.ps1`, `.cmd` shims,
  the case-sensitivity probe taking its false branch) are written but have
  **never been executed**. Only Linux→Windows cross-compilation is proven.
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

## Known incomplete

- 🔜 **No real application.** `apps/imgui_probe` is a disposable smoke test.
- 🔜 **`ufbx` and `third_party/dxc` are wired into nothing.** Present in the tree,
  referenced by no build rule.
- 🔜 **Linux sysroot is x86_64 only.** Add `third_party/sysroot/linux-aarch64/`
  plus a toolchain file to target ARM.
- 🔜 **`configure` needs network.** DiligentFX fetches EnTT and DiligentTools
  fetches rapidjson via `FetchContent` at configure time. Builds are offline
  afterwards. Pre-populate with `FETCHCONTENT_SOURCE_DIR_ENTT`.

---

## Temporary things to remove later

- `apps/imgui_probe/` — delete once a real app exists
- The `ImGuiKey_Mod*` compile definitions in the root `CMakeLists.txt` — delete
  once DiligentEngine updates its Linux/Emscripten ImGui impls (G7)

---

## Next steps

1. Run `ImGuiProbe.exe` under wine to close the Windows verification gap
2. Verify `zig build dist` for the Windows target
3. Try `--mode vk` on real hardware — the one backend never exercised
4. Decide what the real application is, and delete the probe
5. Wire up `ufbx` if FBX loading is still wanted (ozz has its own importers, but
   they are off because the FBX pipeline needs the proprietary FBX SDK)
