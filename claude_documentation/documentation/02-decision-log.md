# Decision log

Every entry records what was rejected and why. If you are about to change one of
these, read the rationale first — most were chosen against a specific failure.

---

## D1 — Zig as the compiler, CMake + Ninja retained

**Amended by D29 (2026-08-06):** the "Vulkan and OpenGL on both targets"
half of this era's setup is gone — OpenGL is removed and Vulkan is the only
backend. D1's substance (zig/MinGW, CMake+Ninja, cross-compiling from either
host) is untouched.

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

**Amended by D29 (2026-08-06): OpenGL is removed on both targets.** The
Windows target is Vulkan only. What survives of this entry is the other half —
no Direct3D, and why.

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

---

## D15 — Particles simulate on the GPU only, and are purely cosmetic

**Decision:** the particle system simulates on the **GPU** via compute shaders.
There is no CPU simulation path, not even as a fallback. In exchange, **particles
are cosmetic: gameplay never reads particle state**, and nothing in the
simulation feeds back into the ECS.

The game is 3D, and VFX are billboarded sprites rather than 2D sprite content.

**Rejected — CPU simulation with the job system**, which is what T0080
originally recommended as a starting point. The reasoning there was that CPU is
simpler, debuggable, and lets gameplay read particle state, and that it is fine
for thousands of particles. The counter-argument that wins: "fine for thousands"
is the problem, not the reassurance. An explosion with smoke, embers and
distortion is tens of thousands of particles on its own, and a CPU path that
works until the third simultaneous explosion is a performance cliff discovered
during a demo. Building it CPU-first also means the render path quietly acquires
CPU-only assumptions — per-particle writes into a mapped vertex buffer, sorting
on the CPU — and each becomes a rewrite when the numbers force the move.

**Rejected — a hybrid with CPU for small emitters.** Superficially attractive,
because a muzzle flash is a handful of particles and a compute dispatch per tiny
emitter is wasteful. It is rejected because two simulation paths means two
implementations of every feature — curves, emission shapes, sorting, budgets —
and they will drift. The correct answer to the small-emitter problem is a
**unified** system where all emitters share buffers and are simulated by one
dispatch, not a second code path.

**What this costs, accepted deliberately:**

- **No exact collision.** GPU particles collide against the depth buffer, which
  is screen-space and approximate: particles collide with what the camera can
  see and pass through everything else. Exact scene collision needs an SDF or
  voxel representation, which is its own project.
- **No readback into gameplay.** This is the enabling constraint above rather
  than a limitation to work around. If an effect must drive gameplay — an
  explosion that damages — gameplay computes that itself from the *event*, and
  the particles merely depict it. The two never share state.
- **Non-determinism.** GPU simulation is not bit-reproducible across hardware,
  so particles can never participate in anything requiring determinism
  (T0070 networking, replays). Follows from them being cosmetic.
- **Harder debugging.** Particle state lives in GPU buffers. A debug readback
  path for development is worth building early, and it is a *tool*, not a
  simulation fallback.

Diligent supports compute shaders on both backends (Vulkan and OpenGL 4.3+),
which is what makes this available on both targets. **The OpenGL floor moves:**
compute requires 4.3, and the fallback path assumptions in T0025 should be
checked against that.

---

## D16 — SDL3 for window, input and audio backend, not DiligentTools NativeApp

**Decision:** the window, input and platform layer is **SDL3**. Diligent renders
into an SDL-created window via its native-handle interface; `DiligentTools/NativeApp`
is not used.

This works because Diligent's device and swap chain creation is **decoupled from
window creation** — `LinuxNativeWindow` takes an X11 `WindowId` plus a `Display*`,
and the Win32 equivalent takes an `HWND`. Diligent attaches to a window made by
anything, so choosing SDL costs nothing in engine integration. T0015 framed the
choice as "NativeApp versus writing our own `IWindow` over GLFW", which was the
wrong pair: the real choice was NativeApp versus a platform library, and Diligent
supports both identically.

**Rejected — `DiligentTools/NativeApp`**, which T0015 originally specified. It
provides window creation and basic keyboard/mouse across an impressive platform
list, and it already cross-compiles in this harness. What it does not provide,
verified by grepping the vendored tree during T0068: **no gamepad, joystick or
XInput code at all**, no clipboard, no DPI or multi-monitor helpers, no relative
mouse capture, no audio. Each of those becomes per-platform code we write and
maintain forever. T0068 had already concluded gamepad meant XInput on Windows
plus raw `/dev/input` scanning on Linux, because udev is not in the vendored
sysroot (D3/D4) — hand-written hot-plug enumeration, for a solved problem.

**Rejected — GLFW.** The closer call, and it was the initial recommendation.
GLFW covers window, keyboard, mouse, clipboard, DPI, monitors, relative mouse,
and gamepads including the SDL_GameControllerDB mapping database and hot-plug
callbacks. It is smaller and simpler than SDL. It lost on one specific
capability: **GLFW has no rumble, haptics or force-feedback API at all**, nor
battery, gyro or touchpad. Controller rumble is wanted, and bolting it on means
platform-specific code (`XInputSetState`, Linux force-feedback ioctls) — which
is precisely the class of work this decision exists to avoid. GLFW also needs
Xrandr, Xinerama, Xcursor, Xi and xkbcommon, none of which the vendored sysroot
currently has.

**Not a reason, stated so it is not later believed:** SDL is *not* faster than
GLFW or NativeApp. All three are thin shims over the same OS calls, and none is
in a hot path — windowing and input happen once per frame, not per draw. This
decision is about capability, not performance.

**What it brings beyond input:**

- **Rumble** — `SDL_RumbleGamepad()`, and `SDL_RumbleGamepadTriggers()` on
  hardware that supports it
- **Audio**, which partially answers T0052's open library question. SDL's audio
  is lower-level than a dedicated library, so a higher-level layer over it (or a
  separate library) may still be wanted; the point is that the platform backend
  is no longer a blocker
- Clipboard, DPI awareness, monitor enumeration, fullscreen/borderless handling

**Costs, accepted:**

- A larger dependency than GLFW, vendored and cross-compiled like everything
  else. SDL is very well tested under MinGW, which is the risk that matters here
  (G2/G3/G4)
- SDL3 is a different API from SDL2 — `SDL_GameController` became `SDL_Gamepad`
  and much else was renamed. Pin SDL3 and do not follow SDL2 examples
- **Unverified:** SDL is understood to `dlopen` most X11 dependencies at runtime
  rather than linking them, which would avoid the sysroot additions GLFW needed.
  That should be confirmed when it is vendored, not assumed — the hermetic-Linux
  property (D4, and a verified item in `05-verification-status.md`) depends on
  knowing exactly what is resolved and from where

SDL3 has been stable since January 2025 and is actively maintained.

---

## D17 — The frame has one owned anatomy, and unbuilt phases are named in advance

**Decision:** `Application::run` implements a single documented phase order —
[`08-frame-anatomy.md`](08-frame-anatomy.md), owned by T0100. Phases whose
systems do not exist yet are present in the loop *now*, as named profiler zones
with a comment naming their owning ticket, doing nothing.

The order, abbreviated: tick → poll → **fixed-step block** (0..n) → update →
structural apply → deferred drain → transform propagate → **late update** →
transform propagate → render → present → **end-of-frame safe point** → frame end.

**Rejected — let each system pick its own point when it lands.** This is the
default, and it is how the classic failures happen: one-frame-lag chains, a
camera reading a transform before the thing it follows has moved, a scene
transition applied mid-iteration, a module reload while a queue still holds
module-typed payloads. Each is invisible in review and expensive in a debugger.

**Rejected — define the anatomy later, when there is more to order.** Retrofitting
an order onto systems that each assumed their own is strictly more work than
writing it down first, and by then the assumptions are load-bearing. T0100 is
deliberately Phase 2 for this reason.

**Rejected — a phase-registration API** (systems register callbacks against named
phases at runtime). More flexible, and the flexibility is not wanted: the value
here is that the order is *fixed and readable in one function*. A registry moves
the order into runtime configuration, where it can no longer be read off the
page or asserted as a whole.

**Consequences:**

- `ILayer` and `Application` gain `onFixedUpdate` and `onLateUpdate`. Follow
  logic in `onUpdate` is now a bug even when it appears to work.
- Three separate tickets (T0048 reload, T0058 asset swap, T0077 scene
  transition) share **one** safe point rather than three, and that point must
  assert the deferred queues are drained and no jobs are in flight (T0026)
  before acting.
- Entity structural change applies mid-frame (phase 5), *not* at the safe point,
  so a destroyed entity cannot be drawn one last time.
- Transform propagation has two points, not one. T0101 may make the second
  incremental but may not remove it.
- Anyone adding a system to the frame adds it to a phase that already exists.
  If no phase fits, that is a change to this decision and to T0100's document —
  not a new call bolted into `run()`.

---

## D18 — Under WSL the working tree lives on the Linux filesystem, never `/mnt/c`

