# T0073 — Gameplay utility library

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 3 — Data model |
| **Order** | 280 |
| **Created** | 2026-08-03 |
| **Refs** | T0062, T0075, T0077 |

## Why

Writing gameplay in C++ only stays pleasant if the small, endlessly-repeated
pieces already exist. Otherwise every behaviour re-implements a timer, and every
character re-implements a state machine slightly differently.

The most immediate case: a **general-purpose state machine**, reusable across the
player, enemies and interactables. Note this is distinct from the animation state
machine in T0049 — that one drives clips and blending; this one drives gameplay
logic. They may share an implementation, but they are different use cases.

## Done when

- [ ] A state machine usable from any behaviour, not tied to entities
- [ ] Timers and cooldowns with elapsed/remaining and completion callbacks
- [ ] Interpolation/easing helpers for simple animation of values
- [ ] A blackboard for sharing named values between behaviours on an entity
- [ ] Everything works with both `OnUpdate` and `OnFixedUpdate` (T0057)
- [ ] All unit tested — utilities are exactly what tests are cheap for

## Subtasks

- [ ] 73.1 `StateMachine` — states, enter/update/exit, transitions with conditions
- [ ] 73.2 Optional hierarchical states, or an explicit decision to skip them
- [ ] 73.3 Timers, cooldowns, and one-shot delayed actions
- [ ] 73.4 Easing/tween helpers
- [ ] 73.5 A per-entity blackboard for loose coupling between behaviours
- [ ] 73.6 Spatial queries against the scene (nearest entity, within radius)
- [ ] 73.7 Tests

## Notes / findings

**Keep the state machine a plain C++ type, not a behaviour or a component.** It
is logic, not a scene object, so it should be usable as a member of anything —
a behaviour, a system, or another utility. Wrapping it in a behaviour is a choice
the *user* makes when they want inspector exposure, not something the utility
imposes.

Design it so states can be plain enums *or* classes. Enums are far more pleasant
for the common case; class-based states earn their weight only when states carry
substantial data of their own.

**Blackboards trade explicitness for decoupling**, and overused they turn into a
global bag of untyped state that is impossible to reason about. Worth having,
worth using sparingly, and worth saying so where it is documented.

Spatial queries will eventually want an acceleration structure. Start with a
linear scan — it is fine at small entity counts, and T0045 notes the same
trade-off for culling.

### Architecture review (2026-08-03) — utilities must be reload-serializable

These utilities live *inside behaviours*, and behaviour state crosses the hot
reload boundary via reflection serialize/recreate (T0062). A timer whose state
is plain data (elapsed, duration, running) survives that; a timer holding a
completion **callback** — a `std::function` into the module — cannot be
serialized and dangles on reload. Design rule for everything in this ticket:
*state is plain data and serializes; callbacks are re-bound in `OnCreate`
after every reload, never stored across one.* The state machine has the same
split: current-state-as-enum serializes; transition condition functions are
code, re-registered on load. Say this in the utilities' documentation, because
it is invisible until the first reload eats someone's timer.

### Re-phased 2 → 3 (2026-08-03)

These utilities live inside behaviours (the review note above), and behaviours
are Phase 3 (T0062's re-phasing) — a utility library for gameplay code that
cannot exist yet is speculative in exactly the way this backlog avoids. The
blackboard (73.5) is per-entity and the spatial queries (73.6) run against the
scene, so T0021 is a real prerequisite too. Ordered directly after T0062.

One correction while moving it: the backlog README previously said this ticket
"actually needs T0045 (Phase 4) and T0049 (Phase 7)". Re-reading the ticket,
neither is a dependency — T0045 is cited only as an analogy for the
linear-scan-first trade-off, and T0049 only to *distinguish* its animation
state machine from this gameplay one. The real cross-phase dependencies are
T0021 and T0062, both Phase 3, which is where this now sits.

### Cross-ticket obligations (2026-08-04, T0124 backfill)

- **T0075.4** dispatches radius-addressed messages through 73.6's spatial
  query — a consumer with dispatch-ordering semantics riding on it, not just a
  convenience helper.
- **T0077.1** decides which resident scenes a query sees (its second-pass note
  names 73.6 directly). Do not answer the scope question locally — per-system
  answers are exactly what that decision exists to prevent.
