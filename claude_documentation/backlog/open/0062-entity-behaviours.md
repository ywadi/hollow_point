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
- [ ] 62.2 `BehaviourComponent` holding the instance; a system driving them
- [ ] 62.3 **Type registry populated by the gameplay module** — see notes
- [ ] 62.4 Registration macro binding name, factory and reflected properties (T0053)
- [ ] 62.5 Serialize as `{ type: "PlayerController", properties: {...} }`
- [ ] 62.6 **Hot-reload cycle**: serialize state → unload → load → recreate →
      deserialize
- [ ] 62.7 Deterministic update order, with an explicit priority
- [ ] 62.8 Entity/component access helpers on the base class
- [ ] 62.9 Pool-allocate instances per type (`FixedBlockMemoryAllocator`)
- [ ] 62.10 Enable/disable without destroying, and the matching callbacks
- [ ] 62.11 Editor: "Add Behaviour" dropdown listing registered types

## Notes / findings

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
