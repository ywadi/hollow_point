# T0071 — Entity references

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 3 — Data model |
| **Created** | 2026-08-03 |

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
