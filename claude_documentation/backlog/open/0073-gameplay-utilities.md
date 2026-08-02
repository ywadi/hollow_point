# T0073 — Gameplay utility library

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 2 — Engine skeleton |
| **Created** | 2026-08-03 |

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
