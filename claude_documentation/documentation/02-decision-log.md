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
