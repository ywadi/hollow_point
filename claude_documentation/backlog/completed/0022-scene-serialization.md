# T0022 — Scene serialization

| | |
|---|---|
| **Status** | ✅ DONE |
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

- [x] Save produces a readable, git-diffable `.hpscene` YAML — and *stably*
      diffable: saving a loaded scene reproduces the document byte for byte,
      which it did not until entity order was fixed (see the findings)
- [x] Load reconstructs an equivalent scene — same entities, GUIDs, components
- [x] Binary cook and load produce an identical scene to the YAML path
- [x] Unknown components in a file are handled deliberately — **preserved as
      text, written back verbatim, and materialised when the type reappears**,
      which is D23's policy rather than a subset of it
- [x] Round-trip tests over a scene using every component type — driven by
      walking the registry, so a type added later is covered without anyone
      remembering this test

## Subtasks

- [x] 22.1 `.hpscene` schema — entities as a list, each with GUID, name, parent
      and components; `version` from the first file ever written
- [x] 22.2 Per-component serialize/deserialize **driven by reflection (T0053)** —
      `get`/`set` added to the existing clone registry, not a second table
- [x] 22.3 Save: iterate entities and their components
- [x] 22.4 Load: recreate entities preserving GUIDs, then components, then
      resolve parents by GUID in a second pass
- [x] 22.5 Wire binary cook/load through T0020 — `cookScene` /
      `loadSceneFromCooked`, inside `hp/Cook.hpp`'s container
- [x] 22.6 Round-trip tests, including an empty scene

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


## Built 2026-08-05 — the YAML round trip, not the binary path

`engine/include/hp/SceneSerialize.hpp`, `engine/src/SceneSerialize.cpp`,
`tests/fast/scene_serialize_test.cpp`. **248 fast and 89 integration green on
both targets**, ten new cases.

### The registry grew two function pointers rather than a twin

`ComponentClone` became `detail::ComponentOps`, carrying `get`, `set` and a
`serialized` flag alongside the existing `copy`, all populated by the same
`registerComponent<T>` call. The design section above called for exactly this and
warned against a parallel table; that warning was worth heeding, because a second
name-keyed registry is two lists to keep in step and a type present in one and
missing from the other fails silently in whichever direction nobody tested.

`detail::registeredComponents()` exposes the table so the serializer walks it
instead of switching on type. Adding a component still touches one place.

### The trap is marked in code, not inferred

`Id`, `Hierarchy`, `WorldTransform` **and `Tag`** are flagged non-serialized
explicitly. `Tag` joined the list because the schema carries the name on the
entity, where a diff reader expects it — the same argument that excludes `Id`.
All four stay reflected; the inspector still wants them.

The flag is set rather than inferred from "has no reflected properties", which is
what these look like today. That inference would start writing garbage the moment
somebody added `.property<&Hierarchy::parent>`, and the assertion that no
`Hierarchy:` key appears in a saved document is in the tests.

### The version field exists from the first file

`kSceneSchemaVersion = 1`, written always, and a **newer** file is refused rather
than partly loaded. That half of T0082 had to happen now: a file with no version
can only be guessed at, and loading what this build understands would write the
loss back on the next save. Migration itself remains T0082's.

### What was not done in that pass — all of it closed 2026-08-05, below

- 22.5, the binary cook path.
- Unknown components were counted and dropped, not preserved.

## Closed 2026-08-05 — the blob, the cook, and a diff bug nobody had noticed

`engine/include/hp/Yaml.hpp`, `engine/src/Yaml.cpp`,
`engine/include/hp/SceneSerialize.hpp`, `engine/src/SceneSerialize.cpp`,
`engine/src/Scene.cpp`, `tests/fast/serialization_test.cpp`,
`tests/fast/scene_serialize_test.cpp`, and a new
[`../../documentation/10-scene-file-format.md`](../../documentation/10-scene-file-format.md).

**263 fast and 89 integration green on both targets** (was 248/89), `zig build
docs` clean.

### The YAML layer gained the primitive that was blocking D23

