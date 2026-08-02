# T0035 — Scene hierarchy and inspector panels

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Phase** | 6 — Editor |
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

Undo/redo is deliberately not in scope, but it is much cheaper to design for now
than to retrofit: if edits go through a small command abstraction rather than
mutating components directly, undo becomes tractable later. Worth a deliberate
decision either way.
