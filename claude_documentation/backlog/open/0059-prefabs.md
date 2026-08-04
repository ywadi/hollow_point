# T0059 — Prefabs and entity templates

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 3 — Data model |
| **Order** | 300 |
| **Created** | 2026-08-03 |
| **Refs** | T0053, T0107 |

## Why

Scene authoring is a stated priority, and without prefabs it means copy-paste:
building a door once, then duplicating it forty times and editing forty copies
when it changes.

A prefab is a saved entity hierarchy, instantiable into scenes, where edits to the
source propagate to instances and per-instance overrides are preserved.

## Done when

- [ ] An entity hierarchy can be saved as a prefab asset with a GUID
- [ ] Prefabs instantiate into a scene
- [ ] Editing the prefab updates existing instances
- [ ] **Per-instance overrides survive** a prefab edit
- [ ] Instances are visually distinguishable in the hierarchy panel
- [ ] Nested prefabs work, or are explicitly and deliberately unsupported
- [ ] Prefab instances serialize as a reference plus overrides, not as a full copy

## Subtasks

- [ ] 59.1 Prefab as an asset: a serialized entity hierarchy (T0022)
- [ ] 59.2 Instantiate into a scene, generating fresh entity GUIDs
- [ ] 59.3 Track which entities came from which prefab
- [ ] 59.4 Override model — record per-property deltas against the source
- [ ] 59.5 Propagate source edits without clobbering overrides
- [ ] 59.6 Editor: create prefab from selection, apply, revert
- [ ] 59.7 Decide on nested prefabs — see notes
- [ ] 59.8 Tests, especially override survival across an edit
- [ ] 59.9 **Persist the per-instance GUID map in the scene file** — see the
      second-pass note; without it every scene load breaks references into
      prefab instances

## Notes / findings

**The override model is the hard part, and it depends on reflection (T0053).**
An override is "this property, on this entity within the instance, differs from
the source". Recording that needs stable property paths, which is exactly what
the reflection layer provides. Without it, overrides degrade into storing whole
copies, and the propagation benefit disappears.

**Nested prefabs multiply the complexity** — an override on an instance of a
prefab that itself contains an instance. Unity took years to get this right.
Deciding *not* to support nesting initially is entirely respectable, provided it
is a decision rather than an accident, and provided the data model does not
silently half-support it.

Prefab instances must serialize as reference + overrides. Serializing the expanded
hierarchy works and quietly destroys the entire feature — every instance becomes
an independent copy the moment the scene is saved.

### Second review pass (2026-08-03) — instance entity GUIDs must be *persistent*, not per-load

59.2 generates fresh GUIDs at instantiation, and the serialization model stores
only "reference + overrides". Combine the two naively and **every scene load
re-instantiates with new GUIDs** — which silently breaks three systems that
reference entities *inside* an instance by GUID:

- an `EntityRef` (T0071) from outside the instance to a child of it (a switch
  wired to a door that is part of a prefab) dangles after save/load;
- authored signal connections (T0072) into the instance dangle the same way;
- save games (T0083) that say "door `guid` is open" cannot find the door after
  the game restarts, because the door has a different GUID every run.

The fix is small if designed in and a migration if not: the scene file records,
per instance, the **template-entity → instance-entity GUID map** (or derives it
deterministically, e.g. hash of instance-root GUID + a stable per-entity key in
the template). Loading an instance reuses the recorded GUIDs; only *new*
template entities added since the save get fresh ones — which is also exactly
the stable intra-instance identity the override model (59.4) needs for its
property paths, so this is one mechanism, not two. Unity's fileID/guid
composition is this same answer.

Tests to add under 59.8/59.9: save → load → save produces identical GUIDs; an
external EntityRef into an instance survives a reload; two instances of the
same prefab never share a GUID.

### Cross-ticket obligations (2026-08-04, T0124 backfill)

- **T0107.1** may resolve "what is a VFX asset" as *a prefab with a Lifetime
  component*. When deciding 59.7 (nesting) and the instantiate API, know that
  multi-entity spawn-as-a-unit with per-component timing and self-retirement
  is a candidate consumer — two spawnable-tree concepts in one engine is the
  lasting tax T0107 is trying to avoid.
