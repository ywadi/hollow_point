# T0022 — Scene serialization

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 3 — Data model |
| **Order** | 250 |
| **Created** | 2026-08-02 |
| **Refs** | T0053 (Blocks this), T0101, T0062, [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D23, [../../documentation/09-gameplay-authoring.md](../../documentation/09-gameplay-authoring.md) |

## Why

A scene that cannot be saved and reloaded is a demo. This is also the first real
consumer of the T0020 util and will expose whatever is wrong with it while the
schema is still small.

## Done when

- [ ] Save produces a readable, git-diffable `.hpscene` YAML
- [ ] Load reconstructs an equivalent scene — same entities, GUIDs, components
- [ ] Binary cook and load produce an identical scene to the YAML path
- [ ] Unknown components in a file are handled deliberately (see notes)
- [ ] Round-trip tests over a scene using every component type

## Subtasks

- [ ] 22.1 `.hpscene` schema — entities as a list, each with GUID, tag, components
- [ ] 22.2 Per-component serialize/deserialize **driven by reflection (T0053)**
      — registration falls out of the type's one reflection declaration, so
      adding a component still touches exactly one place
- [ ] 22.3 Save: iterate entities and their components
- [ ] 22.4 Load: recreate entities preserving GUIDs, then components
- [ ] 22.5 Wire binary cook/load through T0020
- [ ] 22.6 Round-trip tests, including an empty scene and a large one

## Design worked out 2026-08-05, before writing any code

Recorded rather than re-derived. Three of the six subtasks turn out to be wiring
existing pieces; the fourth has a trap that a naive implementation walks into.

### 22.2 is already answered — extend the registry that exists

`Scene.cpp` already keeps a **component registry keyed by stable name**, built by
`registerComponent<T>` and held in `componentClones()`. It carries one function
pointer today (`copy`, for scene cloning) and it is exactly the right shape for
this ticket: name-keyed, populated as a side effect of the one reflection
declaration, and **replacing on re-registration** — which is what a gameplay
module reload needs, and which is already handled with the reason written down.

So 22.2 is *"add two function pointers"*, not *"design a mechanism"*:

- `bool get(const entt::registry&, entt::entity, entt::meta_any& out)`
- `bool set(entt::registry&, entt::entity, const entt::meta_any&)`

**Do not build a second registry.** This ticket's own note warns that a central
`if/else` over component types rots fastest; a parallel table is the same
mistake wearing a different hat. The name is the identity — `entt::type_index`
differs across the module boundary (measured, T0095) and must never reach a file.

### The trap: three registered components must NOT be serialized generically

Eight components are registered today: `Id`, `Tag`, `Transform`, `Hierarchy`,
`WorldTransform`, `MeshRenderer`, `Camera`, `Light`. **A loop that writes all of
them produces a corrupt file**, and it will look fine:

| Component | Why it cannot go through the generic path |
|---|---|
| **`Hierarchy`** | Its fields are `entt::entity parent` and `std::vector<entt::entity> children` — **runtime handles**, indices into one registry's slots. Persisted, they are meaningless on load and will silently address *different* entities, because a destroyed entity's slot is reused. Must be written as **GUIDs and fixed up on load**, which is T0101.1's representation decision to consume, not to reinvent |
| **`WorldTransform`** | Derived from `Transform` by propagation (T0101). Writing it stores the same truth twice and lets the two disagree; a hand-edited file with a stale world matrix would render at the wrong place for one frame |
| **`Id`** | Holds the entity's `Guid`, which the **entity** already carries in the schema (22.1). Writing it as a component too means two places to disagree about identity |

`Id`, `Hierarchy` and `WorldTransform` are all registered **with no reflected
properties**, so today they would silently write an empty map — which is
harmless but also proves nothing, and would start writing garbage the moment
someone "helpfully" adds `.property<&Hierarchy::parent>`. **Mark them
non-serialized explicitly** rather than relying on the absence of properties.

### 22.5 is wiring

`writeProperties` / `readProperties` (YAML) and `cookProperties` /
`readCookedProperties` (binary) all exist and are tested, with the leaf types
hand-written in `Serialize.cpp`. The binary path needs no new machinery.

**`adoptMetaContext()` must be called before any of it** in a consumer that is
not the engine — the registry looks empty otherwise and `writeProperties`
returns false **with no diagnostic at all**, which reads exactly like a broken
serializer. That cost twenty minutes on T0079.

### 22.6 should turn today's luck into a test

Two reflection gaps were found by accident on 2026-08-05: `Light` was not
registered at all, and `MeshRenderer::layers` was added as a field and never
reflected — so a scene would have reloaded with every object back on the default
layer, visible to cameras explicitly told to exclude it. Both were caught by
writing an unrelated round-trip test.

**The test this ticket owes is the systematic version**: walk the component
registry, and for every registered type assert a round trip. That converts "did
someone remember" into "the suite fails". It is also the only thing that will
catch the next component added without a `.property<>` line.

### 22.4's GUID preservation is supported

`Scene::createWithGuid(guid, name)` already exists, and `Scene::find(guid)`
resolves one — which is also the hierarchy fix-up primitive. So load is:
create every entity with its GUID first, then attach components, then resolve
parent links by GUID. Two passes, and the second is what makes forward
references work.

### Unknown components — the blob, and where it has to live

The policy is decided (see the second review pass below): **preserve the raw
subtree**. Note it has to survive on a `Scene`, not in the file layer, because
the round trip is *load → edit → save* and the blob must still be there at save
time. That means a component-shaped store of `name -> raw YAML` on entities that
had unknown types, which the save path re-emits verbatim. Worth designing before
22.3 rather than bolted on after.

## Notes / findings

**Preserve GUIDs on load.** Regenerating them breaks every reference into the
scene from outside and silently corrupts the project — a subtle enough failure
that it is worth an explicit test.

**Decide the unknown-component policy now:** skip-and-warn, or hard-fail. Skipping
is friendlier during development but silently discards data on save-after-load,
which destroys a file. Failing loudly is safer. Whichever we pick, write it down.

Component registration should be data-driven enough that adding a component type
touches one place. A central `if/else` chain over component types is the thing
that rots fastest in an engine of this shape.

### Architecture review (2026-08-03)

This ticket predates T0053 (reflection) and was written as if each component
hand-registers its own serialize functions. T0053 explicitly lists
serialization as a consumer that uses reflection "and nothing else" — so 22.2
has been reworded: per-component serialization is *generated from* the
reflection declaration, and the only hand-written serializers are the leaf
types the reflection layer bottoms out in (see the matching note on T0020).
**T0053 is therefore a hard prerequisite of this ticket**, which its own notes
already claim but this file previously did not acknowledge.

### Second review pass (2026-08-03) — the unknown-component policy has a third case

"Skip-and-warn vs hard-fail" misses the case this engine is guaranteed to hit:
**component/behaviour types that live in the gameplay module** (T0048/T0062).
When the module fails to build, or a type is renamed mid-refactor, its types
are *legitimately absent* at load time — hard-fail makes the editor unusable
exactly when the developer is mid-change, and skip-and-warn destroys the data
on the next save. The answer both Unity and Godot converged on: **preserve
unknown component data as an opaque blob** (keep the raw YAML subtree),
round-trip it through save, and re-materialise it when the type reappears.
Warn loudly meanwhile. Worth building from the start, because it also covers
files from newer schema versions more gracefully than refusal alone (T0082
still refuses newer *file* versions; this handles a missing *type*).

### Cross-ticket obligations (2026-08-04, T0124 backfill)

- **T0101.1** owns how transform parent/child links are represented on disk
  (GUIDs, fixed up on load) versus at runtime — and T0101's hierarchy
  round-trip test lands through this serializer. Consume its representation
  decision rather than inventing a link format here, or the two fix-up paths
  drift and hierarchies break only on load.


### Inherited from T0053 (closed 2026-08-04)

- **T0053.8 moved here.** Reflection is built and tested; the round-trip
  through serialization is not, because there is no serializer yet. This ticket
  owns proving it: write a component out and read it back through
  `hp::resolveType` and `meta_data::get/set` alone, with no per-type switch.
  What is already guaranteed and can be relied on: nested structs resolve *as
  reflected types* so recursion works, enums carry named values so a file can
  say `team: Hostile` rather than `2`, `std::vector` is reachable as a sequence
  container, `hp::Guid` round-trips exactly, and a wrongly-typed `set` returns
  false instead of reinterpreting bytes.
- **Serialize by name, never by type index.** `entt::type_index` differs per
  module and must never reach a file (T0095, D12).

### From T0062 / D23 (2026-08-05) — behaviours serialize through this, not beside it

T0062 originally specified its own `{ type: "PlayerController", properties: {…} }`
path. D23 deletes it: a behaviour is a reflected component, so **this ticket's
component serialization is the only path**, and there must not be a second one
for gameplay types.

Two requirements that follow:

- **Component types registered by the gameplay module** must round-trip, not
  just engine-owned ones. Identity is the stable **name** — `entt::type_index`
  differs across the module boundary (measured, T0095) and can never be
  persisted.
- The same property enumeration is reused by T0062.6's in-memory hot-reload
  snapshot. That snapshot does **not** need the YAML/file layer, but it should
  not be a second implementation of "walk a reflected type's properties".
