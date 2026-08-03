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

---

## D11 — doctest for C++ tests; Zig's own runner for the harness

The test framework question (T0012.1) turned out to be two questions, because
the first tests worth writing are not C++ at all.

**Harness logic uses Zig's built-in test runner.** `cacheHas()` is a Zig
function in `build.zig`, and `dist.cmake` is a `cmake -P` script. Testing either
through a C++ framework would be absurd. `zig test` costs no new dependency and
gives file:line failures, so `cacheHas` moved to `tools/harness/cache.zig` where
both `build.zig` and a test can import it. `dist.cmake` is driven as a
subprocess against a synthetic build tree rather than reimplemented — what is
worth testing is the behaviour of the exact file `zig build dist` runs.

**Project code uses doctest 2.5.3**, vendored as a submodule. It is a single
header, so it is consumed by adding an include directory rather than by adding
its CMake project — its install rules, package-config generation and its own
test targets are all things this project does not want, and skipping them keeps
`third_party/doctest` entirely unexecuted.

**Rejected — Diligent's vendored GoogleTest 1.16.0.** It is already on disk at
`DiligentCore/ThirdParty/googletest`, which is genuinely tempting. But consuming
it means reaching into the engine submodule's internal directory layout, which
breaks whenever Diligent reorganises, and it defines `gtest`/`gtest_main` target
names that would collide if Diligent ever enables its own tests. That is exactly
the coupling D1 exists to avoid, and it would be paid on every build for the
rest of the project's life to save one submodule.

**Rejected — Catch2 v3.** Its tag system is genuinely better than doctest's:
real multi-dimensional tags with boolean selection (`[gpu]` but not `[slow]`),
where doctest has only a single `test_suite` per case. The reason it lost is
that v3 is a compiled library and materially slower to compile per translation
unit — which works against the same goal that made tags attractive. See below.

**Buckets are separate executables, not tags.** This is the part worth
remembering. The concern that motivated tags was "this will become a huge system
and we do not want to wait hours", and a tag only ever saves you the *running*
of a test — you still pay to compile and link it. A separate binary per bucket
(`fast`, `integration`, `gpu`, `perf`) costs nothing at all when you are not
running it, and `zig build test` builds the bucket by name so Ninja resolves
only that executable's dependencies. With the coarse split handled there, the
remaining need for tags is weak enough that doctest's simpler model suffices.
This also satisfies T0012's requirement that GPU tests stay out of the default
suite, structurally rather than by convention.

**Cross-target execution** mirrors D3's build matrix: the suite that only runs
for the host target proves half of what it should. A Windows suite runs on a
Linux host via WSL interop if available (a real Windows process — better than
emulation, and proven in T0004), else wine (proven in T0001), else it is built
but not run *with a warning*, never a silent pass.

---

## D12 — Engine is a shared library; gameplay links it in lockstep, guarded by a build id

**Decision:** the engine is built as a **shared** library (`.so`/`.dll`). The
editor, the runtime and every gameplay module link against that one copy, so
engine state exists exactly once in a process. Gameplay modules are C++ against
the engine's real headers — **not** a C ABI — and engine plus modules are always
built and shipped together. A build id is stamped into the engine and into every
module, and a module whose id does not match is refused at load, loudly.

Proven before deciding (T0095), on both targets under the pinned zig toolchain:
one address for an engine global seen from the executable and from a
`dlopen`/`LoadLibrary`'d module, mutations visible both ways, and an entt
component emplaced by the engine iterable from the module. Windows needed no
import library, no `.def` and no export list — naming the DLL on the link line
was enough.

**Rejected — engine as a static library** (what T0013 originally assumed).
Linking it into both the executable and the gameplay module duplicates every
engine global: the logger, the autoload registry, asset pool statics. Each side
then mutates its own copy and neither sees the other. This is not a build error;
it is a runtime behaviour that looks like impossible bugs in gameplay code.

**Rejected — the executable exports its own symbols and modules link against
it.** Works on Linux with `--export-dynamic`; on Windows it means linking a DLL
against the exe's import library, which is possible under MinGW and unusual
enough to be fragile. Since engine-as-shared-library was measured working on
both platforms, the asymmetric option buys nothing and costs a platform-specific
code path.

