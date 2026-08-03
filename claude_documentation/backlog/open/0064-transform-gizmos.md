# T0064 — Transform gizmos

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 6 — Editor |
| **Order** | 640 |
| **Created** | 2026-08-03 |

## Why

Scene building is a stated priority, and typing numbers into the inspector is not
scene building. Translate/rotate/scale handles in the viewport are the primary
authoring interaction.

## Done when

- [ ] Translate, rotate and scale gizmos on the selected entity
- [ ] Local and world space modes, toggleable
- [ ] Snapping — grid for translate, angle for rotate
- [ ] Dragging a gizmo produces **one** undo entry, not one per frame (T0065)
- [ ] Gizmo interaction does not also trigger viewport picking (T0063)
- [ ] Correct behaviour on children of a parented transform (T0021)
- [ ] Multi-selection, or an explicit decision not to support it

## Subtasks

- [ ] 64.1 Vendor **ImGuizmo** — see notes on which library
- [ ] 64.2 Render gizmos over the viewport panel with the right matrices
- [ ] 64.3 Mode switching (W/E/R or similar) via the input context
- [ ] 64.4 Local vs world space
- [ ] 64.5 Snapping with configurable increments
- [ ] 64.6 Coalesce a drag into a single undo command
- [ ] 64.7 Suppress picking while a gizmo is being dragged
- [ ] 64.8 Handle parented transforms correctly

## Notes / findings

**Diligent bundles `imGuIZMO.quat`, which is NOT what this needs.** That is a
small *orientation widget* — a rotation ball — not a scene manipulator.
**ImGuizmo** (CedricGuillemet) is the translate/rotate/scale gizmo, and Diligent's
own USDViewer sample already pulls it via FetchContent, so compatibility with this
ImGui version is effectively proven. Vendor it as a submodule like the others
rather than relying on a sample's FetchContent.

**Coalescing the drag into one undo entry is the detail that gets missed.** A
gizmo fires a transform change every frame while dragging; recording each one
makes undo useless — fifty presses to undo one move. Open a command on drag
start, update it during, commit on release.

Parented transforms are the other trap: the gizmo works in world space, but the
transform stored is usually local to the parent. The conversion has to happen on
the way in and out or children move incorrectly.
