# T0046 — Frame render target management

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Simple |
| **Phase** | 4 — Render layer |
| **Order** | 390 |
| **Created** | 2026-08-02 |
| **Refs** | T0111 (Blocks this), T0094, T0106, T0107, T0120 |

## Why

Passes share resources: a depth buffer written by the world pass and read by
post-processing, ping-pong targets for blurs, the offscreen colour target the
editor viewport displays. Something has to own their creation, resizing and
lifetime.

Small and concrete — deliberately *not* a render graph (see T0047). A named,
explicitly-managed set of frame resources.

## Done when

- [ ] Frame targets created once and resized with the viewport, not per frame
- [ ] Passes request targets by name/handle rather than creating their own
- [ ] Resize is debounced and leak-free
- [ ] Formats are declared in one place, not scattered across passes
- [ ] GPU memory used by frame targets is reportable
- [ ] Gameplay-owned persistent targets are supported alongside frame targets (T0094)

## Subtasks

- [ ] 46.1 A frame-resources object owning the render targets
- [ ] 46.2 Named lookup for passes
- [ ] 46.3 Resize handling, debounced (T0033 has the same requirement)
- [ ] 46.4 Depth buffer shared between world pass and post-processing
- [ ] 46.5 Ping-pong pair for multi-pass effects
- [ ] 46.6 Report allocated target memory for the profiler

## Notes / findings

**Do not add resource aliasing.** Reusing memory between non-overlapping targets
is the main thing a render graph automates, and doing it by hand is error-prone
in exactly the way that produces intermittent corruption. If memory pressure ever
justifies it, that is the argument for revisiting T0047 — not for hand-rolling
aliasing here.

Diligent's automatic state transitions (`RESOURCE_STATE_TRANSITION_MODE_TRANSITION`)
mean we do **not** need manual barrier tracking, which removes the other main
reason engines build resource managers. Use the automatic mode unless profiling
shows it costs something.

`DiligentFX/Components/GBuffer.hpp` already manages a GBuffer's targets — use it
rather than duplicating it if deferred shading is wanted.

### Amendment (2026-08-03) -- constraints to honour before the frame layout solidifies

Collected from T0106 and the design-gap survey (`documentation/07-design-gaps.md`,
items 2 and 8), because this ticket is where they become cheap or expensive:

- **Depth must be readable during the transparent pass.** Already flagged by
  T0106.5 -- soft particles fade against scene depth while drawing transparents.
  Restated here so the requirement lives with the resource design, not only
  with its first consumer.
- **Scene colour must be readable during the transparent pass, eventually.**
  Screen distortion appears exactly once in the backlog, as a line in T0107's
  anatomy of an explosion ("optionally screen distortion for the shockwave"),
  and no ticket owns a distortion pass. It has the same shape as the
  soft-particle constraint: distortion needs *scene colour* readable mid-frame
  exactly as soft particles need *depth*. One sentence here keeps the door
  open -- a resolve/copy point of the colour target that a later pass can
  consume; discovering it after the frame layout solidifies is a refactor.
  The distortion pass itself remains unowned and unbuilt until VFX demand it.
- **T0111 lands first, by design** (it Blocks this ticket): the anti-aliasing
  decision changes what "formats are declared in one place" declares (sample
  counts, TAA history buffers or their absence), and the render-scale decision
  means world targets may size from render scale rather than swap-chain size.
  Do not freeze the declarations before T0111's answers exist.

### Note (2026-08-03) -- T0120 owns a separate pool, not this ticket's targets

**T0120 (camera render-to-texture)** gives *additional*, independently
positioned cameras their own texture targets -- a portal, a mirror, a
security-camera monitor. Those are a distinct pool from what this ticket
owns: this ticket is the *single* main frame's own colour/depth/ping-pong
set (46.4's shared depth, 46.5's ping-pong pair, the amendment above's
mid-frame readback), sized to one viewport or window. T0120's targets are
per-camera, sized and updated independently, and are T0094.1 `RenderTexture`
instances rather than frame targets. The two should share naming/lookup
conventions (46.2) so GPU memory reporting (46.6, and T0120's own reporting)
stays legible in one place, but they are not the same resource pool and
should not be merged into one to save a ticket.
