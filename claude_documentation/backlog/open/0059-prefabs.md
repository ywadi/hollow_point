# T0059 — Prefabs and entity templates

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 3 — Data model |
| **Created** | 2026-08-03 |

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
