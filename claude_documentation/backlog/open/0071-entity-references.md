# T0071 — Entity references

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 3 — Data model |
| **Order** | 260 |
| **Created** | 2026-08-03 |
| **Refs** | [../completed/0021-scene-and-ecs.md](../completed/0021-scene-and-ecs.md), T0062, [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D23, [../../documentation/09-gameplay-authoring.md](../../documentation/09-gameplay-authoring.md) |

## Why

Entities have to be able to refer to each other to work together — a door to its
switch, a spawner to its spawn points, a camera to its follow target. Nothing
currently covers this, and the naive implementations all fail:

| Approach | Fails because |
|---|---|
| Raw pointer | dangles when the target is destroyed |
| `entt::entity` id | not stable across a scene reload |
| Index into a list | breaks as soon as anything is added or removed |

The answer is the same discipline already used for assets: **reference by GUID,
resolve on use.**

## Done when

- [ ] An `EntityRef` type storing a GUID, resolvable against a scene
- [ ] Resolving a destroyed or missing entity returns null, never a dangling handle
- [ ] `EntityRef` serializes and survives save/load with the reference intact
- [ ] It is a reflected property type (T0053), so the inspector shows a pick slot
- [ ] **Prefab instantiation remaps internal references** — see notes
- [ ] Copying a subtree remaps internal references the same way
- [ ] Tests: destroy the target, reload the scene, instantiate a prefab twice

## Subtasks

- [ ] 71.1 `EntityRef` — GUID storage with `Resolve(Scene&)`
- [ ] 71.2 Null/invalid representation, and a cheap validity check
- [ ] 71.3 Reflection integration so the inspector renders a pick/drag slot
- [ ] 71.4 Serialization as a GUID (T0022)
- [ ] 71.5 **Reference remapping on prefab instantiate and on subtree copy**
- [ ] 71.6 Editor: drag an entity from the hierarchy onto a reference field
- [ ] 71.7 Optionally warn on save about references pointing outside the scene
- [ ] 71.8 Tests

## Notes / findings

**Remapping is the subtle part, and it is where this goes wrong quietly.** If a
prefab contains entity A referencing entity B, instantiating must produce
A′ → B′ — not A′ → B pointing back at the template. Get it wrong and two
instances silently share state through the original, which presents as
inexplicable coupling rather than as an obvious bug.

The remap is: build a map of old GUID → new GUID while cloning, then fix up every
`EntityRef` in the clone. That only works if references are **discoverable via
reflection** (T0053) — otherwise every component with a reference needs a
hand-written remap function, which someone will forget.

**Resolve on use, do not cache.** Caching the resolved handle reintroduces exactly
the dangling problem GUIDs solve. If resolution ever shows up in a profile, cache
it for the duration of a frame and no longer.

Cross-scene references should be disallowed rather than half-supported — decide
that explicitly.

### Cross-ticket obligation — T0021 (2026-08-04)

**`Scene::clone` exists and this ticket must extend it.** `CloneIds::Regenerate`
currently remaps the `Hierarchy` component — parent and children — and nothing
else. Any component holding an entity reference is copied verbatim, so after a
duplicate its references still point into the *source* scene.

That is not an oversight to fix here: remapping needs the old-to-new map and the
reference type itself, and both are yours. The map is built inside
`Scene::clone` in `engine/src/Scene.cpp` and is discarded when it returns, so
exposing it is part of the work.

The other half is already honoured and is worth not undoing: **GUID lookup is
per-scene, never global** (`Scene::byGuid_` is a member). `CloneIds::Preserve`
puts the same GUIDs in two live scenes at once, which is exactly what makes
references keep resolving inside a play-mode clone — and exactly what a global
map would make ambiguous.

### From T0062 / D23 (2026-08-05) — this is the gameplay-facing reference type

D23 makes `hp::Ref<T>` the type a behaviour property holds when it points at
another entity or behaviour — `hp::Ref<Door> target;` in a switch, picked in the
inspector, stored as a GUID. **It must resolve on use and return null for a
destroyed target, never a dangling pointer.**

That is what makes `06-engine-conventions.md`'s rule — *do not store raw
pointers to other behaviours or entities across frames* — structural instead of
advisory, and it is why T0076's teardown-ordering hazard stops being fatal.

Ordered before T0062 (260 vs 270) on purpose: behaviour properties hold these.
