# T0034 — EditorState

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Simple |
| **Phase** | 6 — Editor |
| **Created** | 2026-08-02 |

## Why

Panels increasingly need to share data — most immediately, the hierarchy panel
must tell the inspector which entity is selected. `EditorState` is that shared
struct, passed by pointer, split into a temporary (session-only) section and a
persistent (serialized) one.

Built before the hierarchy/inspector pair because they are its first consumers.

## Done when

- [ ] `EditorState` with clearly separated temporary and persistent sections
- [ ] The persistent section round-trips via T0020 and survives a restart
- [ ] Passed to panels by pointer, like the asset pool
- [ ] Selected entity GUID lives in the temporary section
- [ ] It lives in `apps/editor` — never in the engine library

## Subtasks

- [ ] 34.1 The struct, with the two sections visibly separated
- [ ] 34.2 Persistent fields: last viewport mode, last project, theme path, layout
- [ ] 34.3 Temporary fields: selected entity GUID, transient UI state
- [ ] 34.4 Save on exit, load on start
- [ ] 34.5 Handle a corrupt or older state file by falling back to defaults

## Notes / findings

**The critical rule: the active scene does NOT belong here.** It lives in the
scene/project manager inside the engine library (T0024). EditorState vanishes when
the editor is stripped at export, and the runtime still has to know which scene to
load. Putting the scene here would work perfectly right up until export, then fail
in a confusing way.

Useful test for anything proposed for EditorState: *would the exported game still
work without it?* If no, it belongs in the engine.

A corrupt state file must never prevent the editor starting. Fall back to defaults
and say so — being unable to launch because a UI preference file is malformed is
an infuriating failure mode.
