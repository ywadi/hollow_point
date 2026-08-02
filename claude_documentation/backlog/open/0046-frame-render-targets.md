# T0046 — Frame render target management

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Simple |
| **Phase** | 4 — Render layer |
| **Created** | 2026-08-02 |

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