**Rejected — a C ABI boundary, as Godot's GDExtension uses.** Godot needs it
because extensions must survive engine updates they were not compiled against,
and the price they pay is `godot-cpp`: an entire generated binding layer,
because a raw C ABI is miserable to write gameplay against. Lockstep removes the
requirement that creates that cost. Engine and gameplay are compiled together,
so rich C++ — real types, templates, entt registries — crosses the boundary
directly and no binding layer is needed. **The build id is what makes this
safe:** without it, a stale module against a changed engine is silent memory
corruption; with it, the failure is a refusal at load with both ids printed.

**Consequence, accepted deliberately:** shipping a gameplay update means
shipping the engine too. For a project that publishes its own games this is
already how releases work. It does mean third-party modules compiled against a
different engine build can never be loaded — see D14.

---

## D13 — All content addressed through a virtual filesystem (PhysicsFS)

**Decision:** every asset read goes through a virtual filesystem, never
`std::filesystem` or `fopen` directly. **PhysicsFS** (zlib licence, vendored
like everything else) provides it. Loose directories are mounted during
development and archives when shipping, and because both are mounts, dev and
shipped builds run the *same* code path. Mount order decides who wins when a
path exists in more than one source, which is the whole mechanism for patches
and DLC: ship a later archive and it overrides earlier ones per file.

**Rejected — writing our own pack format and VFS.** It is a well-understood
problem with a mature answer. PhysicsFS is Quake 3-inspired, handles ZIP plus
PAK/7z/WAD and native directories, sandboxes all writes to a designated write
directory (which T0083's saves want anyway), offers `PHYSFS_setRoot` to mount a
subset of an archive, and has been API/ABI stable back to 1.0. It also builds
for Switch and PS5, which costs nothing now and is worth having if consoles ever
happen.

**Rejected — exporting a folder of loose files** (what T0043 originally
described). It ships fine, but it makes patching and DLC into separate features
invented later, and it lets asset code call the filesystem directly — after
which introducing a VFS means touching every read site. The order matters: the
VFS has to exist *before* T0023 hardens, not after.

**Not what this replaces.** PhysicsFS is file I/O, not an asset database. GUIDs,
metafiles, reference counting and hot reload (T0023, T0058) remain ours. The
division is deliberate: it owns *where bytes come from*, we own *what the bytes
mean*.

**Open, and flagged rather than assumed:** PhysicsFS is a C library with global
init state, and its guarantees for concurrent reads were not confirmed. That
matters once the job system (T0026) wants async loading, and is worth a spike
before the first threaded loader, not after.

---

## D14 — No scripting language; gameplay is C++, composed from data

**Decision:** there is no embedded scripting VM. Gameplay logic is C++ in the
gameplay module. New content is expressed by **composing existing behaviours
with new data** — a new enemy is existing behaviours with different stats, mesh
and animations — and genuinely new mechanics arrive as a game update, which
under D12 already ships the binary.

**Rejected — Lua (with sol2), Wren, or AngelScript.** Any of them would be a
reasonable embed. The reason none is taken: the requirement they satisfy is
"install DLC or mods without shipping a new binary", and that is not a goal
here. Adding a VM to satisfy a requirement nobody has is cost without benefit,
and the cost is not only the integration — it is that gameplay authored in
script tends to migrate onto the hot path, where a per-entity per-frame call
across a native↔VM boundary is exactly what this project cannot afford with the
unit counts implied by T0093 and T0098.

**Consequence, accepted:** no Steam-Workshop-style user-generated content
without revisiting this. Assets-only DLC still works through D13 and needs no
code; it is *new behaviour* that requires a build.

**Kept regardless of this decision:** behaviours are registered and referenced
by **stable name**, not by C++ type identity alone. This was first considered as
a hedge for a future script backend, which undersold it — it is required
independently by serialization. T0095 measured that `entt::type_index` is a
per-module runtime number that differs across the module boundary and cannot be
persisted, so scenes and prefabs must reference behaviours by name whatever else
is true. See T0053 and T0062.
