# T0025 — Render layer and device lifecycle

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 380 |
| **Created** | 2026-08-02 |
| **Refs** | T0100, T0110 (Blocks this), T0113, [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md), [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D15 |

## Why

Rendering becomes its own system layer on the LayerStack (T0017), which is what
lets the runtime reuse it in Phase 8 with no editor present.

This owns the Diligent device, contexts and swap chain, and their lifetime
relative to everything that allocates GPU resources.

## Done when

- [ ] `RenderLayer : ILayer` owning device, immediate context and swap chain
- [ ] Backend selectable at runtime (Vulkan default, OpenGL fallback)
- [ ] Window resize resizes the swap chain without artefacts or leaks
- [ ] Clean shutdown with no validation-layer complaints
- [ ] Runs on both Linux and Windows targets

## Subtasks

- [x] 25.1 Device/context/swapchain creation via Diligent's engine factories — Vulkan up on real hardware; see below
- [ ] 25.2 Backend selection and command-line override — the selection path exists and both backends are reachable, but **OpenGL cannot come up at all** (see below) and there is no command-line override yet
- [ ] 25.3 Resize handling — `RenderLayer::resize()` works on a live swap chain (measured), but is **not yet wired to the window resize event**
- [x] 25.4 Shutdown ordering — GPU resources released before the device — flush, wait-for-idle, then swap chain, context, device
- [x] 25.5 Profiling zones using the T0019 macros while writing, not after
- [x] 25.6 Enable Vulkan validation layers in debug builds

## Notes / findings


### Frame anatomy — phases 10 and 11 — render, present (T0100, D17)

Render is **phase 10**, present is **phase 11**. Render reads
`Clock::interpolationAlpha()`; without it, rendering at a rate different from
the fixed step stutters visibly even at high frame rates. The *policy* behind
present belongs to T0110, not here.

The full order is in [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md); the decision and what it rejected is **D17** in the
decision log. If this ticket needs a phase that does not exist, that is a change
to T0100's document and to D17 — not a new call bolted into `Application::run`.

Both engines are **shared libraries** (`GraphicsEngineVk_64r.dll`,
`GraphicsEngineOpenGL_64r.dll`) and must sit beside the executable on Windows —
`cmake/dist.cmake` already stages them correctly.

Diligent loads the backend through a factory; on Linux the `.so` is found via
RUNPATH, which the harness already sets (and deliberately excludes the sysroot
stubs from — G6).

Turn validation on in debug from the very first frame. Every hour spent without
it is an hour of bugs deferred to a worse time.

### Note (2026-08-03) -- two tickets now attach to this one's device code

From the design-gap survey (`documentation/07-design-gaps.md`, items 1 and 5):

- **T0110 (presentation) gates this ticket** and is ordered before it. The
  swap chain this ticket creates must be created against T0110's present-mode
  policy -- without it, Diligent defaults to `VK_PRESENT_MODE_IMMEDIATE_KHR`
  (uncapped, tearing), which is what completed T0003's own log shows already
  happening. Do not harden 25.1 with an unexamined present mode.
- **T0113 (device loss)** adds detection to this ticket's present/submission
  error paths. Creation, resize and shutdown were owned here; the device dying
  *mid-run* was owned nowhere.

### Amendment (2026-08-03) — this ticket is where the engine first links Diligent

`engine/` deliberately links **nothing** from Diligent today. T0013 made that
choice explicitly (13.3): nothing needed a device, and a careless PUBLIC link
would have handed ImGui to every gameplay module via DiligentFX (D6). It was the
right call, and it left a prerequisite with no owner.

**This ticket is that owner.** Three items from other tickets are blocked on it
and were closed pointing here rather than pinning their own tickets open near
the top of the queue — T0054 sits at order 40 and T0056 at 50, so leaving them
blocked would have shown two tickets at the head of the board that could not
move until order 380.

| Waiting item | From | What it needs |
|---|---|---|
| Route Diligent's `DebugOutput` into the engine logger | T0054 (54.5) | A `SetDebugMessageCallback` to install |
| Confirm the SSE/NEON math paths are on in release | T0056 (56.2) | `BasicMathSSE.hpp` / `BasicMathNEON.hpp` compiled in |
| Adopt `DynamicLinearAllocator` for per-frame scratch | T0056 (56.3) | The allocator to exist in the link |
| Adopt `FixedBlockMemoryAllocator` where pooling pays | T0056 (56.4) | The allocator, **and a profile** — see below |

**Do them when Diligent arrives, not later.** The logger one in particular is
most of T0054's value: validation-layer output, shader compile errors and engine
warnings landing in the same stream and the same editor console is worth far
more than any of the logging machinery itself.

**56.4 has a second blocker that Diligent does not clear.** "Where pooling pays"
needs a measurement, not a linked library. Adopting a pool allocator because it
became available is how a codebase acquires complexity it cannot justify — that
one waits for T0031's budgets and a profile, and should not be ticked here just
because the type is reachable.

**Also inherited: the PUBLIC/PRIVATE rule.** T0013 wrote it down and this is
where it first applies — link Diligent targets PRIVATE unless a *public engine
header* names their types. Getting it wrong is not a build error; it silently
widens every consumer's dependency surface.

### Cross-ticket obligations (2026-08-04, T0124 backfill)

- **D15** sets an OpenGL 4.3 floor: particles are GPU-compute-only, so 25.2's
  backend selection must verify the GL fallback actually provides >= 4.3 with
  compute — D15 says this ticket's fallback assumptions "should be checked
  against that", and the check lives here. A GL device below the floor should
  fail loudly at selection, not when the first emitter dispatches nothing.

## 25.1 done (2026-08-04) — a Vulkan device, on real hardware

```
[info ] render: Vulkan on 'NVIDIA GeForce RTX 2080', 1280x720, 3 buffers, vsync on
[info ] render: swap chain buffer count 2 requested, 3 granted by the surface
[info ] render: device released
[info ] app: HollowPoint Runtime ran 3 frame(s) in 0.252s, exit 0
```

`hp::RenderLayer` is an `ILayer` holding device, immediate context and swap
chain, clearing and presenting at frame phases 10 and 11. **This is the first
time the engine links Diligent at all.** It is linked PRIVATE, and
`hp/Render.hpp` is a pimpl specifically so that stays true — `src/Render.cpp` is
the only translation unit in the engine that includes a Diligent header.

### Finding 1 — the Windows link fails, and the fix is per-platform

Linking the engine DLLs' import libraries does not work under this toolchain:

```
lld-link: error: duplicate symbol: atexit
>>> defined at .../mingw/crt/crtdll.c:180 (dllcrt2.obj)
>>> defined at libGraphicsEngineVk_64r.dll.a(GraphicsEngineVk_64r.dll)
```

`GraphicsEngineVk_64r.dll` **exports `atexit`**, and zig's MinGW `dllcrt2.obj`
defines it. Diligent's own answer is explicit loading — link the interface
target only (headers, no import library) and `LoadLibrary` the DLL at run time.
`LoadEngineDll.h` includes `<Windows.h>`, so that path exists on Windows and
nowhere else, which is why the link strategy is genuinely per-platform rather
than a preference:

| target | link | factory |
|---|---|---|
| Windows | `*Interface` + `DILIGENT_*_EXPLICIT_LOAD=1` | `LoadGraphicsEngineVk()` |
| Linux | `*-shared` | `GetEngineFactoryVk()` |

Worth knowing: the explicit-load macros are `DILIGENT_VK_EXPLICIT_LOAD` and
`DILIGENT_OPENGL_EXPLICIT_LOAD`. The first draft guessed
`EXPLICITLY_LOAD_ENGINE_VK_DLL`, which compiled fine as a dead branch and would
have silently taken the wrong path forever.

### Finding 2 — the OpenGL backend cannot come up, and it is the window's fault

```
[error] render.diligent: No current GL context found! (GLContextLinux.cpp:47)
[error] render.diligent: Failed to initialize OpenGL-based render device
[error] render: no graphics device could be created (OpenGL requested)
```

`Window::create` asks SDL for `SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY`
and nothing else — no `SDL_WINDOW_OPENGL`, no GL context — while Diligent's Linux
GL backend attaches to a context that must already be current.

**This is an ordering constraint, not a missing flag.** The window is created
before a backend is chosen, so it cannot know whether to make a GL context; and
a window created *with* `SDL_WINDOW_OPENGL` is not free for Vulkan either. The
fix is a decision about who chooses the backend and when — plausibly the config
must name it before the window opens. That is 25.2's real content and it was
invisible until a device was attempted.

**D15's OpenGL 4.3 compute floor is therefore unchecked**, because no GL device
exists to query. The check still belongs in 25.2.

### Finding 3 — Diligent's diagnostics needed routing, per factory

Diligent wrote ~250 lines straight to stderr with its own ANSI colouring,
bypassing the engine log entirely — so the editor console (T0066), the log file
and every other sink would never have seen a driver warning.

`SetDebugMessageCallback` — the global — **compiled, ran, and changed nothing**:
each Diligent engine library carries its own copy of that global, the same
statics-per-artifact property behind T0105.1 and T0127. `IEngineFactory::SetMessageCallback`
is per-factory and does work. Output went from ~250 stderr lines to 13 through
`hp::Log`, with the extension dump at Debug where it belongs.

### Finding 4 — the surface overrides what you ask for

Requesting 2 buffers returned 3 ("minimal image count supported for this
surface"), and `RGBA8_UNORM_SRGB` came back as `BGRA8_UNORM_SRGB`. The layer now
logs the **created** description rather than the requested one; logging the
request would have told a latency investigation a quiet lie. The sRGB
substitution is T0096's business.

Also confirmed, exactly as T0110's correction predicted from reading the source:
`Using VK_PRESENT_MODE_FIFO_RELAXED_KHR swap chain present mode` with vsync on.

### Verified

```
zig build test -Dtest=all   18/18 steps, 24/24 tests, 55 integration + 49 fast, both targets
zig build docs              exit 0
RenderLayer::resize(800, 600) on a live Vulkan swap chain -- still ready, clean release
```
