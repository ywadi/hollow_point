# T0062 — Entity behaviours: attaching C++ logic to entities

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Very Complex |
| **Phase** | 2 — Engine skeleton |
| **Created** | 2026-08-03 |

## Why

The core gameplay-authoring question: how does C++ code get attached to an entity
and executed for it, in the way Godot attaches a script to a node?

Two paradigms are in tension. Godot is OOP-per-object — one script instance per
node with `_process(delta)`. ECS is data-oriented — systems iterating component
arrays. Naively mixing them gives scattered allocations *and* awkward systems.

**Resolution: support both, deliberately.**

- **Systems** for many-of things — physics sync, animation, culling, particles.
  Cache-friendly, parallelisable (T0050).
- **Behaviours** for bespoke per-entity logic — the player, a boss, one door.
  Godot's ergonomics, where entity counts are low and logic is unique.

## Done when

- [ ] A `Behaviour` base with `OnCreate` / `OnUpdate` / `OnFixedUpdate` / `OnDestroy`
- [ ] A behaviour is attachable to an entity from the editor inspector
- [ ] Reflected properties on a behaviour are editable in the inspector
- [ ] Behaviour type and property values serialize with the scene, **by name**
- [ ] **State survives a gameplay module hot reload** (T0048)
- [ ] Update order is deterministic and controllable
- [ ] `OnFixedUpdate` runs at the physics rate (T0057), `OnUpdate` per frame
- [ ] Behaviours can find their entity, its components, and other entities
- [ ] Guidance written down on when to use a behaviour vs a system

## Subtasks

- [ ] 62.1 `Behaviour` base class and lifecycle
- [ ] 62.2 **Multiple behaviours per entity**, driven by one system — see notes
- [ ] 62.3 **Type registry populated by the gameplay module** — see notes
- [ ] 62.4 Registration macro binding name, factory and reflected properties (T0053)
- [ ] 62.5 Serialize as `{ type: "PlayerController", properties: {...} }`
- [ ] 62.6 **Hot-reload cycle**: serialize state → unload → load → recreate →
      deserialize
- [ ] 62.7 Deterministic update order, with an explicit priority
- [ ] 62.8 Entity/component access helpers, plus `GetBehaviour<T>()` so
      behaviours on the same entity can find each other
- [ ] 62.9 Pool-allocate instances per type (`FixedBlockMemoryAllocator`)
- [ ] 62.10 Enable/disable without destroying, and the matching callbacks
- [ ] 62.11 Editor: "Add Behaviour" dropdown listing registered types

## Notes / findings

**Many behaviours per entity, not one.** Two models exist: Godot allows one
script per node and composes via child nodes; Unity allows many components per
object. **Unity's is the right fit here.** Composing a `StateMachine` and a
`PlayerController` onto the player should not require inventing a child entity
whose only purpose is to hold logic — that pollutes the hierarchy with things
that are not scene objects.

Consequence: behaviours need `GetBehaviour<T>()` to find siblings, and update
order within an entity must be defined (declaration order, or explicit priority).

**Not everything reusable should be a behaviour.** The test is whether it needs to
exist *in the scene* — selectable, positioned, saved, visible in the hierarchy. A
state machine is logic, so it is best as a plain C++ class owned by a behaviour
(see T0073). Promote it to a behaviour only when it needs inspector exposure.


**Hot reload is the constraint that shapes the whole design.** A behaviour
instance allocated inside the gameplay DLL and stored in a component has a vtable
pointer into that module — on reload it dangles, and the memory came from a
module that no longer exists. The cycle in 62.6 is the answer, and it works only
because reflection can serialize arbitrary behaviour state generically.

This makes **T0053 (reflection) a hard prerequisite**, and is the third
independent reason it is the keystone of the architecture — the other two being
scene serialization and the inspector.

**Type registration must be pushed from the module, not pulled by the engine.**
The engine cannot enumerate types inside a shared library it just loaded. The
module exports one entry point — `RegisterTypes(Registry&)` — called on load and
again on every reload. That same registry populates the inspector's dropdown.

**Performance, stated honestly so the guidance is not cargo-culted:** a virtual
call is a few nanoseconds, and ten thousand behaviours updating per frame is
genuinely fine. The real cost is cache misses from scattered allocations, which
pooling by type addresses. The rule: *bespoke logic → behaviour; thousands of
identical things → system.* Do not reach for a system because virtual dispatch
sounds slow; reach for it when the data layout actually matters.

**Do not store raw pointers to other behaviours or entities across frames.**
Entities are destroyed, behaviours are recreated on reload. Store `Entity`
handles (GUID-backed) and resolve on use — the same discipline that makes assets
reloadable (T0058).

`OnFixedUpdate` must be driven by the accumulator in T0057, not by the frame
loop, or gameplay physics interactions become frame-rate dependent.

### Architecture review (2026-08-03)

Two scope clarifications, so this Phase 2 ticket is not blocked on Phase 6:

- **The editor-facing Done-when items cannot close in Phase 2.** "Attachable
  from the editor inspector", reflected properties "editable in the
  inspector", and 62.11's dropdown all require T0035 (Phase 6). The Phase 2
  deliverable is the mechanism — base class, registry, serialization,
  hot-reload cycle, update dispatch — exercised from code and tests; the
  editor surface lands with the inspector. Same pattern as other tickets whose
  acceptance spans phases, but worth stating here because this one is the
  gameplay keystone.
- **Decide edit-mode execution policy now, even if the answer is simple:** do
  behaviours run while *editing* (not playing)? The sane default is no —
  `OnCreate`/`OnUpdate` fire only in play mode / runtime, and anything that
  must run in-editor is an explicit opt-in later (Unity's `ExecuteInEditMode`
  equivalent, if ever). Leaving it undecided means someone's `OnCreate` will
  fire during scene load in the editor and mutate the authored scene.

Linkage prerequisite: the type registry being "pushed from the module" only
works once T0095 (module ABI — one engine state, entt across the boundary) is
settled. T0095 now blocks this ticket.
