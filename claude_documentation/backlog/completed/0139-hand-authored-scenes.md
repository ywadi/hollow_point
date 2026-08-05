# T0139 — Scenes that can be written by hand, or by a model

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | Medium |
| **Complexity** | Simple |
| **Phase** | 3 — Data model |
| **Order** | 255 |
| **Created** | 2026-08-05 |
| **Refs** | [../completed/0022-scene-serialization.md](../completed/0022-scene-serialization.md), [../../documentation/10-scene-file-format.md](../../documentation/10-scene-file-format.md), T0024, T0071 |

## Why

T0022 built a format a person can read and diff. It is not yet a format a person
can comfortably *write*, and the gap is one field: **every entity must carry a
16-hex-digit `guid`**, and an entity without one is skipped with a warning.

That is exactly right for a file the engine wrote — identity must never be
regenerated, because doing so breaks every reference into the scene from outside.
It is the wrong trade for a file a human or a model is typing from nothing, where
inventing 16 hex digits per entity is pure friction and the "reference into the
scene from outside" the strictness protects does not exist yet.

The concrete goal is that this is a valid scene:

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

## Done when

- [x] An entity with no `guid` loads, and receives a fresh one
- [x] An entity with a **malformed** `guid` is still refused — a typo must not
      become a new identity
- [x] `parent` resolves by entity **name** when it is not a GUID, so a hierarchy
      can be authored without inventing identities
- [x] An ambiguous name reference is refused with a message naming it, not
      resolved to whichever entity happened to be first
- [x] Saving a hand-authored scene normalises it: GUIDs everywhere, `parent` by
      GUID. Round-tripping the normalised file is byte-stable
- [x] `10-scene-file-format.md` documents authoring mode as a supported way to
      write a file, not as a leniency someone discovered

## Subtasks

- [x] 139.1 Generate a GUID for an entity that omits one
- [x] 139.2 Resolve `parent` by name as a fallback, with duplicate detection
- [x] 139.3 Tests, including the normalise-on-save round trip
- [x] 139.4 Document it, with a worked example of a file written from nothing

## Design

### Absent and malformed are different, and that is the whole safety argument

**Absent** `guid` means "I did not care about identity" — generating one is the
only sensible reading and nothing can be broken by it, because nothing could have
referenced an identity that was never written down.

**Malformed** `guid` means "I cared about identity and got it wrong". Generating
one there would silently turn a typo into a new entity, and quietly break every
`parent` that referred to the value the author *meant*. It stays refused, and
that refusal is what makes generating in the absent case safe rather than
merely convenient.

### Why `parent` by name, and why it is not a general lookup

`Tag` is explicitly documented as "not required to be unique; nothing looks
entities up by it", and this ticket must not turn that into a lie. So the name
lookup is deliberately narrow:

- it applies **only** to `parent`, and only when the value is not 16 hex digits,
  so a GUID always wins and a file the engine wrote never takes this path;
- an ambiguous name is **refused**, not resolved to the first match. Silently
  picking one would make the scene depend on file order, which is the class of
  bug that is impossible to reason about from the symptom;
- **save always writes GUIDs.** A hand-authored file is normalised the first time
  it is saved, so name references exist at the edge of the system and never
  inside it.

### What this does not do

Entity references *between components* (a field pointing at another entity) are
T0071's, and they will need this same question answered — but for a field, not a
schema key. Do not generalise this into a reference resolver; it is two dozen
lines serving one key, and T0071 should decide its own representation.

## Notes / findings

### Built 2026-08-05 — and the design survived contact unchanged

`engine/src/SceneSerialize.cpp`, `engine/include/hp/SceneSerialize.hpp`,
`tests/fast/scene_serialize_test.cpp`,
[`../../documentation/10-scene-file-format.md`](../../documentation/10-scene-file-format.md).
**268 fast and 89 integration green on both targets**, was 263/89. Five new
cases, the first of which is the ticket's goal file loaded verbatim.

The load path now keeps the parent as **text** rather than as a parsed `Guid`
until the second pass, because deciding whether it is a GUID or a name needs the
whole entity set to exist first — which the two-pass structure already provided,
so this cost nothing.

Duplicate names are detected during pass one by marking the name's entry
`entt::null` on the second sighting, rather than by counting afterwards. Same
result, one pass, and the ambiguity is impossible to forget to check because
resolution has to read the entry anyway.

### Testing note that cost a few minutes twice

`CHECK(x == entt::null)` does not compile: doctest wraps the left operand in an
expression-decomposition type, and comparing *that* with `entt::null_t` is
ambiguous because entt supplies both a member and a free operator. There is now a
`kNoParent` constant at the top of the file with the reason on it. Entity-to-
entity comparison is fine, which is why the pre-existing tests never hit this.

### Not done, deliberately

- **Nothing resolves an entity reference inside a *component*.** That is T0071's,
  and this ticket's name lookup is scoped to the `parent` key so it cannot be
  mistaken for a general mechanism. T0071 should decide its own representation.
- **`name` is still not an index.** The lookup is built per load and discarded;
  `Tag` remains "not required to be unique, nothing looks entities up by it"
  everywhere except this one key at load time.