**Decision:** A WSL checkout goes in the Linux filesystem (`~/dev/hollow_point`
or similar). WSL is then a native Linux host and cross-compiles both targets,
which is D3's matrix unchanged. Windows-host building, if wanted, uses a
separate checkout on `C:\`.

**Why it is forced, not preferred:** Zig commits its build cache by writing
`.zig-cache/tmp/<hash>/` and renaming that directory to `.zig-cache/o/<hash>`.
POSIX allows renaming a directory containing open files; Windows does not, and
`/mnt/c` is a 9p bridge onto NTFS where Windows performs the operation. The
rename is refused with `AccessDenied` and the build dies before compiling
anything. Measured: the same rename succeeds on ext4 with a file held open, and
succeeds on `/mnt/c` with nothing open.

This is [ziglang/zig#24955](https://github.com/ziglang/zig/issues/24955) —
**open**, no maintainer response, and its fix
([PR #30588](https://codeberg.org/ziglang/zig/pulls/30588)) is **closed
unmerged**. It regressed in 0.15.1; 0.14 worked. We pin 0.16.0.

**Rejected — host-key the cache and build tree, as T0102 did for `.harness/`.**
This was the first diagnosis and it was wrong. The rename destination did not
exist and its hash differed every run, so nothing was colliding. A host-keyed
cache is still on `/mnt/c` and still cannot be renamed.

**Rejected — `ZIG_LOCAL_CACHE_DIR`, or a wrapper script that sets it.** Works,
but it is a workaround carried forever for a bug upstream declined to fix, and
it fails confusingly in any shell that missed the setup. It also cannot be made
automatic: the cache location is chosen while Zig compiles `build.zig` itself,
before any of our build logic runs, so `build.zig` cannot fix its own
environment.

**Rejected — out-of-tree artifact root** (`build/`, `dist/`, cache under a
host-local directory). Sounds like the clean refactor and is structurally
incomplete for the same reason: the failure happens while compiling the build
runner, upstream of anything `build.zig` controls.

**Rejected — keep one tree on `/mnt/c` and build Linux in a container.** Solves
it and buys CI parity, but it is real infrastructure — a Docker dependency and a
second environment to maintain — to work around a filesystem choice that can
simply be made differently.

**Rejected — move the single tree into WSL and build Windows over `\\wsl$`.**
Trades a broken bridge for a different broken bridge; the Windows host then pays
the performance and semantics cost instead.

**Consequences:**

- **No build-system change.** No wrapper, no environment variable, no
  `build.zig` edit. This is the main argument for it.
- One tree cannot serve both hosts. There is no filesystem that gives Windows
  and Linux correct semantics at once.
- Native Windows-host building is covered by CI's `tests-windows-host` job
  (T0004) rather than by a developer, so the 2×2 matrix stays proven even when
  nobody runs that row by hand.
- T0102's host-keying of `.harness/` stays — it is correct and cheap — but it
  solved a *different* problem and should not be read as making a shared
  `/mnt/c` tree workable.
- The case-collision on `X11/bitmaps/{Stipple,stipple}`, permanently dirty on a
  case-insensitive Windows filesystem, resolves for free: ext4 holds both.

---

## D19 — No runtime C++ compilation; gameplay is AOT-compiled, and iteration is bought with a precompiled header

**Decision:** there is no embedded C++ interpreter or JIT. Gameplay is compiled
ahead of time by the pinned toolchain into a module the host loads, as D12 and
T0048 describe. Iteration speed is bought with a **precompiled header** of the
engine's public headers plus entt — not with a compiler in the process.

**Why this needed deciding at all:** D14 rejected a scripting *VM* on the cost of
a per-frame native↔VM boundary. A JIT has no such boundary — it emits native
code — so D14's reasoning does not dispose of it. The question was never asked,
and "C++ as the scripting language" is a genuinely different proposition from
Lua: same language, same headers, no binding layer, native speed.

**Rejected — Cling, `clang-repl`, or ORC JIT as the gameplay-authoring
mechanism.** Three findings decide it, measured here rather than surveyed:

- **It costs half the build matrix.** `clangInterpreter` cannot be embedded
  without vendoring and building llvm-project, and **zig ships no LLVM or Clang
  libraries at all** — verified: zero `libLLVM*`/`libclang*` under
  `.harness/zig/`, only a single static `zig` binary and source trees. That is a
  second toolchain to vendor into a hermetic offline build. Worse, the embed has
  never been run under the MinGW-w64 ABI by anyone: CppInterOp's Windows CI is
  MSVC-only, and `lli` (the same ORC infrastructure) cannot currently run
  hello-world on Windows COFF. D3 requires both targets, and this is the half
  that cannot be recovered.
- **There is no C++ runtime in the process for JIT'd code to link against.**
  Zig links libc++/libc++abi/libunwind statically into every artifact with hidden
  visibility. Verified against the real `libhp_engine.so`: **zero** exported
  `std::`, `__cxa_*` or `_Unwind_*` symbols, with `__cxa_throw` present only as a
  *local*. Prototypes confirm the consequence — `std::string` at `-O0`,
  `std::vector::push_back`, and `entt::registry reg;` all fail to resolve.
  Supplying the JIT its own libc++ fixes the link and puts **two C++ runtimes in
  one process** sharing `std::__1` mangling: the `entt::type_index` hazard of
  T0095 one layer deeper, with no build id to catch it.
- **The prize does not exist.** Measured on the real gameplay TU with this
  toolchain: the **front end is the cost**, and a JIT pays it identically.
  `-fsyntax-only` is 1.4 s while `-O0` vs `-O3` differs by ~0.2 s, linking is
  0.05 s and `dlopen` is 0.5 ms. A JIT competes for a fraction of a second while
  adding hundreds of megabytes of LLVM.

**Rejected — the JIT-in-development / AOT-on-ship split** (ROOT's architecture
applied to a game engine). It is a coherent shape and it fails on its own
premise: the premise is redefine-in-place, and upstream `clang-repl` **cannot
redefine** — a second definition of a function is `error: redefinition`, and
redefining a struct crashes in `IncrementalParser::CleanUpPTU`. Redefinition is
a Cling extension that was never upstreamed, and Cling is the MSVC-only patched
fork that fails the matrix requirement hardest. The split also relocates T0048's
state-survival problem rather than removing it: a module reload has an explicit
safe point (D17's phase 12), while a type redefined under live instances has
none — existing objects keep the old layout while new code assumes the new one.

**Rejected — assuming consoles might permit it.** All three families enforce W^X
for third-party titles; Xbox's XR-018 names the pattern explicitly for UGC,
LuaJIT hard-codes interpreter-only per console target, and Unity ships IL2CPP
precisely for platforms that disallow JIT. Switch 2's policy is not publicly
documented and is not assumed here either way.

**What is taken instead — a precompiled header.** Measured on the real
`samples/sandbox` TU: front-end time falls from **1.4 s to 0.34 s** with a 19 MB
PCH of the engine's public headers plus entt. Roughly a 4× cut in the dominant
cost, hermetic, both targets, no new dependency, and available today. Owned by
T0048.

**Consequences, accepted:**

- No runtime-authored gameplay of any kind. This strengthens D14 rather than
  changing it: gameplay is C++, composed from data, and new behaviour ships a
  binary.
- **`dlopen`ing gameplay is a development affordance, not the shipping
  configuration.** Unreal links Monolithic for everything but the Editor, and
  IL2CPP does no runtime loading at all. T0048's "the exported runtime can link
  it statically" therefore becomes the console-port requirement rather than an
  optimisation — recorded on T0105.2.
- Reflection (T0053) is unaffected. Serialization, the inspector and undo need
  generic property enumeration regardless, and even ROOT — which *did* choose
  runtime-compiled C++ — still runs a separate generated dictionary pipeline for
  I/O. Reflection and binding are separable concerns.
- **Revisit only if** zig begins exposing LLVM/Clang as linkable libraries, *or*
  `clang-repl` gains tested MinGW-w64 support *and* upstream redefinition. Both
  are required; either alone does not reopen this.

---

## D20 — Device loss is fatal, with a distinguishable message; recreate-and-continue is rejected for now

**Decision:** when the graphics device is lost mid-run, the process logs a
message that names it as a **GPU or driver failure**, flushes the log, and
aborts. It does not attempt to recreate the device and carry on.

The message is the point. `VK_ERROR_DEVICE_LOST` happens on real machines —
driver updates, GPU hangs, a laptop switching between integrated and discrete
GPUs — and this engine intends to run its entire particle system in compute
(**D15**), which is the classic way to write an accidental GPU hang while
developing. A player report saying "device lost" is triaged in seconds; the same
failure as an unlabelled crash is triaged as memory corruption, which is where
the afternoons go.

**Rejected — recreate and continue.** Recreating the device means reloading
every GPU resource, rebuilding every pipeline state, and resuming mid-frame with
a half-populated cache. It is a large feature, it is exercised almost never, and
code that is exercised almost never is wrong when it finally runs. Nobody should
build it speculatively.

**Revisit when** there is evidence of frequent *recoverable* loss in the wild —
a platform where losses are routine rather than exceptional, or a support burden
that shows players hitting it repeatedly on healthy hardware. Until then the
honest engineering position is that a lost device is an environment failure, not
a state the engine should pretend to survive.

**What this cost, measured while implementing it (T0113):** DiligentCore has no
device-loss handling of its own — `VK_ERROR_DEVICE_LOST` appears only in the
vendored Vulkan headers, nowhere in Diligent's source. There is no status query
to poll and no structured signal to subscribe to. The **only** hook is the
per-factory debug message callback, so detection is necessarily
pattern-matching on what the backend reports, and that is a limitation of the
dependency rather than a shortcut.

The OpenGL side is worse and is recorded rather than guessed: Diligent exposes
nothing for GL context loss on desktop (`glGetGraphicsResetStatus` and
`GL_CONTEXT_LOST` appear only in the Android EGL path). On the GL backend,
device loss is therefore **undefined** — it will present as whatever the driver
does, most likely a crash without the distinguishable message. Vulkan is the
default backend, which is what keeps this acceptable.

## D21 — Diligent's math headers are public API; the RHI they drag in is visible but unlinkable

**Decided** 2026-08-04, on T0021, when the first public header needed a vector.

Two binding rules collided and neither had anticipated the other:

- `06-engine-conventions.md` (T0056) says **gameplay uses Diligent's math types
  directly**, not a wrapper. Every renderer call takes them, so a second vector
  library means conversions at every boundary forever, and the two libraries
  disagree about row versus column major — a mismatch that produces a transposed
  matrix about once a month.
- `hp/Render.hpp` and T0013's 13.3 say **no public engine header may name a
  Diligent type**, because doing so hands every consumer Diligent's include path.

They cannot both hold, because **the math cannot be had without the RHI.**
`BasicMath.hpp` includes `HashUtils.hpp` — solely for `ComputeHash`, used by the
`std::hash` specializations at the bottom of the file — and `HashUtils.hpp`
includes `Sampler.h`, `PipelineState.h`, `TextureView.h`,
`PipelineResourceSignature.h` and `VertexPool.h`, so it can hash *their*
descriptors. The leak is an artifact of Diligent hashing everything in one
header, not of anything intrinsic to the math.

**Measured, not assumed** (2026-08-04, zig 0.16.0, this tree):

| | |
|---|---|
| A TU containing only `#include "BasicMath.hpp"` | **146,280** preprocessed lines |
| Distinct Diligent headers pulled in | **74** |
| RHI types made visible | `IRenderDevice`, `IDeviceContext`, `IPipelineState`, `ISampler`, `ITextureView` |
| Added to a TU already including entt | **+594 ms** (1557 → 2151 ms, +38%, three-run mean) |

**Decision: the math policy wins, and the exposure is made as narrow as it can
be.** `hp/Math.hpp` is the only public header that includes Diligent, and it
exists so that this dependency is deliberate and has one place to change.

The narrowness is the substance of the decision, not decoration:

- **Include directories go PUBLIC; libraries never do.** `Diligent-Common` is a
  STATIC library, and linking it PUBLIC would compile its code into every
  gameplay module. A second copy of anything stateful is precisely the
  statics-per-artifact failure behind T0105.1's dangling `__cxa_atexit` and
  T0127's typeinfo mismatch. The graphics engines stay PRIVATE, so **the RHI
  types are visible to gameplay and unlinkable by it** — declarations with no
  symbol behind them.
- `Diligent-PublicBuildSettings` *is* linked PUBLIC and is safe: it is an
  `INTERFACE` target carrying only `PLATFORM_LINUX=1` / `PLATFORM_WIN32=1`.
  Those are not optional — `PlatformDefinitions.h` is a hard `#error` without
  one — so omitting them turns every gameplay TU that touches a transform into a
  compile failure with a misleading message.
- **`hp/Render.hpp` stays a pimpl.** Nothing here makes it acceptable for an
  engine header to hand out an `IRenderDevice`. 13.3 is amended for *math*, not
  repealed.
- Measured: `Common/interface` alone suffices; everything below resolves through
  relative includes.

