# T0025 — Render layer and device lifecycle

| | |
|---|---|
| **Status** | 🔜 TODO |
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

- [ ] 25.1 Device/context/swapchain creation via Diligent's engine factories
- [ ] 25.2 Backend selection and command-line override
- [ ] 25.3 Resize handling, driven by the window resize event (T0018)
- [ ] 25.4 Shutdown ordering — GPU resources released before the device
- [ ] 25.5 Profiling zones using the T0019 macros while writing, not after
- [ ] 25.6 Enable Vulkan validation layers in debug builds

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
