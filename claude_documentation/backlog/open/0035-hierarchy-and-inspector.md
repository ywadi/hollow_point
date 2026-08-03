# T0035 — Scene hierarchy and inspector panels

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 6 — Editor |
| **Order** | 630 |
| **Created** | 2026-08-02 |

## Why

Built as a pair, because the inspector is useless without the hierarchy feeding
it a selection. Together they are the first point at which the editor can
actually *edit* rather than only display.

## Done when

- [ ] The hierarchy lists all entities in the open scene, read live
- [ ] Selecting an entity writes its GUID into EditorState's temporary section
- [ ] The inspector reads that GUID and lists the entity's components
- [ ] Component fields are editable and changes show in the viewport immediately
- [ ] Entities can be created, renamed and deleted
- [ ] Deleting the selected entity clears the selection rather than dangling

## Subtasks

- [ ] 35.1 Hierarchy panel querying the scene each frame — do not cache (notes)
- [ ] 35.2 Selection writing to EditorState
- [ ] 35.3 Create empty entity, rename, delete
- [ ] 35.3b Duplicate an entity/subtree — uses T0071's reference remap (old→new
      GUID map), which is why it is called out rather than assumed
- [ ] 35.4 Inspector reading the selection and listing components
- [ ] 35.5 Editable widgets per component type, registered like serialization
      (T0022) rather than a central switch
- [ ] 35.6 Add/remove component UI
- [ ] 35.7 Handle a stale selection GUID after scene reload or entity deletion

## Notes / findings

**Read the entity list live from the scene, do not cache it in the panel.** A
cached list goes stale the moment anything else mutates the scene — script, undo,
asset reload — and produces a UI that disagrees with reality. Query per frame; it
is cheap at this scale.

Component editor registration should mirror component *serialization*
registration (T0022). Two parallel central switches over component types is
exactly the duplication that rots.

### Architecture review (2026-08-03)

The paragraph above predates T0053 and understates it: the inspector should
not "mirror" serialization registration — **both are generated from the same
reflection declaration** (T0053 lists the inspector as a consumer that uses
reflection "and nothing else"). 35.5's per-type widgets are the *presentation*
layer over reflected properties (plus metadata like ranges and tooltips), not
a second registry. Duplicate-entity was also missing from the ticket entirely
(now 35.3b) — it is a scene-authoring staple and it is the first editor
operation that exercises T0071's remap.

Undo/redo is deliberately not in scope, but it is much cheaper to design for now
than to retrofit: if edits go through a small command abstraction rather than
mutating components directly, undo becomes tractable later. Worth a deliberate
decision either way.


### Architecture decision (2026-08-03) — the inspector shows module-defined types (D12)

The inspector's job includes game-defined components and behaviours, which live
in the gameplay module the editor loads (see T0032's amendment). This binds the
inspector to two things it did not previously reference:

- **Reflection (T0053) must work for types defined in the module**, not only for
  engine types. Whatever the registry is, the module populates it at load and
  must depopulate on unload or a reload leaves stale entries describing types
  that no longer exist
- **Behaviours are addressed by stable name, not by `entt::type_index`** (D14).
  T0095 measured that the sequential index is a per-module runtime number which
  differs across the boundary and cannot be persisted. An inspector that keys
  anything on it will appear to work in-process and break on reload or reload
  order changes

"No module loaded" is a legitimate state the inspector must render sensibly
rather than treat as an error.