**Rejected, with reasons:**

- **An `hp::Vec3` converting at the boundary.** This is what the conventions
  document already rejected, and the collision above does not make it a better
  idea — it makes the conversion surface larger, since transforms now cross
  between gameplay and renderer constantly.
- **Forward declaration.** A component holds a transform *by value*, so the type
  must be complete. There is no version of this that works.
- **Patching or forking Diligent to split the hashers out.** `HashUtils.hpp`
  uses `#pragma once`, so its guard cannot be pre-tripped from outside;
  DiligentEngine is an upstream submodule, so the alternative is carrying a fork.
  Six hundred milliseconds per TU is not worth a permanent merge burden.

**Revisit when** either the compile cost is measured to matter at real project
scale — the mitigation is a precompiled header, which D19 already commits to for
iteration speed — or upstream splits the math hashers into their own header, at
which point `hp/Math.hpp` is the single file that changes and the RHI stops being
visible at all.

## D22 — Gameplay may drive the RHI through interface pointers; it may never create one

**Decided** 2026-08-05, on T0046/T0027, after measuring what D21 had accidentally
made possible.

D21 exposed Diligent's include directory so `hp/Math.hpp` could work, and noted
in passing that this leaves the RHI types **visible to gameplay and unlinkable by
it** — the graphics engines stay PRIVATE, so there is no symbol behind them.
That was recorded as a tolerable side effect. It is more than that.

**Measured 2026-08-05:** a shared library that calls `SetRenderTargets`,
`ClearRenderTarget` and `Draw` through `IDeviceContext*` and `ITextureView*`
links cleanly under `-Wl,--no-undefined` with **zero Diligent libraries linked**.
Diligent's interfaces are pure-virtual COM-style types, so a call through a
pointer is virtual dispatch and needs no external symbol at all.

So the boundary is not "gameplay cannot touch the RHI". It is sharper and more
useful than that:

- **Gameplay CAN use anything it is handed a pointer to** — issue draws, set
  render targets, bind pipelines, map buffers. This is what makes T0027's "the
  stack accepts layers implemented outside the engine" a real extension point
  rather than a token one, and it is why a gameplay-authored fog-of-war or
  minimap pass is possible without a C API or a command-buffer abstraction.
- **Gameplay CANNOT create a device, a swap chain or an engine factory.**
  `CreateEngineFactoryVk` and friends are free functions in the PRIVATE
  libraries; a module that calls one fails to link. That is the correct
  asymmetry and it enforces itself — no policy, no review, just the linker.

**Consequence, and it is an amendment to `hp/Render.hpp`'s rule:** public engine
headers **may** name Diligent RHI interface types where a gameplay consumer needs
to use them. `RenderLayer` still owns device creation and lifetime, and no header
exposes a factory. What changes is that handing out an `IDeviceContext*` is now a
deliberate, sound part of the design rather than a leak.

**What this does not license.** Not `RefCntAutoPtr` in a public header — refcount
manipulation across the boundary reintroduces exactly the ownership question D12
avoids. Not engine-internal Diligent objects handed out casually; a pointer in a
public API is a contract about lifetime, and the engine's answer is that
frame-scoped pointers are valid for the frame and nothing else.

**Revisit if** a second backend is ever wanted behind an abstraction, since this
makes Diligent's interfaces part of the gameplay-facing API surface rather than
an implementation detail. That is a real cost and it is accepted deliberately:
D6 already chose Diligent, and an abstraction over an abstraction buys nothing
until there is a concrete second target.

---

## D23 — Gameplay is authored as C++ classes over component storage, behind a declaration layer

**Decided** 2026-08-05, before any of T0062 or T0076 was built, after the
question "what does game code actually look like here?" turned out to have no
answer written down anywhere.

**Decision:** gameplay is authored as ordinary C++ classes deriving from
`hp::Behaviour` (per entity) or `hp::Service` (per scene, per session). Instances
are **components in engine-owned per-concrete-type entt pools**. A declaration
layer — one macro per type — generates registration, reflection, dispatch and
deregistration, so a gameplay file contains no engine plumbing. Callback
vocabulary is **Godot's** (`ready`, `process`, `physicsProcess`), composition is
**Unity's** (many behaviours per entity), storage is **entt's**, and there is a
`lateProcess` phase that neither Godot nor Unity has.

The shape, the worked examples and the open questions are
[`09-gameplay-authoring.md`](09-gameplay-authoring.md).

**What this is answering.** Not "which paradigm is nicest". The binding
constraint is **T0048**: a gameplay module genuinely unloads (proven over 25
host lifetimes, reload swap measured at **1.76 ms**), so instance state cannot
live in a module-allocated object and statics in the module image die. State
must therefore be engine-owned. Once it is a reflected component, the inspector
(T0035), serialization (T0022) and the reload snapshot come from machinery
already being built — so most of the design is *forced*, and the only genuinely
open question was the authoring surface.

**Rejected — a type-keyed autoload registry** (what T0076 originally specified):
`Autoload::Get<T>()`, a topological sort, a cycle detector, hand-written reverse
teardown. All of that machinery exists to manage a dependency graph created by
putting services in a bag. A single root object per scope gets construction
order from **declaration order** and teardown from **reverse destructor order**,
both guaranteed by the language, and turns a dependency cycle into a *compile*
error. It also contradicted `06-engine-conventions.md`'s own rule that stateful
engine services are *"passed, not reached"*.

**Rejected — `registry.ctx()` as a service container.** Verified against the
vendored entt 3.16.0: it is a `dense_map<id_type, basic_any<0u>>`, so
destruction order is **unspecified** and every entry is a separate heap
allocation. Fine as the storage primitive for *one* root (both objections
vanish at n=1); wrong for twenty services.

**Rejected — Godot's node model wholesale.** Its `_process`/`_physics_process`
split is genuinely good and is adopted. The rest is load-bearing on *everything
being a Node*, which is not true here. T0076 had already written the reason —
*"Godot makes them nodes; that is a consequence of everything in Godot being a
node, not a virtue to copy"* — and T0062 had written the other one: one script
per node forces composition by **child node**, filling the hierarchy with
entities that are logic wearing a transform.

**Rejected — plain aggregate components plus free functions, with no base
class.** This was the leading candidate for most of the design conversation and
it loses on ergonomics. The performance argument for it was weaker than it
appeared: what makes MonoBehaviour slow is iterating a *heterogeneous list of
pointers into scattered heap objects*, not inheritance. One pool per concrete
type is contiguous whether or not the type has a vtable, and `final` on a leaf
lets the compiler devirtualise the call. The cost of the base class is 8 bytes
of vptr per instance.

The base class also earns its place on **safety**, which the free-function
version cannot: it is the only place to put `setPosition()` / `setRotation()`
wrappers that mark the transform dirty. Writing `Transform::rotation` directly
is invisible to propagation — `Scene.hpp` says so — and that is the single
silent failure a door author hits first. A base class makes it unreachable.

**Cost accepted:** a class with virtual functions is not an aggregate, so the
compile-time field-name reflection used by libraries like `glaze` and
`reflect-cpp` does not apply, and exported fields must be listed in the macro.
That is the `@export` tax and it is the price of the base class. The two cannot
both be had.

**Rejected — a scripting VM (Lua/sol2), reconsidered rather than inherited.**
**D14** rejected scripting on the grounds that the requirement it satisfies is
mods and DLC without shipping a binary, *"and that is not a goal here"*. That
argument does not dispose of the question asked in this conversation, which was
**authoring ergonomics for non-expert developers** — a requirement D14 never
weighed. Re-examined on those grounds, Lua is genuinely cleaner for plumbing,
field registration and hot reload, and it still loses on three things:

- **The per-frame boundary.** A `lua_pcall` is hundreds of ns against ~2–5 ns
  for a devirtualised C++ call, and *field access crosses the boundary too* —
  `self.position.x` is a metatable dispatch into C, where the C++ version is a
  memory offset. That lands directly on T0093 and T0098, the two tickets D14
  already names as the unit-count drivers.
- **It relocates the plumbing rather than removing it.** Every engine type a
  script touches needs a binding. In C++ a developer who needs something
  unusual reaches for it; in Lua they hit a hard stop and file a ticket against
  the same programmers whose time the change was meant to protect.
- **Two of everything, forever** — a C++ API and a Lua binding per feature, and
  serialization, reflection and the inspector all understanding both.

LuaJIT does not rescue the first point: **D19** already recorded that it
hard-codes interpreter-only per console target because all three families
enforce W^X.

**Not revisited here:** runtime-compiled C++ (Cling / `clang-repl` / ORC JIT) is
**D19**, which measured it and found the front end is the cost a JIT cannot
avoid. Re-verified 2026-08-05 that the pinned toolchain still ships **zero**
linkable `libLLVM*`/`libclang*`. A JIT is also strictly *worse* for the problem
that prompted this conversation — debuggability — since JIT'd code has no
on-disk image, and D19 already measured that `libhp_engine.so` exports no
`_Unwind_*` symbols, so a stack trace cannot cross a JIT frame.

**Consequences:**

- **T0062 is rescoped and gets materially smaller.** No pool allocator (entt
  pools), no behaviour registry parallel to the component registry, no separate
  serialize-by-name path (T0022 handles components), no "Add Behaviour" dropdown
  distinct from "Add Component". What replaces them is the declaration layer.
- **T0076 is rescoped to a single root per scope.** Its ordering, cycle-check
  and teardown-order subtasks are answered by the language.
- **The reload snapshot is one mechanism, not two.** T0062.6 and T0076.8 are the
  same code — reflected data in engine storage — because genuine unload works
  and therefore genuinely dangles module-side vtables and pools.
- **`hp::Behaviour` must delete copy and move.** Verified: entt's
  `component_traits<T>::in_place_delete` defaults to true for types that are not
  move-constructible, which gives behaviour instances **stable addresses**. That
  is what makes signal connections and `Behaviour*` safe to hold within a frame.
- **`using Super = X;` becomes a convention with teeth**, not decoration: it is
  how the type hierarchy reaches `entt::meta`'s `base<Base>()` — already exposed
  by `hp::TypeBuilder::base()` — and without it a base-type query will not find
  the subclass.
- **The layer may fail at compile time and must never fail silently at
  runtime.** This is a constraint on how it is written, not a hope. Hiding
  machinery moves a developer further from the cause when something breaks; an
  ugly template error is an acceptable price, a door that does not move is not.
- **`ModuleContext` and `ModuleApi` must grow** — a `Scene*` and the phase
  hooks. Verified 2026-08-05 that nothing in `apps/` owns a `Scene` today, so
  no gameplay code of any shape can currently be written. Roughly thirty lines,
  and it blocks everything else.

**Revisit if** gameplay is going to be written by people who are not C++
programmers. No declaration layer closes the last gap — a segfault is still a
segfault, and a compile error still stops the world. That is a studio and hiring
question, not an engineering one, and it is the one input that would change this
answer.

---

