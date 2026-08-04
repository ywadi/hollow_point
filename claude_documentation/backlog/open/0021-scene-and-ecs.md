# T0021 — Scene and entity-component system

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 3 — Data model |
| **Order** | 200 |
| **Created** | 2026-08-02 |
| **Refs** | T0053, T0100, [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md) |

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
- [ ] 21.4 Transform component **with parenting** — required, not optional (notes)
- [ ] 21.5 Mesh, material and camera components referencing assets by GUID
- [ ] 21.6 `Scene::Copy` for play mode
- [ ] 21.7 GUID → entity lookup, since serialization and selection both need it

## Notes / findings


### Frame anatomy — phase 5 — structural apply (T0100, D17)

Entity creation and destruction queued during update apply at **phase 5
(structural apply)**, not at the end-of-frame safe point. Deferring them to end-
of-frame would let a destroyed entity be transformed at 7, read by a follower at
8 and drawn at 10 — one last frame of a thing that no longer exists.

The full order is in [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md); the decision and what it rejected is **D17** in the
decision log. If this ticket needs a phase that does not exist, that is a change
to T0100's document and to D17 — not a new call bolted into `Application::run`.

**Components reference assets by GUID, never by pointer.** Pointers do not
serialize and do not survive an asset reload. This is the single most important
rule in the data model.

**Transform hierarchy is required, not optional.** Skeletal animation is core to
this engine (T0041), and skinned characters need parenting for attachments —
weapons in hands, props on sockets, cameras on rigs. Retrofitting parenting
changes the transform component, the update order and the hierarchy panel, so it
goes in from the start.

Bone transforms are deliberately *not* entities. A skeleton with 80 bones per
character would swamp the registry; ozz keeps its own compact SoA pose buffers
and the renderer consumes those directly. Entity parenting is for attachment
points, not for the skeleton itself — conflating the two is a common and
expensive mistake.

Do not expose `entt::` types in public engine headers; keep the dependency
swappable and keep compile times down. The `Entity` handle is the seam.

### Architecture review (2026-08-03) — `Scene::Copy` is two operations, not one

Two different copies exist with **opposite GUID semantics**, and 21.6 currently
names only one of them:

- **Play-mode clone** (T0037): GUIDs are *preserved*, so EntityRefs (T0071),
  signal connections (T0072) and save data keep working inside the clone.
  Consequence: GUID→entity lookup must be per-scene, never global, because two
  live scenes now contain the same GUIDs.
- **Duplicate / subtree copy** (editor duplicate, prefab instantiate): GUIDs
  are *regenerated* and internal references *remapped* (T0071's old→new map).

Make both explicit in the API — one function with a mode is fine; one function
with an ambiguous behaviour is how an instance ends up silently sharing state
with its template.

Also: once behaviours exist (T0062), cloning a scene must clone behaviour
*instances* — heap objects with vtables — not just component data. The
reflection serialize/recreate cycle T0062 builds for hot reload is the same
mechanism; pointer-copying instances between scenes is never correct. Worth
knowing before 21.6 is designed, even though behaviours land later.


### Inherited from T0053 (closed 2026-08-04)

- **T0053.7 moved here.** Reflection landed without ECS integration because
  there was no registry to hook into. This ticket owns it: when a component type
  is registered with the registry it must also be registered for reflection, in
  **one** place — `hp::reflect<T>("Name")` alongside the component's declaration,
  not a second list that drifts. The documented entt idiom is a meta func
  wrapping `registry::emplace_or_replace<T>`, roughly thirty lines of glue.
  Identity is the **name**, never `entt::type_index`, which T0095 measured to be
  a per-module number with no meaning across the module boundary.
