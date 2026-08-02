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

**Correction (2026-08-03):** it was stated several times during planning that
"ImGui never ships". That is wrong, and the PUBLIC link above is why — ImGui is
linked into the *runtime* binary too, not just the editor. What does not ship is
the editor's *UI code*. This matters for T0069: the argument against using ImGui
for player-facing UI is that it is unsuitable (immediate mode, not built for
shipping UI), not that it is unavailable.

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

## D9 — `apps/imgui_probe` was a deliberately disposable smoke test *(retired)*

A copy of `Tutorial10_DataStreaming`, chosen because it drives ImGui hard **and**
calls `ImGui::InputInt(..., ImGuiInputTextFlags_EnterReturnsTrue)` — the exact
path behind the assert Diligent's fork used to comment out.

One marked edit (`HP_DOCKING_PROBE`) enables docking, creates a
`DockSpaceOverViewport` and a second dockable window, and honours
`HP_PROBE_EXIT_FRAMES` for headless runs. Those docking APIs do not exist in
ImGui 1.92.1, so **the build itself proves the swap took effect**.

**Retired in T0007** (2026-08-02), on request, before a real app existed. It
earned its keep first: it caught G7 (removed ImGui modifier aliases), G8
(mandatory `readme.md`), the sysroot glibc mismatch (D4) and the RPATH-to-stubs
bug (G6) — none of which a compile-only check would have found.

Consequence to be aware of: with it gone the build produces **no executable**, so
there is currently no way to catch a *runtime* regression in the harness. The
ImGui/Vulkan/wine evidence already recorded stands, but it can no longer be
re-run. Recover the file from git history if that becomes a problem before T0006
lands.

---

## D10 — Layered communication: direct access, signals, and a message bus

**Decision:** three mechanisms, each used where it is strongest, rather than one
universal one.

| Use | Mechanism |
|---|---|
| Reading another entity's state | **Direct component access** — `e.Get<Health>()`, no messaging |
| Authored 1:N with a known partner (door ← switch) | **Signals** (T0072) — visible, serializable, editor-wireable |
| Unknown audience: tag, radius, or broadcast | **Message bus** (T0075) with addressing |

**Rejected — a single global bus for all entity communication.** The addressing
model is genuinely valuable and is why T0075 exists, but making it the *only*
mechanism carries costs that bite hardest where the alternatives are cheapest:

- **"Who handles this?" stops being answerable from the call graph.** With a
  signal you inspect the connection; with a bus you grep. This is the well-known
  failure mode of global event buses, and it works against keeping the codebase
  legible.
- **Message chains cost frames.** Deferred dispatch avoids reentrancy, but
  `damage → death → loot → UI` becomes four frames. Immediate dispatch fixes that
  and reintroduces reentrancy.
- **Queries do not fit.** "What is your health?" is request/response, not publish.
  Forcing it through a bus is painful, so direct access returns anyway — the bus
  becomes an *additional* mechanism rather than the single one, which was the
  point.
- **Order becomes semantics**, and deterministic ordering across parallel systems
  is hard — a real problem if replay or networking ever matters.
- **Typed payloads are worth keeping.** A universal bus tends toward untyped
  messages, discarding compile-time checking in a language chosen partly for it.

**What the idea contributed, and was kept:** addressing by tag, radius and
broadcast, which per-entity signals cannot express without manual fan-out. That
required a tag system (T0074), which turns out to be independently useful for
queries, filtering and content authoring.

Two mitigations for the bus's traceability cost are built into T0075: a debug
view of recent messages and their subscribers, and Tracy integration — a message
log is a real debugging asset.
