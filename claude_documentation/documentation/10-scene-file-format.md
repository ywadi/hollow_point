# The scene file format (`.hpscene`)

A scene is a YAML document. It is meant to be **read, hand-edited and diffed by a
person** — that is not a side effect of choosing YAML, it is the requirement the
schema was designed against, and several decisions below cost something to keep
it true.

This document describes the format itself. For the API that reads and writes it,
see [`docs/api/SceneSerialize.md`](../../docs/api/SceneSerialize.md); for why
serialization is generated from reflection rather than written per type, see
**D23** in [`02-decision-log.md`](02-decision-log.md).

---

## A complete example

Everything the format can express, in one file:

```yaml
version: 1
entities:
  - guid: 00000000000000a1
    name: Sun
    components:
      Transform:
        position: [0, 10, 0]
        rotation: [0, 0, 0, 1]
        scale: [1, 1, 1]
      Light:
        type: Directional
        colour: [1, 0.95, 0.9]
        intensity: 3
        enabled: true

  - guid: 00000000000000b1
    name: Player
    components:
      Transform:
        position: [0, 0, 0]
      MeshRenderer:
        mesh: 4f8a12c0d3e5b678
        material: 91aa2b3c4d5e6f70
        layers: 1
      PlayerController:          # a gameplay type; see "Unknown components"
        speed: 4.5

  - guid: 00000000000000b2
    name: Weapon
    parent: 00000000000000b1     # a child of Player
    components:
      Transform:
        position: [0.3, 1.2, 0]
```

## The three top-level rules

**`version` is present in every file the engine has ever written**, from the
first one. A file without a version can only be guessed at, never migrated, and
it cannot be added retroactively to files already in the wild. A file whose
version is *newer* than this build is **refused**, not partially loaded — see
"Versioning" below.

**`entities` is a flat list.** Nesting a child under its parent would put the
hierarchy in two places (the nesting and the `parent` field) and let them
disagree. A flat list with GUID links has exactly one truth, and it makes a diff
of a reparented object one changed line instead of a moved block.

**Entity order is creation order and is stable.** Saving a loaded scene
reproduces the document byte for byte. This matters more than it sounds: entt
iterates its storage backwards, so the obvious implementation emitted the
entities in reverse on every save, meaning *opening a scene and saving it
produced a whole-file diff containing no change at all*. It is tested.

## The entity

| Key | Required | Meaning |
|---|---|---|
| `guid` | no | Stable identity, 16 hex digits. Generated when absent; **refused when present and malformed** — see "Authoring mode" |
| `name` | no | The editor label. Defaults to `Entity` |
| `parent` | no | The parent's **GUID**, or its `name`. Absent means a root |
| `components` | no | Map of type name to that component's fields |

### `guid` is yours to choose, and is never regenerated

Whatever the file says **is** the entity's identity for the rest of its life.
The loader uses `Scene::createWithGuid`, so a hand-written `00000000000000a1` is
as valid as a generated one — and readable GUIDs make a hand-authored scene far
easier to wire together.

Regenerating GUIDs on load would break every reference into the scene from
outside and corrupt a project subtly enough that there is an explicit test
against it.

Two rules:

- a `guid` that is **present** must parse — see the asymmetry below, which is
  what makes omitting it safe;
- it must be **unique within the scene** — a duplicate is refused with a log
  rather than making a GUID lookup ambiguous.

### `name` and `parent` sit on the entity, not among its components

This is a schema decision, not a convenience, and it is the single most
important thing in the format to understand.

`Tag` (the name) and `Hierarchy` (the parent link) are both real, registered
component types. A generic loop over registered components would happily write
them — and **writing `Hierarchy` produces a corrupt file that looks completely
fine**, because its fields are `entt::entity` handles: registry slot indices,
reused after a destroy. Reloaded, they silently address *different* entities.

So the link is written as a **GUID and resolved in a second pass**. The second
pass is also what makes forward references work: a parent may appear anywhere in
the file, including after its own child.

Four registered components are therefore excluded from `components` by name:

| Component | Why it is excluded |
|---|---|
| `Hierarchy` | Runtime handles; see above. The `parent` key replaces it |
| `WorldTransform` | Derived from `Transform` by propagation — storing it lets two truths disagree |
| `Id` | Duplicates the entity's own `guid` key |
| `Tag` | The `name` key carries it, where a diff reader expects it |

All four stay *reflected* — the inspector still shows them — and they are marked
non-serialized **explicitly**, rather than being inferred from having no
reflected properties today. That inference would start writing garbage the
moment somebody added a property to `Hierarchy`.

A file that names one of them under `components` anyway is **ignored with a
warning**, not preserved: writing it back would resurrect exactly the corrupt key
the schema exists to keep out.

## Authoring mode: writing a scene from nothing

A file the engine wrote and a file a person (or a model) typed have different
needs. The format serves both by being **strict about the one thing that cannot
be recovered** — an identity recorded wrongly — and **lenient about the one that
was never recorded at all**.

This is a complete, valid scene:

```yaml
version: 1
entities:
  - name: Sun
    components:
      Light: { type: Directional, intensity: 3 }
  - name: Player
    components:
      Transform: { position: [0, 0, 0] }
  - name: Weapon
    parent: Player
    components:
      Transform: { position: [0.3, 1.2, 0] }
```

Two leniencies make that work, and both are narrow on purpose.

**An entity with no `guid` receives a fresh one.** Nothing could have referenced
an identity that was never written down, so issuing one breaks nothing.

