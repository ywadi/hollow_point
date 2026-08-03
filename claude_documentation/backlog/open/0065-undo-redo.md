# T0065 — Undo/redo command system

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 6 — Editor |
| **Order** | 650 |
| **Created** | 2026-08-03 |

## Why

An editor without undo is not usable for real work — every mistake becomes a
manual repair, and people stop experimenting.

It is listed here as a Phase 6 feature, but it has a **Phase 3 design
consequence**: if editor edits mutate components directly, undo is impossible to
add without rewriting every edit path. If they go through a command abstraction
from the start, it stays tractable. That is why it is worth deciding early even
though the UI comes later.

## Done when

- [ ] Every editor mutation goes through a command — no direct component writes
- [ ] Undo and redo restore state exactly, including selection
- [ ] Commands coalesce where appropriate (a gizmo drag is one entry, T0064)
- [ ] A bounded history with a sensible depth
- [ ] Structural changes work: create, delete, reparent, add/remove component
- [ ] Undo history is cleared on scene load and on entering play mode
- [ ] The history is inspectable for debugging

## Subtasks

- [ ] 65.1 `ICommand` with execute/undo, and a stack with a redo branch
- [ ] 65.2 **Generic property-set command driven by reflection (T0053)** — see notes
- [ ] 65.3 Structural commands: create, destroy, reparent, add/remove component
- [ ] 65.4 Coalescing for continuous edits
- [ ] 65.5 Bounded history with memory awareness
- [ ] 65.6 Clear on scene change and on play-mode entry
- [ ] 65.7 Wire every panel through it — inspector, hierarchy, gizmos
- [ ] 65.8 Tests, especially reparenting and delete/undo of a parented subtree

## Notes / findings

**Reflection makes this tractable.** With T0053, one generic command covers every
property on every component: record the property path plus before and after
values, and undo is a set. Without it, every editable field needs a hand-written
command class — hundreds of them, each a chance to get undo subtly wrong.

**Delete is the hard case.** Undoing a delete must restore the entity with its
original GUID, all components, its children, *and* anything that referenced it.
Restoring with a fresh GUID silently breaks every reference — and it looks like
it worked. Serialize the subtree on delete and restore it wholesale.

**Play mode must not be undoable.** Entering play clones the scene (T0037);
changes during play are discarded on stop, so mixing them into the same history
lets an undo after stopping corrupt the authored scene. Clear on entry.