## D24 — DiligentFX's `PBR_Renderer` is the engine's shading path, driven directly; its glTF renderer, its USD stack and its extended materials are not

**Decided 2026-08-05 on T0134**, after T0028 had to pick something to turn a
`Diligent::GLTF::Model*` into pixels and could not answer the broader question
inside its own scope. Four tickets inherit this — T0060 (materials), T0079
(lights), T0087 (IBL), T0096 (HDR) — and the point of writing it down is that
they inherit an argument rather than an accident.

**Adopted:** `PBR_Renderer` — its shaders, pipeline-state creation and caching,
shader-resource-binding management and material attribs — together with its
shader-side data model. `PBRFrameAttribs`, `CameraAttribs`, `PBRLightAttribs` and
`PBRMaterialShaderAttribs` **are the engine's shader vocabulary**; systems
populate them rather than inventing parallel structures. Its `static` writer
helpers are used as-is, because they encode the packing rules and reimplementing
them is how a layout drifts silently.

**Owned here, not by DiligentFX:** the `GraphicsPipelineDesc`, the model
traversal, and frame-attribs population. The first is forced — reverse-Z (T0130)
requires `COMPARISON_FUNC_GREATER_EQUAL` and `GLTF_PBR_Renderer` builds its own
desc with `DepthFunc` left at `LESS`, behind a private PSO cache, which under our
convention draws **nothing at all**. The second follows from the first, and is
where T0060's per-entity overrides and T0045's sorting have to live anyway.

**Rejected, and these are the ones worth the ink:**

- **`GLTF_PBR_Renderer`** — the reverse-Z blocker above. Not a preference.
- **`USD_Renderer`, `Hydrogent`, `Radient`** — whole USD/Hydra scene-description
  subsystems. The asset model here is glTF through T0023; adopting a USD stage
  model is a far larger decision than picking a renderer, and nothing has asked.
- **Extended materials** — clearcoat, sheen, anisotropy, iridescence,
  transmission, volume. Each widens the PSO permutation space and the material
  attribs buffer **whether or not any material uses it**. Off until a ticket asks
  by name.
- **OIT** — order-independent transparency is a transparency *design*, not a
  flag, and nothing has designed transparency.
- **An abstraction over the renderer** — **D22**. One implementation, so an
  interface buys indirection and nothing else. The modularity is structural: the
  renderer lives inside one `.cpp` behind an `Impl`, and DiligentFX is linked
  PRIVATE.

**Tonemapping is a PBR-shader feature, not a pass, and the natural reading of
the tree is wrong.** `Components/ToneMapping.hpp` declares two functions —
`ReverseExpToneMap` and `ToneMappingUpdateUI` — and is a UI helper;
`Shaders/PostProcess/ToneMapping/` is a shader *include*. There is no standalone
tonemapping stage, so there is no double-apply hazard. The genuine fork is
in-shader (`PSO_FLAG_ENABLE_TONE_MAPPING`) versus a pass over an HDR target, and
**T0096 should take the pass**: the in-shader path tonemaps per draw before
blending, which breaks transparency and leaves DiligentFX's Bloom component with
no HDR image to read.

**What this decision does not cover, deliberately:** which lights exist and how
they are selected (T0079), what a material asset is (T0060), the HDR chain
(T0096), and anti-aliasing (T0111). This decides *whose code shades a pixel*, not
what the pixel should look like.

**Revisit if** a second rendering target appears — a deferred path, a mobile
tier, or a stylised non-PBR renderer. That is the "concrete second target" D22
names as the precondition for an abstraction, and it would reopen this rather
than being layered on top of it. The seam that would hurt is **not** the draw
call: it is `MeshAsset::model()` returning `Diligent::GLTF::Model*` from a public
header, which T0023 committed to and which no renderer interface would touch.

---

## D25 — Temporal antialiasing is the target, MSAA is out, and upscaling is adopted as an abstraction rather than a backend

**Decided 2026-08-05 on T0111**, before a lit scene exists — deliberately, because
the alternative is the silent default this engine already paid for once with
`VK_PRESENT_MODE_IMMEDIATE_KHR` (see T0110).

**MSAA is out, and not because it is bad.** It is the best answer for geometric
edges — exact, deterministic, no ghosting — and the usual objection does not
apply here, since `PBR_Renderer` is a **forward** renderer, which is where MSAA is
cheapest and most natural. It is out because it **antialiases coverage, not
shading**: the pixel shader runs once per pixel, so specular shimmer and
normal-map crawl — the dominant aliasing in a PBR renderer — are untouched. And
its cost lands precisely on the passes this engine is accumulating: anything
sampling scene depth becomes a per-sample read (T0106.5's soft particles are
already specified to need depth while drawing transparents), and DiligentFX's
post-process stack expects single-sample textures, forcing a resolve before
tonemapping — where bright samples dominate the average and produce fireflies.

Consequence: `TargetFormat` gains no sample count and `FrameTargets::formatFor`
stays as it is.

**TAA is the target.** DiligentFX ships a competent middle-tier implementation —
variance clipping, Catmull-Rom bicubic history, disocclusion rejection — and it is
the only technique that antialiases shading. No FXAA/SMAA-class filter ships in
Diligent at all, so the cheap tier would have to be written; rejected for now on
that basis rather than on quality. Three prerequisites are **named and not
built**: motion vectors (T0101.5 — `PrevCamera` is already written every frame),
sub-pixel jitter (T0081, and `CameraSystem.hpp` already documents the seam by
name), and history buffers (T0046 — `declarePingPong` is already the right shape).

The risk that decides whether TAA looks good is motion vectors, and two cases are
hard: **skinned meshes** need previous *joint* matrices, not just previous
transforms, and **GPU-driven particles** (D15) have no CPU-side previous position,
so they smear unless the simulation writes its own.

**Upscaling is adopted as `ISuperResolutionFactory`, not as a technique.**
DiligentCore ships a unified abstraction over NVIDIA DLSS, Microsoft DirectSR,
AMD FSR and Apple MetalFX, discovering the available implementations from the
render device at factory-creation time. The engine asks and uses the best
available; it never hardcodes one. **This is an engine for several games on
several machines, and what one developer's build happens to compile is not an
engine capability decision** — a player with an NVIDIA GPU should get DLSS, and
the engine's job is to have asked.

This costs nothing beyond TAA's prerequisites: temporal upscalers need exactly
depth, motion vectors and jitter, so **one piece of work opens TAA, DLSS,
DirectSR, FSR2 and MetalFX Temporal together.** Spatial FSR needs only the colour
texture and is the guaranteed floor on every API — but it provides **no
antialiasing at all**, which is why TAA is decided separately rather than assumed
to arrive with the upscaler.

**Render scale is accepted**, with the sizing rule: world and post-process targets
size from **output x renderScale**; UI, HUD and editor panels are **always
native**; the upscale happens once, between them. `FrameTargetDesc::scale` already
implements the mechanism, having been added for half-resolution bloom buffers.

**That rule collides with T0027.5's single-target compositing**, and the collision
is recorded rather than resolved: `RenderStack` composites every layer into one
colour target, so a HUD layer inherits the world's render scale and its text is
upscaled. The fix is a two-phase stack, or UI layers owning their own target —
**the same seam a tonemap pass needs**, so T0096 must not invent a second one.

**Revisit if** an MSVC-based Windows build is ever added. The current toolchain
targets Windows through the MinGW ABI, and `MINGW_BUILD = TRUE` is measured to
gate DLSS and DirectSR off — the same root cause that rules out D3D11/D3D12, since
DiligentCore gates those on ATL. Unlocking them means giving up the
single-toolchain hermetic cross-compile the harness is built around, which is a
real price. It is written down here so it can be weighed, rather than discovered
later as "the engine does not support DLSS".

---

## D26 — The engine owns the **surface stage**; DiligentFX supplies the lighting. Diligent's source is never modified

**Decided 2026-08-05 on T0141.0, by the owner**, in their words: *"i dont want to
modify dilligent"*. That constraint alone settles a question this ticket had been
carrying as three options.

**Amends D24, which did not survive contact with a real requirement.** D24 made
DiligentFX's `PBR_Renderer` the shading path and its `RenderPBR.psh` the material
shader. That was right for what T0028 and T0134 needed. It has a ceiling, and the
owner hit it within a day of materials existing: **height mapping, parallax
occlusion, triplanar and tessellation are unreachable at any setting**, because
`RenderPBR.psh` is private and has no hook before texture sampling.
`CreateInfo::GetPSMainSource` reaches only the pixel shader's output struct and a
footer, so it can change what the shader *emits* and never how it *samples*. And
`CreatePSO` sets `pVS` and `pPS` only — there is no hull or domain stage anywhere
in the PBR pipeline.

### What was rejected, and why the obvious option was the wrong one

| | | Verdict |
|---|---|---|
| **Keep `RenderPBR.psh` as the material shader** | Cheapest. Custom shaders bolted alongside | **Rejected.** It is the ceiling itself. Nothing can be added to the standard path, and a custom shader would inherit none of the PBR lighting |
| **Patch DiligentFX's shaders** (a hook before sampling) | Looked like a few lines | **Rejected on the *vendoring*, not the diff.** `third_party/DiligentEngine` is a **git submodule pointing at upstream** and this tree has **no patch mechanism at all**. So it costs either a fork of a large engine, owned indefinitely by a small studio, or new build machinery that must run before configure everywhere — and CI's build-tree cache is keyed on `git submodule status --recursive` (T0121), so a patch would have to enter that key or a changed patch silently reuses a tree compiled without it. It also **cannot reach tessellation**, because there the missing piece is C++ PSO construction rather than shader text |
| **Write our own uber-shader outright** | Total control | **Rejected.** Reimplements split-sum IBL, punctual lighting, shadow filtering, tonemapping, every alpha mode and the glTF extensions — precisely the body of work D24 declined to write |

### Revised 2026-08-06 — we own `main`, **not** the sampling

**The first implementation of this decision wrote its own texture sampling, and
that was wrong.** ~100 lines reimplementing UV-set selection, the UV transform,
wrap-mode clamping and `SampleBias`, for three of the seventeen slots DiligentFX
already covers.

The owner asked the question that undid it: *"why are we not #including their
implementation and building on it?"* The honest answer was that the option had
**never been evaluated**. This entry had considered *shadowing*
`PBR_Textures.fxh` — shipping a file with that name so *their* shader includes
*ours* — and correctly rejected it. It never considered simply **including**
theirs and calling it, which is a different thing entirely and strictly better.

`PBR_Textures.fxh` provides fifteen getters covering every slot, including all
six extended features D24 turns off: `GetBaseColor`, `GetPhysicalDesc`,
`GetOcclusion`, `GetEmissive`, `GetMicroNormal`, `GetClearcoatFactor`,
`GetSheenColor`, `GetAnisotropy`, `GetIridescence`, `GetTransmission`,
`GetVolumeThickness` and the rest.

