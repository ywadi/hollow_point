# T0100 — Frame lifecycle and system update order

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
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

### 2026-08-04 — scope: the contract is complete, the enforcement is not

Half of this ticket's Done-when names systems that do not exist yet — T0072/T0075
queues, T0048 reload, T0058 asset swap, T0077 transitions, T0101 transforms, and
the ECS itself (T0021, Phase 3). Those conditions are met **at the contract
level** — the phase exists, in the right position, in `Application::run`, with an
owner named — and explicitly *not* at the enforcement level, which lands with the
system that fills the phase.

That split is recorded here rather than papered over, and each of the 15
consuming tickets now carries a back-reference saying what it must honour, so the
contract is discoverable from the side that has to obey it. A one-way reference
would not have been enough: nobody reads the ticket they have already closed.

**Deliverables:**

- `claude_documentation/documentation/08-frame-anatomy.md` — the ordered list
- **D17** in the decision log, recording what was rejected (per-system ad hoc
  placement; defining the anatomy later; a runtime phase-registration API)
- `Application::run` restructured to 13 named phases; unbuilt ones are profiler
  zones with an owning-ticket comment, following the precedent the `present`
  phase already set
- `ILayer`/`Application` gain `onFixedUpdate` and `onLateUpdate`
- `tests/integration/frame_order_test.cpp`
- A rule added to `CLAUDE.md`: tickets that constrain each other must point
  **both** ways

### Could not verify locally — see T0122

`zig build test -Dtest=integration -Dtarget=linux` fails on this WSL tree before
compiling anything:

```
error: failed to rename compilation results ('.zig-cache/tmp/17b026b44f6c6824')
       into local cache ('.zig-cache/o/213c45259d0009b8481e686cd149efc4'):
       AccessDenied
```

`.zig-cache/` and `build/` have no host discriminator, and this tree was last
built from Windows (`CMAKE_C_COMPILER=C:/Development/.../zig-cc.cmd`). It is the
same collision T0102 fixed for `.harness/` and did not extend to the caches.
Filed as **T0122**.

Building anyway would have forced a reconfigure that destroyed the Windows-host
build tree, so it was not done. **Verification for this ticket is the CI run**,
recorded below. That is a real gap in the loop, not a preference: a WSL-side
developer currently has no local test loop.

### Finding: the fixed-step accumulator was dead code

`Clock::consumeFixedStep()` and `Clock::interpolationAlpha()` have existed since
T0057 and **nothing has ever called them**. `Clock::advance()` adds to
`accumulator_` every frame and nothing drained it, so the accumulator grew
without bound for the lifetime of the process; `interpolationAlpha()` would have
returned a steadily increasing number rather than a value in [0, 1).

Invisible until now only because the one function that would expose it was also
uncalled. Wiring phase 3 drains it properly. Worth recording as the general
shape of the problem this ticket exists to prevent: a correct mechanism, built
early, that nothing was contractually obliged to use.

### Note (2026-08-03) -- the anatomy must name the present/pacing step

The design-gap survey (item 1) observed that this ticket owns "the single
ordered list of what runs when" and never mentions presentation or pacing --
the natural owner, blind to it. Fixed by division of labour: the frame-anatomy
document (100.1) must include the present step and the pacing/cap point as
named phases of the frame; the *policy* behind them (present mode, vsync,
frame-rate cap, focus loss) is owned by **T0110**. Neither document should
duplicate the other's half.
