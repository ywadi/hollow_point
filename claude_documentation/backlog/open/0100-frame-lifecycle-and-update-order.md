# T0100 — Frame lifecycle and system update order

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 2 — Engine skeleton |
| **Order** | 120 |
| **Created** | 2026-08-03 |
| **Refs** | T0014, T0018, T0026, T0048, T0057, T0058, T0062, T0072, T0075, T0077, T0101 |

## Why

Many tickets each define a *piece* of the frame — T0014 the loop, T0057 the
fixed-step accumulator, T0062 behaviour dispatch, T0072/T0075 deferred queues
with "an explicit drain point", T0048 a reload that must happen "between
frames", T0058 GPU uploads marshalled to the main thread, T0077 scene
transitions applied from gameplay code — but **no ticket owns the frame's
anatomy**: the single ordered list of what runs when, and the defined safe
points where structural changes apply.

Left unowned, each system picks its own spot ad hoc and the result is the
classic class of bugs this backlog keeps warning about individually:
one-frame-lag chains, a camera that reads a transform before the thing it
follows has moved (visible as jitter), a scene transition applied mid-iteration,
a module reload while a queue still holds module-typed payloads (the exact
hazard T0075's review note describes), and entities destroyed while a view is
iterating them. Every one of these is a decision that costs a paragraph now and
a debugging week later.

## Done when

- [ ] A frame-anatomy document exists in `claude_documentation/documentation/`
      listing every phase of the frame in order, and `Application::Run` (T0014)
      implements exactly that order
- [ ] The fixed-step block's contents and internal order are defined (input
      snapshot → `OnFixedUpdate` → physics step → post-physics), driven by
      T0057's accumulator
- [ ] A late-update point exists after gameplay update and before rendering, so
      cameras and followers read final transforms — no one-frame lag
- [ ] Deferred-queue drain points (signals T0072, message bus T0075) are fixed,
      documented, and drained-before-reload is asserted
- [ ] Entity create/destroy during iteration has defined semantics — deferred
      via a command buffer, or immediate with stated invariants — and it is
      enforced, not conventional
- [ ] One end-of-frame safe point applies: gameplay module reload (T0048),
      asset reload swap (T0058), and scene transitions (T0077)
- [ ] The order is testable: a test registers callbacks in several phases and
      asserts they fire in the documented order (T0012)

## Subtasks

- [ ] 100.1 Write the frame-anatomy document; wire `Application::Run` to it
- [ ] 100.2 Fixed-step block ordering on T0057's accumulator, and the point
      where the renderer reads interpolated state (alpha)
- [ ] 100.3 Structural-change policy for the ECS: deferred destruction /
      creation command buffer, applied at a named point
- [ ] 100.4 Drain points for T0072/T0075, including the drain-until-empty
      iteration cap T0075 describes
- [ ] 100.5 End-of-frame safe point: module reload, asset swap, scene
      transition — assert queues are drained and no jobs are in flight (T0026)
- [ ] 100.6 Late-update phase (camera follow, audio listener sync)
- [ ] 100.7 Transform propagation point(s) per T0101
- [ ] 100.8 Ordering test via the test harness (T0012)

## Notes / findings

**This is deliberately Phase 2.** T0014 builds the loop; if the anatomy is
defined at the same time, every later system slots into a *named* place instead
of picking one. Retrofitting an order onto systems that each assumed their own
is the expensive version.

The camera-follow example is the cheapest illustration of why late-update
matters: gameplay moves the player in `OnUpdate`; if the camera also runs in
`OnUpdate` with no defined relative order, it reads either this frame's or last
frame's position depending on registration order — visible as intermittent
jitter that profiles as nothing.

The safe point (100.5) is the same point three tickets independently need:
T0048's reload, T0075's in-flight-payload hazard, and T0077's "change scene
from gameplay code" (which must not destroy the scene that is currently being
iterated). One point, one assertion, three bugs prevented.

This ticket produces mostly a document plus wiring — the systems it orders are
built in their own tickets. Keep it that way; the value is the contract, not
code volume.