**A `guid` that is present and malformed is still refused**, and the entity is
skipped with a warning. That is not an inconsistency — it is what makes the
first rule safe. Absent means *"I did not record an identity"*; present-and-wrong
means *"I did, and mistyped it"*, and generating there would turn a typo into a
new entity while silently orphaning every `parent` that named the value the
author meant.

**`parent` may name an entity** when the value is not 16 hex digits, so a
hierarchy can be authored without inventing identities. Deliberately narrow:

- **a GUID always wins**, so a file the engine wrote never takes this path —
  which keeps `Tag`'s documented "nothing looks entities up by it" true
  everywhere but this one key;
- **an ambiguous name is refused**, not resolved to the first match. Names are
  not unique, and silently picking one would make the hierarchy depend on file
  order — a bug that is impossible to reason about from the symptom. The child
  is left a root and the name is logged;
- it applies to `parent` and **nothing else**. References between *components*
  are T0071's and will decide their own representation.

**Save always writes GUIDs.** A hand-authored file is normalised the first time
it is saved — full identities everywhere, `parent` by GUID — so name references
exist at the edge of the system and never inside it, and the normalised document
is byte-stable from then on.

## Components, and how a type gets into the file

**Nothing in the serializer knows what a component is.** Save walks the component
registry and asks reflection for each type's properties; load does the reverse.
Adding a component to the format is one declaration and no change to any file:

```cpp
registerComponent<Health>("Health")
    .property<&Health::current>("current")
    .property<&Health::max>("max");
```

That is the whole change. It applies identically to types a gameplay module
registers — **there is no separate path for gameplay types**, which is what D23
settled.

**The type name is the identity**, always. `entt::type_index` differs across the
module boundary — measured, T0095 — and must never reach a file. Renaming the
string in `registerComponent` breaks every file that used the old one; renaming
the C++ type does not.

### Field shapes

Chosen for a person reading a diff:

```yaml
position: [1, 2, 3]        # float3, float4 and Quaternion are short sequences
rotation: [0, 0, 0, 1]
mesh: 4f8a12c0d3e5b678     # Guid, as its canonical text
name: Player               # std::string
type: Directional          # enums by NAME, never by number
enabled: true
layers: 1                  # LayerMask, as its bits
```

Enums by name is deliberate: `type: Spot` survives someone inserting a value
into the middle of the enum, and `type: 2` does not.

### Reading is lenient; writing is exact

The asymmetry is intentional, because files outlive the code that wrote them:

- **A field absent from the document keeps the type's default.** A component can
  gain a property without invalidating every file written before it.
- **A field the type does not have is ignored.** A component can lose one
  without a migration.
- **A malformed field leaves its target alone** and is reported, rather than
  being reinterpreted.

So deleting a line from a scene file is safe, and the practical consequence is
that a minimal hand-written entity is three lines.

## Unknown components — data this build cannot interpret

A component type can be legitimately **absent**: its gameplay module failed to
build, or it is mid-rename. This is guaranteed to happen, and it happens exactly
when a developer is in the middle of changing something.

The engine **preserves it as text**. The raw YAML subtree is kept on the entity,
written back verbatim by the next save, and turned into a real component once the
type exists (`materialiseUnknownComponents`, which the module host calls after a
reload). It survives a scene clone, so a play-mode copy is not what finally loses
it.

The alternative — dropping it with a warning — destroys the file on the next
save, and from the file layer's side it *looks like it worked*. Hard-failing
instead makes the editor unusable precisely when the developer needs it. This is
where Unity and Godot both landed, and D23 records the same conclusion.

The subtree is kept as **text**, not as a parsed structure, because the moment it
is decomposed into something this build understands it is no longer what the file
said, and writing that back is a lossy guess rather than a round trip.

## Versioning

`version` is the **schema** version — the shape of the document, not the engine's
own version.

Bump it when the shape changes: a field renamed, a component split, a meaning
revised. **Adding a property to a component does not need a bump**, because
reading is lenient and an older file simply leaves the new field at its default.

A file from a **newer** schema is refused outright (`SceneLoadStatus::NewerSchema`)
rather than partially loaded. Loading the fields this build understands and
dropping the rest would write the loss back on the next save, which destroys work
in a way nobody notices until they reopen the project in the newer build.

Migration between versions is **T0082's**, and is deliberately not built yet.
What exists now is the half that cannot be added later: stamping the version, and
refusing a future one.

## The binary form

**YAML is the truth; the cook is a cache.** `cookScene` writes a binary form
inside the container described in [`docs/api/Cook.md`](../../docs/api/Cook.md),
and `loadSceneFromCooked` reads it. Only the binary ships (Phase 8 export).

Every way that read can fail — wrong container version, wrong schema, changed
source, truncated, or a component type this build no longer has — collapses to
one answer: `SceneLoadStatus::Stale`, meaning *load the YAML instead and cook it
again*. A caller never distinguishes them, which is why there is one status
rather than six.

**Staleness is decided by content hash, never by mtime.** Timestamps lie after a
git checkout, a file copy, a clock change or a CI cache restore.

One asymmetry is worth knowing: the YAML path *preserves* a component it cannot
interpret, and the binary path cannot — cooked bytes for a type nobody can name
are not something a save could write back. So a cook naming an absent type
declares itself stale and sends the caller to the text beside it, rather than
quietly loading a scene with less in it than the document has.

## What is not here yet

- **No `.hpscene` file is read from or written to disk by this layer.** The API
  is string-in, string-out; the virtual path and the file extension belong to
  T0024's project layout. Everything above is about the document, and the
  document is ready for it.
- **Migration between schema versions** — T0082.
- **Entity references between entities** (a component field pointing at another
  entity) — T0071. Today a component references *assets* by GUID; a reference to
  another entity has no field type yet.