**The surface stage never required owning the sampling. It requires owning
`main`.** Every getter takes `VSOutput` **by value**, so parallax (141.7) and
triplanar (141.8) build a displaced copy of it and pass that in — the hook this
decision exists for is completely intact. Measured: switching to their getters
cut `HpSurface.psh` from 228 code lines to 165, and the rendered frame stayed
**byte-identical** to what `RenderPBR.psh` produces on the same scene.

So the split is one level finer than first written:

| | Owner |
|---|---|
| the pixel shader's `main`, and what it calls | **ours** |
| texture sampling (`PBR_Textures.fxh`) | **theirs**, included |
| lighting (`PBR_Shading.fxh`) | **theirs**, included |
| the game's hook (`HpSurface`) | ours (D27) |

**Override granularity is per function call, and it is ours to choose.** For any
single one of their functions we can decline to call it and call our own instead,
in our file, without forking or shadowing theirs. The default is to call theirs;
writing our own is a decision to be argued per function, not the starting point.

`PBR_Textures.fxh` is **private**, and taking it is deliberate — the same trade
already accepted for `RenderPBR_Structures.fxh`. A signature change upstream
breaks this build loudly, which is the failure mode worth having.

### Slang was evaluated, rejected on a wrong premise, then **measured and adopted** — see D28

**Recorded here because the reasoning failed twice before it worked**, and both
failures are the kind that repeat.

**First rejection, and it was wrong.** A research pass found that Slang's
`extension` declarations *"can only apply to structure types"*, and DiligentFX's
shading code is free functions calling free functions by static name — so an
`extension` has nothing to attach to. That is **true**, and it is **not the
mechanism that matters**. The conclusion drawn from it — "Slang cannot help" —
was wrong, and it was written into this log as a rejection before anyone had run
the compiler.

**What actually works is `interface` + default implementations.** The engine
declares an interface whose default methods *are* the standard material; a game
material implements the interface and marks its overrides with `override`. No
struct inheritance is involved anywhere. Which matters, because Slang is
**removing struct inheritance**: `warning[E30816]: support for inheritance is
unstable and will be removed in future language versions, consider using
composition instead`. A design built on subclassing would have been built on
something already deprecated.

**Measured, not read.** `slangc 2026.14.1`, against this tree's own submodule:

