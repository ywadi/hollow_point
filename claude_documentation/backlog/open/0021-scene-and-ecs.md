# T0021 — Scene and entity-component system

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Phase** | 3 — Data model |
| **Created** | 2026-08-02 |

## Why

A Scene is the container of everything the game and editor manipulate, and it is
the simplest self-contained concept in the data model — so it comes before assets
and projects, both of which reference it.

EnTT 3.16.0 is already vendored (D7), so the registry itself is free; the work is
the component set, the entity handle, and the scene's ownership semantics.

## Done when

- [ ] `Scene` owns an entt registry and creates/destroys entities
- [ ] Every entity automatically gets an ID (GUID, T0016) and tag component
- [ ] A lightweight `Entity` handle for add/get/has/remove of components
- [ ] Core components: transform, mesh, material, camera
- [ ] Scene copy works — Phase 6 play mode needs to clone and discard
- [ ] Tests for entity lifetime, component add/remove, GUID stability

## Subtasks

- [ ] 21.1 `Scene` wrapping `entt::registry`
- [ ] 21.2 `Entity` handle — a registry pointer plus an `entt::entity`, cheap to
      copy, never owning
- [ ] 21.3 Automatic ID + tag on creation
- [ ] 21.4 Transform component; decide parenting now or explicitly defer it
- [ ] 21.5 Mesh, material and camera components referencing assets by GUID
- [ ] 21.6 `Scene::Copy` for play mode
- [ ] 21.7 GUID → entity lookup, since serialization and selection both need it

## Notes / findings

**Components reference assets by GUID, never by pointer.** Pointers do not
serialize and do not survive an asset reload. This is the single most important
rule in the data model.

**Decide about transform hierarchy explicitly.** Parenting is far cheaper to
design in now than to retrofit — it changes the transform component, the update
order, and the hierarchy panel. If we defer it, record that as a decision with
its cost, rather than just not doing it.

Do not expose `entt::` types in public engine headers; keep the dependency
swappable and keep compile times down. The `Entity` handle is the seam.