`YamlNode::emitSubtree()` and `YamlNode::graft()`, a **pair** — the property that
matters is that `parent.graft(child.emitSubtree())` reproduces `child`. rapidyaml
supports both directly (`emitrs_yaml` takes a node id; `Tree::duplicate_children`
grafts across trees), so this was exposing what was already there rather than
building anything.

**The trap, and it is not a compile error:** `Tree::duplicate` copies *spans*,
not characters — `_copy_props` assigns `m_key` and `m_val` straight across — so a
grafted node points into the buffer **and the arena** of the tree it was parsed
from. The document therefore owns every grafted fragment's text *and* its
temporary tree, for the document's lifetime. That is the same invariant the
header already stated about its own text; a graft would have broken it silently,
and the symptom would have been garbage in an emitted document arbitrarily far
from the call that caused it. There is a test that mutates and frees the source
string after grafting.

Held indirectly (`unique_ptr`) because the addresses must not move: `std::string`
has a small-buffer optimisation, so moving one relocates short text — which is
exactly the size a component fragment is.

### Unknown components are preserved, and it is a component

`UnknownComponents` holds `{type, raw YAML}` per preserved subtree and lives on
the entity, because the round trip that matters is *load → edit → save* and the
blob has to still be there at save time. Registered in `registerCoreComponents`
like any other component, so `Scene::clone` carries it for free — a play-mode
clone of a scene loaded while the module was broken must not be the thing that
finally loses the data — and marked non-serialized, because `saveSceneToString`
writes its contents back under their *own* names rather than as a component
literally called `UnknownComponents`.

`materialiseUnknownComponents(Scene&)` is the other half: it turns preserved text
into real components for types that now exist, and is what a module host calls
after a reload. An item whose stored form will not read is **kept as text**
rather than discarded — the type came back but its shape changed, and dropping
the text then would destroy the only record of what the file said.

**A registered-but-non-serialized type is not "unknown".** `Hierarchy`, `Id`,
`WorldTransform` and `Tag` appearing under `components` in a hand-edited file are
ignored with a warning, not preserved — preserving them would write them back and
resurrect exactly the corrupt key the schema exists to keep out.

### 22.5 was wiring, as predicted

`cookScene` / `loadSceneFromCooked`, inside `hp/Cook.hpp`'s container, with the
payload layout documented on the declaration. Each component record carries its
type name and a **byte length**, so a build whose type set has moved on can name
every type it could not read instead of losing sync at the first.

**One asymmetry, made explicit rather than hidden:** the YAML path preserves a
component it cannot interpret; the binary path cannot, because cooked bytes for
an unnameable type are not something a save could write back. So a cook naming an
absent type returns `Stale` — "re-cook from the YAML" — instead of quietly
loading a scene with less in it than the document has. Every other cook failure
collapses to the same answer, which is why there is one status and not six.

### The bug this ticket did not set out to find

**Every save-after-load produced a whole-file diff containing no change.** entt
walks a storage's packed array from the back, so `view<Id>` yields *reverse*
creation order — while a load creates in *file* order. The two compose into an
order that flips on every round trip.

It surfaced only because the binary-vs-YAML comparison compared whole documents;
none of the existing round-trip tests looked at order, and a human would have met
it as "why does my commit touch every line". Fixed with
`entitiesInCreationOrder`, used by both save and cook, and pinned by a test that
round-trips **twice** — because an order that flips is stable every *other* time,
so one round trip would have passed while the file still churned on every save.

That is the "readable, git-diffable" Done-when actually being met rather than
assumed, and it is a good argument for comparing whole outputs rather than
fields.

### What is still not done, deliberately

- **No `.hpscene` file is written to or read from disk.** The API is
  string-in/string-out; the virtual path and the file extension belong with
  T0024's project layout. The document is ready for it.
- **`materialiseUnknownComponents` is not called by anything yet.** Nothing loads
  a gameplay module and then a scene in the same process today; T0062 owns
  calling it, and now has the reference.
- **Migration between schema versions** remains T0082's. This ticket built only
  the half that cannot be added retroactively: stamp the version, refuse a newer
  one.