| Question | Result |
|---|---|
| Does Slang compile `PBR_Shading.fxh` (992 lines) unmodified? | **yes**, zero errors |
| `PBR_Textures.fxh` (934, private, global resources)? | **yes**, with the permutation macros passed as `-D` |
| Can a Slang method call their free functions? | **yes** |
| Do interface defaults + `override` work? | **yes**, and `override` is *required* — omitting it is a compile error, so nothing is shadowed by accident |
| Dynamic dispatch? | **none** — generics specialise statically; the emitted code calls `MyGameMaterial_getBaseColor_0` directly |
| SPIR-V accepted by Diligent? | **yes**, via `ShaderCreateInfo::ByteCode` |
| Do resource names survive? | **yes** — `g_BaseColorMap`, `g_BaseColorMap_sampler`, `cbMaterialAttribs` |
| The `SLANG_ParameterGroup_*_std140` trap (DiligentCore #698)? | **already handled by the pinned submodule** — `SPIRVShaderResources.cpp:331` tests `spv::SourceLanguageSlang` and prefers the instance name |

Two things it needs that we already have: Diligent's own `HLSLDefinitions.fxh`
prelude, and the permutation macros — which is exactly what
`PBR_Renderer::DefineMacros` already produces.

**Proven on hardware, end to end.** A Slang shader including their unmodified
`PBR_Shading.fxh` and calling `GetSurfaceReflectanceMR` through an interface
method, compiled by `slangc` to SPIR-V, handed to Diligent as bytecode, built
into a pipeline and drawn on an RTX 2080, returned **(245, 122, 49)** — which is
`BaseColor(1.0, 0.5, 0.2) * 0.96`, *their* arithmetic, to the byte.

**The lesson worth keeping:** the research was accurate about `extension` and
wrong about the conclusion, and the log recorded the conclusion. A rejection
written from documentation, about something this tree can compile in ten minutes,
is exactly what "anything measurable here, measure it" exists to prevent.

### What was adopted

**Our vertex and pixel shader mains, our PSO creation, their lighting library.**
The split every modern engine draws, and it needs no modification to Diligent
because all three pieces are already public:

- `Shaders/PBR/public/PBR_Shading.fxh` — `GetSurfaceReflectanceMR`,
  `PerturbNormal`, `ApplyPunctualLight`, `ApplyIBL`, `GetBaseLayerLighting`, and
  the sheen and clearcoat equivalents. Surface properties in, radiance out.
- `DiligentFXShaderSourceStreamFactory` — a public interface, so a shader in
  *our* tree can `#include "PBR_Shading.fxh"` through a compound factory.
- `CreateCompoundShaderSourceFactory` — public in `GraphicsTools`, and how
  `PBR_Renderer` builds its own factory internally.

`PBR_Renderer` is still **subclassed for the plumbing** — `DefineMacros`, the VS
input/output structs, `CreateSignature`, `CreateCustomSignature` are all
protected, and that permutation and resource-signature machinery is the part that
would be genuinely unpleasant to rewrite. `CreatePSO` is *private*, so this is
not an override: the subclass reuses the plumbing and creates its own shaders and
pipeline states beside it.

The struct layouts stay Diligent's — `PBRFrameAttribs`,
`PBRMaterialShaderAttribs`, `PBRLightAttribs` — so their lighting functions are
callable with no translation, and D24's "materials map onto
`PBRMaterialShaderAttribs`" survives intact. **That half of D24 is unchanged**;
what changes is only which shader consumes them.

### The cost, stated rather than discovered later

**We stop inheriting improvements to `RenderPBR.psh`.** When upstream adds a glTF
extension or wires OIT differently, we port it rather than getting it free. That
is the real price and it was weighed against a fork; it was accepted because it
fails as **our code not compiling** on an upgrade, which is loud, rather than as
a patch silently mis-applying, which is not.

**Offering the hook upstream remains worth doing** even though the patch route
was rejected as a dependency. If it ever lands, the trade can be revisited from a
better position at no cost.

### What this unlocks, and what it still does not

Parallax occlusion, height mapping, triplanar, detail maps, vertex displacement,
dissolve, and T0093's per-pixel visibility all become ordinary surface-stage code
(T0141.7/141.8). **Tessellation becomes reachable** — we own the PSO, so we can
add hull and domain stages — but is **deliberately deferred with a named
trigger: when a silhouette must change** (141.9). Parallax is an illusion in the
pixel shader: convincing head-on, and a POM brick wall still has a straight edge
against the sky.

---

## D27 — A game's shader compiles against the **engine's** contract, never against DiligentFX directly

**Decided 2026-08-05 on T0141, by the owner**: *"godot model it is"*.

A game developer writes a function; the engine owns the shader `main` around it:

```hlsl
#include "HpMaterial.fxh"          // the only engine header a game shader includes

void HpSurface(in HpSurfaceInput In, inout HpSurfaceOutput Out)
{
    Out.BaseColor = ...;           // theirs
    Out.Roughness = ...;
}
```

**We include DiligentFX; they include us.**

### Why the pass-through was rejected

Letting a game shader `#include "PBR_Shading.fxh"` is one line cheaper and would
have made **DiligentFX's internals part of this engine's public contract**. Every
upgrade that renamed a function or changed a struct would then silently break
every shader in every shipped game — which is the *same* inability-to-upgrade
trap **D26** was written to escape, arriving from the opposite direction. D26
stopped us being unable to change DiligentFX; this stops us being unable to
*update* it.

With a contract in between, a rename upstream is ported **once**, by us, behind a
header whose shape does not move.

### The cost, accepted rather than discovered

**A developer who wants something the contract does not expose has to wait for
the engine to expose it.** They cannot reach for a Diligent function themselves.
That is a real constraint on a real person, and it is named here because "just
let them include it" will look extremely reasonable the first time somebody hits
that wall — and taking it then costs everything above.

The mitigation is not a loophole, it is keeping `HpSurfaceInput` generous:
**T0141.6 decides that list deliberately**, because it is a promise. Adding to it
later is easy; removing from it breaks games.

### Consequences

- **Three shader sources, resolved in order: engine, then game, then DiligentFX.**
  Engine before game is deliberate — a project must not be able to shadow
  `HpMaterial.fxh` and quietly redefine the contract. The consequence is that
  engine and DiligentFX header names are **reserved**, which is a documented
  constraint rather than a surprise at compile time.
- **Game shaders arrive through the VFS** (D13), like any other content, while
  engine shaders are embedded in the binary (T0141). That asymmetry is what makes
  **hot reload** work for the half that needs it: a game's shader can change
  while the editor is open; the engine's cannot without a rebuild, and does not
  need to.
- **HLSL**, per D2 — OpenGL is the only fallback on Windows and Diligent's
  portable path is HLSL. T0141.1 must also record which HLSL subset survives the
  GL converter, because that subset is what a game shader may actually use.

---

## D28 — **Slang is HollowPoint's shader language.** HLSL/SPIR-V is the interchange with Diligent

**Slang is the default and the only language shaders are authored in** — the
engine's own material, every sample, and every game's custom material. HLSL
appears in this repository only as *DiligentFX's* headers, which Slang consumes,
and as a cooked output format. A new shader is a `.slang` file; there is no
second path to keep working.

**Decided 2026-08-06 on T0141, by the owner**, after the mechanism was measured
rather than read: *"we use the Diligent HLSL as is and solve our problem all in
all"*, and *"anything from OUR engine is Slang, anything going to Diligent is
HLSL"*.

**The problem it solves is D27's stated cost.** D27 chose Godot's model — a game
writes `HpSurface(in, inout)` and the engine owns the `main` around it — and
named the price in as many words: *"a developer who wants something the contract
does not expose has to wait for the engine to expose it."* Every field is
all-or-nothing, the contract has to anticipate every need, and widening it is a
ticket. That is a real ceiling and the owner hit it the same day.

**HLSL cannot express the alternative.** It has no inheritance, no override, no
partial types. Shader Model 5's `class`/`interface` dynamic linkage was the only
mechanism that ever came close, and it is D3D11-only, unsupported by DXC/SM6 and
absent from SPIR-V. Reuse in HLSL is `#include`, macros, and shadowing a filename
— nothing else.

### What was adopted

**A game material implements an engine `interface` whose default methods are the
standard material**, and overrides only what it wants:

```slang
interface IHpMaterial {
    float4 getBaseColor(float2 uv);                   // the game must supply this
    float  getRoughness(float2 uv) { return 0.5; }    // engine default
    float3 shade(...) { ... GetSurfaceReflectanceMR(...) ... }   // theirs, called from ours
}

struct MyGameMaterial : IHpMaterial {
    float4 getBaseColor(float2 uv) { ... }
    override float getRoughness(float2 uv) { return 0.05; }
}
```

**`override` is mandatory** — omitting it on a method that shadows a default is a
compile error. So no game accidentally replaces engine behaviour it meant to
inherit, which is the failure mode a permissive scheme would have.

**Generics specialise statically.** `render<T : IHpMaterial>(T m, ...)` emits a
direct call to `MyGameMaterial_getBaseColor_0` — no dynamic dispatch, no
indirection, no register cost. Slang does silently fall back to dynamic dispatch
when a call site is too polymorphic to specialise; the engine's use is
monomorphic per pipeline, so it does not arise, but it is worth knowing.

### Two capabilities this brings that HLSL has no answer for

**A module system, so include order stops being a hazard.** Slang's `import`
resolves modules from search paths, once, without the header-ordering discipline
`#include` demands — `HpSurface.psh` currently has a comment explaining why
`RenderPBR_Structures.fxh` must precede the generated structs, and that class of
comment disappears. Modules also carry their own preprocessor state rather than
leaking it, so one shader cannot silently change another's compilation. `slangd`
gives the editor real completion and diagnostics over the same modules, which is
what makes a game developer's shader authoring tolerable.

**Reflection without a device, which is what the editor needs.** Diligent already
reflects constant-buffer *contents* — name, type, offset, array size, nested
members — via `IShader::GetConstantBufferDesc()`, and that covers T0141.2 on the
runtime side. What it cannot do is answer before the shader compiles for a
device. Slang's reflection API reads the parameters straight from source, so the
editor can build a material inspector for a shader that has never been compiled,
and can do it while the developer is still typing.

**On reflection and the ECS, stated precisely so it is not mis-read later.**
These are two different systems and Slang does not replace `entt::meta`:
component reflection is C++ types keyed on stable names (D12, T0095) and Slang
cannot see a `MeshRenderer`. What they share is the **inspector**. A material is
an asset referenced by a component, so the editor must present reflected
*component* fields and reflected *shader* parameters in one panel, from two
sources. The intended direction is that the inspector consumes a single
description regardless of which reflection produced it — that is a T0142 concern,
and it is a unification of presentation, not of mechanism.

### The boundary, which is the whole decision

| | authoring | editor | cooked | shipped runtime |
|---|---|---|---|---|
| language | **Slang** | Slang + reflection | **HLSL or SPIR-V** | consumed by Diligent |
| `slangc` linked? | — | **yes** | yes (cook tool) | **no** |

**Diligent never learns Slang exists.** It receives HLSL text or SPIR-V bytecode,
both of which it already accepts, and its macros, generated structs, resource
signature and reflection all keep working untouched. The submodule stays
pristine, which is the owner's original constraint from D26.

**And no fork is needed.** Slang compiles DiligentFX's headers *as they are*, so
the ~5,000-line transitive closure — `PBR_Common`, `PCF`, `ToneMapping`,
`AtlasSampling`, `Iridescence` — stays upstream's and keeps receiving upstream
fixes. Forking was seriously considered and would have meant owning all of it,
including shadow filtering and tonemapping that T0086 and T0096 have not started.

### What was rejected

| | Verdict |
|---|---|
| **Fork DiligentFX's shaders into our tree** | **Rejected.** ~5,000 lines across 18 files, and a fork restructured into Slang stops being mergeable — every upstream fix becomes a hand re-application to a different shape, not a text merge |
| **Rewrite their PBR in Slang** | **Rejected**, for the reason D26 already gave: it reimplements split-sum IBL, PCF, tonemapping and the BRDF, and a wrong BRDF still looks like a material. Their physics stays theirs |
| **Struct inheritance** | **Rejected by Slang itself** — being removed from the language |
| **Slang as a runtime dependency** | **Rejected.** Cooking to HLSL/SPIR-V keeps `slangc` a build-machine tool; a shipped game links Diligent only |

### Costs accepted, and stated plainly

- **A second shader compiler in the toolchain.** Prebuilt per-platform archives
  (23–78 MB) pinned in `.harness/` the way zig, cmake and ninja already are, with
  no configure-time fetch. Build-time only.
- **Two compile steps** where there was one, on the HLSL path.
- **Depending on their private headers from Slang**, the same trade D26 already
  accepted — a signature change upstream breaks the build loudly.
- **Cooked shaders are a compiled *asset*, not a cache.** `Cook.hpp`'s invariant
  is that anything cooked can always be re-cooked from its source; that does
  **not** hold here, because an exported game has neither `slangc` nor the
  `.slang` source. A missing cooked shader is fatal, not recoverable, and the
  cook layer must say so rather than inherit the wrong contract.

### Not verified

*(As decided 2026-08-06. Struck-through items were verified later the same
day by T0142's first implementation session; the remainder still stand.)*

- ~~No permutation of a *real* engine shader has been compiled through Slang —
  only their headers plus a probe. The `.generated` interface structs would
  need feeding to Slang's include path rather than Diligent's factory.~~
  **Verified**: the engine's own surface shader and DiligentFX's
  `RenderPBR.vsh`, with the real permutation macros and generated structs,
  compile through Slang at pipeline-build time and render **digit-for-digit
  identical** pixel values to the glslang baseline, against the real resource
  signature, on both targets.
- No compile-time or runtime performance measurement.
- ~~Nothing on Windows~~ **Verified under wine** (the Windows suite loads
  `slang-compiler.dll` and matches its baseline); a native Windows host run is
  still owed. **D3D12/DXIL is moot** rather than unverified: this toolchain
  has no D3D12 backend at all (MinGW/ATL, see D25).
- `RenderPBR.psh` itself has not been compiled through Slang — and no longer
  needs to be: the engine's own pixel shader replaced it (D26/T0141.10).
- **New, found by measurement**: Slang's HLSL and GLSL outputs rename every
  resource (`cbFrameAttribs` → `cbFrameAttribs_0`), and Diligent binds by
  name — so the **OpenGL backend cannot consume Slang output today** and keeps
  Diligent's HLSL path over the same single source. SPIRV-Cross with a
  renaming pass is the known route to closing this.

**T0142 owns the adoption** and carries each of these as work.

---

## D29 — **Vulkan is the only backend.** OpenGL is removed

**Decided 2026-08-06 by the owner**, on the research below: *"I lean towards
ripping out opengl support all in all, we are a small studio anyway and wont be
able to support both."*

**Amends D1**, which recorded *"Both targets get Vulkan and OpenGL"* and the
backend table in `01-project-overview.md`. D1's substance — zig/MinGW, no
Direct3D, cross-compile from either host — is untouched. Only the second backend
goes.

### What the fallback was actually buying

Measured floor: the engine **never requests a Vulkan version**.
`EngineVkCreateInfo` has no `ApiVersion` field, `Instance.cpp` starts at
`VK_API_VERSION_1_0` and takes whatever the driver reports. So the requirement is
**Vulkan 1.0**, the 2016 specification — not 1.3.

That puts the hardware floor at 2012:

| vendor | floor |
|---|---|
| NVIDIA | Kepler (GTX 600). **Fermi never got Vulkan** — NVIDIA called it *"not an engineering issue but an install base issue"* |
| AMD | GCN 1 (HD 7000). Pre-GCN TeraScale never did |
| Intel | Skylake on Windows (2015); Haswell on Linux via Mesa, barely maintained |

Players genuinely below that floor are **low single digits** — ~91–92% of Steam
reports DX12-class GPUs, which is a close proxy because both APIs need the same
hardware generation, and Linux and macOS users are Vulkan-capable or out of
scope. **Not a verified figure**: Steam publishes no "% with Vulkan" number and
repeated fetches disagreed, so this is directional.

**Unreal made this exact cut in 4.26, December 2020.** Godot and Unity keep
desktop GL, but their stated reasons are **mobile and web**, where GLES and WebGL
genuinely cover devices Vulkan does not. This engine targets Windows and Linux
desktop only, so that reasoning does not transfer.

### Why removing it is a gain, not merely an acceptable loss

**The two backends disagree in ways that produce silently wrong images, and the
engine carries abstractions whose only purpose is to paper over it.**
`DepthConvention.hpp` exists because Vulkan clips Z to `[0, 1]` and OpenGL to
`[-1, 1]`, and *"the two disagree about which way"* the axis runs — its own
comment warns of *"a projection matrix that is right on Vulkan and silently wrong
on OpenGL."* `combinedSamplers = IsGLDevice()` splits the shader path. A second
path that is exercised less and fails quietly is exactly the shape of bug this
tree lost a full session to on 2026-08-06, twice over: a duplicated `CreateInfo`
that hid an unset `TextureAttribIndices`, and a feature flag that left the
occlusion map bound and unread.

**And it is what unblocks D28.** Slang's HLSL and GLSL emitters rename every
resource (`cbFrameAttribs` becomes `cbFrameAttribs_0`) and GL binds by name, so
GL cannot consume Slang output — measured. Diligent's GL backend also refuses
bytecode outright (`ShaderGLImpl.cpp:221`). Keeping GL therefore pins
`HpSurface.slang` to the subset both compilers accept, which makes `IHpMaterial`
— interfaces, default implementations, `override`, the entire reason D28 was
adopted — **unreachable**. That is the concrete, recurring price of the fallback.

### What was rejected

| | Verdict |
|---|---|
| **Keep GL, route Slang → SPIR-V → SPIRV-Cross → GLSL** | **Not taken.** Technically plausible — SPIRV-Cross is vendored and Slang's SPIR-V preserves names — but it means owning a translation step to keep a backend worth low single digits, and the studio has said it cannot support two |
| **Add Direct3D 12 instead** | **Rejected, and it is not close.** D3D would genuinely buy better Windows driver tuning, DirectStorage and PIX — but MinGW has no `atlbase.h`, DiligentCore's own probes disable D3D11/D3D12, and having it means the MSVC ABI plus a real Windows SDK, *which cannot be driven from a Linux host*. The choice is not "Vulkan or D3D"; it is "Vulkan only" versus **giving up cross-compiling Windows from Linux** (D1, D3). It stays available later if a concrete need appears |
| **Keep GL for CI and QA** | **Rejected on the owner's answer.** Nobody develops or tests in a VM or over RDP. This was the one genuine risk — Hyper-V's GPU-P supports DX11 and OpenGL but **not** Vulkan, RDP forwards no acceleration, and Windows has no ubiquitous software-Vulkan fallback the way GL has GDI-generic |

### The cost, stated plainly

- **A breaking public API change.** `RenderBackend::OpenGL` and
  `WindowConfig::openGLContext` are removed, and `RenderBackend::Default` no
  longer means "try Vulkan, fall back".
- **Intel Haswell/Broadwell on Windows** (2013–14) never had a Vulkan driver and
  are lost outright. So is any machine whose OEM driver package never registered
  a Vulkan ICD, on otherwise capable silicon.
- **GPU tests on a Windows CI runner become impossible** until that runner has a
  real Vulkan device. `gpu_adapter_report_test.cpp` records the current one as
  *"gdi generic — Windows' generic OpenGL 1.1"*. Survivable only because the GPU
  bucket is built and never run in CI.
- **No fallback of any kind.** A machine without Vulkan does not start the
  renderer, and the failure must be a clear message rather than a crash.

**T0144 owns the removal.**

---

## D30 — Shading control is a ladder, and the engine owns the light loop

**Decided 2026-08-06, on the owner's goal**, in their words: *"I want the game
devs to be able to either use Diligent PBR and Lights and so on as is, OR
override them with their own shaders, or just parts of it with their own
shaders"*, *"we need to have Godot-like control but even more"*, and the
constraint that grounds both: *"a junior game dev can just start using it, and
an advanced game dev can take control big time."*

**Amends D24** — the lighting *functions* stay Diligent's, called by default;
the *loop that calls them* becomes ours. **Extends D27** — the contract grows a
lighting stage under exactly the rules the surface stage already obeys.
**T0145** executes the loop; T0146 (vertex), T0147 (intermediates), T0148
(post), T0149 (styles) and T0150 (compute) fill the other rungs.

### Progressive disclosure is the design principle, not a nicety

That last sentence of the owner's is a **requirement on the shape of the API**:
every level of control must be reachable **without learning the level above
it**. A developer who picks a style never sees a shader; one who overrides
`baseColor` never learns what a light loop is; one who owns the loop still
never touches PSO creation. The ladder, with each rung's boundary stated:

| Rung | The developer... | The engine still owns | Mechanism | Status |
|---|---|---|---|---|
| 0 | picks a style in the inspector | everything | style bundles (**T0149**) | not built |
| 1 | edits material parameters | every shader | `.hpmat` (T0060) | **live** |
| 2 | overrides one surface method, inherits the rest | `main`, lighting, the loop | `IHpMaterial` defaults (D28, 142.2) | **live** |
| 3 | overrides the response to **one light** | the loop, attenuation, the shadow lookup | per-light method with a default (**T0145**) | not built |
| 4 | owns the **light loop** and the shading model | the pass, its targets, the light *data* | lighting-stage method with a default (**T0145**) | not built |
| 5 | owns passes, targets and pipelines | frame anatomy (D17), device lifetime (D22) | `IRenderLayer` (T0027/T0094), compute (T0150), post (T0148) | partial |

Each rung is a **promise with D27's terms**: a method with a default is added
freely and removed never. And each rung obeys D27's other rule — nothing is
exposed before the system behind it exists, so the per-light method gains a raw
shadow factor **with T0086**, not before.

### Why the loop must be ours — the seam was measured, not assumed

Where an override could attach inside DiligentFX's public `PBR_Shading.fxh`
(verified against the vendored submodule, 2026-08-06):

| Seam | What it is | Verdict |
|---|---|---|
| `ResolveLighting` (:847) | four terms: base + emissive, sheen, clearcoat | thin, overridable |
| `GetBaseLayerLighting` (:813) | **one line**: `Base.Punctual + GetBaseLayerIBL` | thin, overridable |
| `ApplyPunctualLight` (:601–721) | range attenuation, spot cone, **shadow-map lookup + `FilterShadowMapFixedPCF` (:655)**, then the base-layer BRDF **inline** — `SmithGGX_BRDF` at :690, not through any seam | **the per-light BRDF has no hook** |

The clearcoat layer goes through `ApplyDirectionalLightGGX` (:718); the base
layer does not. So Godot's per-light rung **cannot be offered by calling their
loop body** — there is no point inside `ApplyPunctualLight` where a game's
shading model could be substituted. Offering rung 3 means the engine mirrors
the loop body — attenuation, cone, shadow lookup, ~120 lines — into its own
lighting stage and calls `SmithGGX_BRDF` and friends from there *as the
default*. Rung 4 then falls out of the same move for free: the whole stage is
already a method with a default.

The struct economics support the same split: the per-light hook's natural
vocabulary (`SurfaceReflectanceInfo`) is **four primitive fields**
(`Common/public/PBR_Common.fxh:362`), while the coarse hooks drag
`SurfaceShadingInfo` (12 fields, nested layer structs, `#if`-conditional
members) and `SurfaceLightingInfo`. The fine-grained promise is the cheap one
to keep; the coarse ones are where D31's mirroring earns its cost.

### Godot's ceiling, verified — and where "even more" is a real claim

Surveyed against Godot **4.7.1-stable** (current, 2026-08-06; sources in
T0145):

- Godot's `light()` receives `ATTENUATION` with **shadow already folded in**;
  there is no raw shadow factor, and the open proposal asking for one
  (godot-proposals #15040) is the evidence the ceiling is felt.
- The light **loop is fixed engine C++**. `light()` is a per-light callback;
  a `light_post()` for cel shading has been an open proposal since 2019
  (#484). Rung 4 does not exist in Godot at any price short of a fork.
- **No partial override exists between materials** — a `ShaderMaterial`
  replaces the whole shader, and StandardMaterial3D → shader conversion is
  one-way code generation. Rung 2 — inherit everything, override one method —
  is something Godot does not have at all.

So "Godot-like" is rungs 0–3, and "even more" is specifically: rung 2's
piecewise inheritance, rung 4, the raw shadow factor (when T0086 exists), and
rung 5's C++ pass ownership (D22) where Godot's equivalent proposals
(#4287, #10778) remain open.

### Sequencing — this is materially cheaper before T0086, on both sides

The shadow lookup lives **inside** `ApplyPunctualLight`. If T0086 builds shadow
sampling against Diligent's loop first, moving to our loop afterwards relocates
shadow sampling a second time — the same argument T0141 recorded for the pixel
shader, one level deeper, and the same reason T0086 already waits on 141.10.
T0145 must land its loop **before** T0086 starts; both tickets record it.

### What was rejected

- **Handing games `PBR_Shading.fxh` or the loop directly.** D27's trap
  unchanged: every upstream rename breaks every shipped game. We include them;
  games include us.
- **Toon-by-post-process as the stylisation mechanism.** A post pass cannot
  see per-light data — it quantises the *sum*, so a two-light surface bands
  wrongly and rim terms are unrecoverable. Fine as one style's deliberate
  choice; wrong as the only mechanism. Godot's open #484 is what that ceiling
  looks like from inside.
- **Slang dynamic dispatch as the override transport.** Measured on the pinned
  2026.14.1 (probes in T0151): existential dispatch to SPIR-V **works** —
  `-conformance` registration, an `OpSwitch` over type IDs — but an existential
  material **may not hold opaque members** (`error[E33080]`), so textures
  require `DescriptorHandle<T>` bindless handles (compiles; demands
  `RuntimeDescriptorArray` descriptor indexing the engine has not adopted),
  and the dispatch is a per-wave branch over every registered material.
  Static specialisation (D28) stays; the dynamic path is recorded in T0151 as
  the pipeline-count escape hatch, with a trigger, not a default.
- **A second, stylised renderer.** D24's revisit clause named "a stylised
  non-PBR renderer" as what would reopen it. The ladder is the answer that
  keeps it closed: a toon look is a rung-3/4 override *inside* the one forward
  path — same passes, same shadow maps, same culling — not a second render
  path with its own bugs.

### Costs, stated rather than discovered

- **~120 mirrored lines** whose upstream original keeps moving: every
  DiligentFX bump needs a diff of `ApplyPunctualLight` against our stage.
  The D26 class of cost, one level deeper; T0145 owns a drift guard so the
  check is mechanical rather than remembered.
- **Every rung is API surface promised forever.** The mitigation is D27's,
  unchanged: generous but deliberate, decided per method.
- **Registers.** An interface-heavy main with per-light methods may cost
  occupancy against the fused original. Unmeasured; T0145 measures before/after
  on the byte-identical baseline.

### Not verified

- No rung-3 or rung-4 override has compiled end to end. Above rung 2 the
  ladder is designed here, not built; T0145's acceptance test is the existing
  byte-identical baseline discipline (141.11/142.3).
- The ~120-line estimate is from reading :601–721, not from a completed port.
- No performance measurement of an overridden loop exists.

### Amended 2026-08-06 — the owner accepted the maintenance, and named how to build for it

The standing cost this decision commits to — roughly 120 lines mirrored from
`ApplyPunctualLight`, re-diffed against DiligentFX on every submodule bump — was
put to the owner in plain terms and **accepted**, with a steer that changes how
the drift guard is built: *"Claude Code will be doing the work so we should
optimize for the right path."*

So the guard is not a minimum-effort tripwire that spares a human a chore. It is
built to catch a real upstream change and show what moved, and **the
architecture is not compromised to shrink a cost that is not a human one.** The
upstream hook is still worth offering — a merged seam beats a maintained copy —
which is T0141's C1/C3 reasoning applied one layer down.

### Why rung 3 is load-bearing rather than optional: material switching cannot substitute for it

Recorded because the cheaper-looking alternative is genuinely tempting and is
what a reader will reach for first.

**Swapping a material changes the surface — albedo, roughness, normals. It
cannot change how light is applied.** Toon shading is quantised `N·L`, which
lives in the light loop, so no amount of material assignment reaches it. The
evidence is the comparison engines: **Unreal's shading models are fixed, so toon
there means a post-process material or an engine-source edit; Godot's is
achievable because it exposes `light()`.** That difference is the whole argument
for owning the loop.

The consequence for the style work is direct: T0149's styles are content
assembled from primitives (see its own notes and the Godot/Unreal finding), and
**rung 3 is the primitive that makes a toon style possible at all** — not an
enhancement to it.

---

## D31 — The lighting contract **mirrors** DiligentFX's types; it never re-exports them

**Decided 2026-08-06, with D30.** Rung 3 and 4 signatures must name a light and
a surface. The question is whose types those are, and it is the same question
D27 answered for functions, now for **data**.

**Decision: the contract's types are the engine's** — call them `HpLight`,
`HpShadedSurface` — restating the fields the ladder needs, converted from
`PBRLightAttribs` / `SurfaceShadingInfo` inside the engine's own shader code.
A game shader never names a Diligent struct.

### Why mirror wins

- **D27's argument, applied to fields.** An upstream rename of
  `PBRLightAttribs.IntensityR` is then a compile error in **one file of ours**,
  not a break in every shipped game's shaders. Re-exporting (aliasing) their
  structs hands every field name to upstream forever.
- **The conditional-member hazard, which re-export cannot fix.**
  `SurfaceShadingInfo`'s sheen, clearcoat, anisotropy, iridescence,
  transmission and volume members exist **only under their `#if`s** — a
  re-exported struct changes *shape* per permutation, so a game shader reading
  `.Sheen` compiles on some materials and not others. A mirrored struct keeps
  every field present (zeroed when the feature is off, T0143), which is a
  genuine improvement, not just insulation.
- **Their packing is not an interface anyone would choose.** `PBRLightAttribs`
  carries `DirectionX/DirectionY/DirectionZ` as separate floats and intensity
  as `IntensityR/G/B` — constant-buffer packing artifacts. The mirror hands a
  game `float3 direction; float3 color;`, and the packed original stays what
  the engine writes to the GPU.

### The cost, and what is unmeasured

Conversion code in the engine's lighting stage — a repack per light, per
fragment, in registers. **Expected negligible** (the same values are loaded
either way; no extra memory traffic) and **not measured**; T0145 measures it
against the byte-identical baseline. The mirror itself grows with T0143's
features, one struct field per feature, in the one file that already changes
when a feature turns on.

**Rejected — a second CPU-side buffer in engine layout**: writing `HpLight`
per frame beside `PBRLightAttribs` doubles the light data and adds a second
writer to keep in step (the exact drift shape that hid `TextureAttribIndices`).
Conversion at point of use has one source of truth.

**Not verified:** none of the mirror exists yet; the register cost above is a
prediction. If measurement shows the repack costs real occupancy, the fallback
is mirroring *layout-compatibly* so conversion is a reinterpretation — noted so
the option is seen when the number arrives, not invented under pressure.

---

## D32 — Shader types: one override mechanism per domain, and particle simulation stays out of reach

**Decided 2026-08-06, with D30.** Godot ships six shader *types* — spatial,
canvas_item, particles, sky, fog, texture_blit (4.7) — plus compute in raw GLSL
through `RenderingDevice`. HollowPoint has exactly one authorable domain today
(the surface material), and the absence of the rest was a gap nobody had
written down. This entry makes each one a decision.

**The mechanism does not multiply; the domains do.** Every domain a game can
author uses D28's one shape — a Slang `interface` whose defaults are the
engine's behaviour — arriving **with its owning system**, never before
(D27's rule):

| Domain | Godot's | Ours | Arrives with |
|---|---|---|---|
| surface material | `spatial` | `IHpMaterial` | **live** (142.2) |
| per-light / lighting | `light()` | lighting stage | **T0145** |
| vertex | `vertex()` | vertex stage | **T0146** |
| post effect | CompositorEffect (4.3) | post stack | **T0148** |
| sky | `sky` | sky shader override | T0088, when it exists |
| fog | `fog` | fog override | T0089/T0091, when they exist |
| compute | raw GLSL via `RenderingDevice` | **Slang**, same modules as materials | **T0150** |
| 2D / UI | `canvas_item` | undecided with T0069's library | Phase 12 |

Sky and fog carry the obligation on their own tickets: when the system lands,
its shader is authored against an interface a game can implement, not as a
sealed engine file. That costs the owning ticket one design constraint now and
saves the retrofit D27 exists to prevent.

### Particles are the deliberate exception, and it is D15's line, not an oversight

Godot exposes particle *process* shaders — `start()`/`process()` — so gameplay
authors simulation. HollowPoint **does not**, and the divergence is deliberate:

- D15's enabling constraint is that particles are **cosmetic** — gameplay
  never reads their state, which is what buys GPU-only simulation, a single
  dispatch over shared buffers, and a fixed budget with enforced degradation.
- A game-authored process shader punctures all three: it invites reading back
  what it wrote, it fragments the one-dispatch model, and a budget cannot be
  enforced against arbitrary spawning logic.
- What games author is particle **appearance** — T0106's sprites, flipbooks
  and blend modes, which flow through the same material mechanism as every
  other surface — and effect *composition* (T0107).

**Revisit when** a wanted effect is genuinely inexpressible as authored curves
plus emission shapes (T0080) — at that point the D15 trade is re-argued in the
open, rather than eroded by a hook that seemed harmless.

### Also deliberately absent, so the absences are decisions

- **A visual (node-graph) shader editor.** Godot ships one and its own docs
  concede it does not cover the language. Rungs 0–1 (styles, parameters) are
  this engine's answer to "junior developer, no code"; a node graph is an
  editor-era question for the owner, not an engine gap.

  **Answered 2026-08-06 — wanted eventually, with one binding constraint.** The
  owner: *"Probably in the editor yes, but i assume thats a function of the
  editor right?"* Mostly yes, and the part that is *not* purely the editor's is
  worth deciding now because it costs nothing today:

  > **A visual shader graph must emit an ordinary `.slang` material implementing
  > `IHpMaterial`.** Never its own runtime evaluation path.

  If the graph emits normal materials, it stays an editor feature and inherits
  every rung, every override and every optimisation for free. If it gets its own
  evaluation path, there are two systems doing one job — which is precisely the
  outcome **D26** and **142.13** exist to prevent, and the shape that let the
  `CreateInfo` duplication hide `TextureAttribIndices`. Recording the sentence
  now is the whole cost of never having that argument.
- **Stencil render modes.** Godot added them in 4.5, still marked
  experimental. Nothing here owns stencil-driven material effects; if a game
  needs them, that is a T0045/T0094-shaped conversation and this line is where
  it starts.
- **Geometry shaders.** Godot refuses them too; tessellation is already
  covered by 141.9's named trigger.

## D33 — **Hardware facing equals glTF facing.** `FrontCounterClockwise` is `false`, declared; single-sided culls `BACK`; and the "engine-wide winding inversion" was the test assets

**Decided 2026-08-06**, triggered by the owner's question after T0141.12's
single-sided workaround: *"my worry is we are moving away from Vulkan
standards, are we? what's the right direction?"* Root-cause trace and the
convention header live on **T0152**; this entry records what was measured,
what was rejected, and what turned out to be wrong.

### The premise the question rests on is false, and that is the finding

**Vulkan has no winding standard.** Facing is the sign of the triangle's
signed area in framebuffer coordinates, and the spec defines
`VK_FRONT_FACE_COUNTER_CLOCKWISE` and `VK_FRONT_FACE_CLOCKWISE`
symmetrically, with no default and no preference — unlike OpenGL, whose
state machine defaults to CCW
([Vulkan spec, Basic Polygon Rasterization](https://docs.vulkan.org/spec/latest/chapters/primsrast.html#primsrast-polygons-basic)).
The standard that binds this engine is **glTF's**, because glTF is the only
mesh format (D13's pipeline): front faces are counter-clockwise seen from
the front, and the sign of the node transform's determinant flips it —
that determinant clause is the spec's *only* normative winding sentence
([glTF 2.0 §instantiation](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#instantiation));
"glTF is CCW" is its identity-transform corollary.

### What was measured (T0152.1, all in-tree)

Counting winding reversals from a glTF index buffer to the rasteriser:
import converts nothing (`GLTFLoader.cpp` has no axis or index conversion);
the view is a rigid inverse (det +1); the left-handed projection has no XY
mirror; **reverse-Z is winding-neutral** (`SetNearFarClipPlanes` writes only
`_33`/`_43` — checked because a near/far swap *sounds* like a determinant
change, and it is one only in the 4×4 sense that facing never consults);
Diligent's Vulkan backend flips the viewport internally and unconditionally
to D3D screen conventions (`DeviceContextVkImpl.cpp:1831`) and passes
`FrontCounterClockwise` through raw (`VulkanTypeConversions.cpp:718`), so
the flag means the same thing on every backend, flip included. **Zero
reversals in the engine's own chain.** Under D3D's clockwise-front default —
the current, never-set value — a glTF front face facing the camera
classifies **front**. The engine was conformant all along.

**T0141.12's diagnosis inverted cause and effect.** Its probe quad — like
every quad in the gpu suite — is wound `{0,1,2, 0,2,3}` over BL,BR,TR,TL
vertices: right-hand-rule normal **+Z**, away from the camera, while its
authored `NORMAL` attributes say −Z, toward it. `SV_IsFrontFace == false`
on those fragments was the *correct* classification of an asset whose
winding contradicts its normals; it was read as an engine-wide inversion
because the normals were taken as ground truth. The lit suite's calibration
is the same inconsistency compounded: its light travels −Z onto the +Z
face, the side facing *away* from the camera, and the pixels are lit only
because the two-sided flip — handed `false` — inverts the authored normal
to meet it. Setting `FrontCounterClockwise = true` went black not because
`true` is deeply wrong but because it is wrong *for this mirror-free chain*,
and the scenes depend on the inverted flip.

### Rejected

- **`FrontCounterClockwise = true` "because Diligent's own GLTFViewer sets
  it".** The viewer's chain contains a mirror this engine's does not:
  `InvYAxis` (det −1) on the model transform (`GLTFViewer.cpp:240–243`),
  plus `InvZAxis` for glTF cameras. One mirror flips apparent winding once,
  so their `true` and our `false` are the *same* convention. Raw-enum
  comparisons across engines are meaningless without the full mirror count —
  Godot's clockwise front (projection Y-flip, import conversion) and
  Filament's CCW front (no viewport negation, native glTF) also both land on
  the identical invariant: **glTF front = hardware front, single-sided culls
  back.** Every surveyed engine preserves it; they differ only in where the
  mirrors sit.
- **Keeping the `CULL_MODE_FRONT` workaround.** It keeps the faces a
  conformant renderer culls and culls the faces it keeps; it renders today's
  backwards test assets and will invert every correctly-exported asset a DCC
  tool produces. It dies with T0152.4, in the same commit that re-winds the
  assets — either change alone blanks every single-sided draw.
- **Re-baselining against the current light positions.** That would freeze
  in the flip-dependence. Rejected as unnecessary as much as wrong: the
  corrected lit scene is the mirror image of the current one, so its
  measured (211, 144, 144) survives by symmetry, and the suite's other
  assertions are deliberately orientation-independent. The feared
  "re-baseline every pixel test" measured out at six index lines, two
  re-aimed lights, two engine lines.

### What turned out to be wrong

The three-factor attribution recorded on T0141 — "glTF's CCW winding × the
left-handed view × Vulkan's viewport flip" — names three real facts of which
none contributes a reversal: the LH view is det +1, the viewport flip is
pre-compensated inside Diligent's flag semantics, and CCW-ness is what the
current default already honours. The comment at `SurfacePipeline.cpp:480`
("a glTF front face reaches the rasteriser wound counter-clockwise") is the
same error in place: the face it describes is, by winding, a back face.

### The accepted cost, and the coupling that keeps it honest

The convention is `WindingConvention.hpp` (T0152.2), modelled on
`DepthConvention.hpp`: `kFrontFaceCounterClockwise = false` and
`kImportMirrorsContent = false`, chained by a `static_assert`, because one
mirror introduced anywhere — an import axis-flip, a negative camera-parent
scale — toggles apparent winding once and the two must move together. An
uncompensated mirror does not look like a mirror; it looks like every
single-sided mesh vanishing.

**Left open, deliberately, for the owner:** the same trace shows the engine
displays right-handed content mirror-imaged (the real consequence of the LH
camera — derived and cross-anchored to the facing measurement, not yet
observed on screen; T0152.6 is the probe). Accepting that as the authored
convention, or spending exactly one mirror to remove it — flipping both
constants above with it — is a content-pipeline decision, not a winding one,
and it must be taken before real DCC assets arrive. What D33 fixes either
way: whichever parity is chosen, hardware facing equals glTF facing, and
`CULL_MODE_BACK` means what the spec means by it.

**Sequencing:** T0086 tunes shadow bias with cull-face as its knob, T0087
adds view-dependent IBL baselines, T0143 adds per-feature pixel tests.
Every one of them calibrated against inverted assets makes the correction
strictly more expensive; none of them starts before T0152 lands.
