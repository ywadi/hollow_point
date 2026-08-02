# T0063 — Editor camera controls and entity picking

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 6 — Editor |
| **Created** | 2026-08-03 |

## Why

Scene authoring is a stated priority, and these two are the floor for it: you
cannot build a scene without moving the view and clicking to select. Neither is
covered by any existing ticket.

## Done when

- [ ] Orbit, pan, zoom and a fly/WASD mode in the viewport
- [ ] Focus-on-selection (frame the selected entity)
- [ ] Clicking an entity in the viewport selects it, writing to EditorState
- [ ] Clicking empty space deselects
- [ ] Picking is accurate for skinned and LOD'd meshes, not just static ones
- [ ] The editor camera is **not** an entity in the scene — see notes
- [ ] Camera position persists per project across editor restarts

## Subtasks

- [ ] 63.1 Editor camera state, held outside the scene
- [ ] 63.2 Orbit / pan / zoom bound through the input context (T0068)
- [ ] 63.3 Fly mode with configurable speed
- [ ] 63.4 Focus-on-selection using the entity's bounds (T0045)
- [ ] 63.5 Picking — choose GPU ID buffer or CPU raycast (see notes)
- [ ] 63.6 Map viewport coordinates to a ray or buffer lookup (T0033 has the
      panel-space mapping)
- [ ] 63.7 Persist camera per project in EditorState (T0034)

## Notes / findings

**The editor camera must not be an entity in the scene.** If it is, it gets saved
into the scene file, appears in the hierarchy, and ships with the game. It belongs
to the editor, alongside EditorState — the same reasoning that keeps the active
scene *out* of EditorState (T0034), applied in the opposite direction.

**Picking: GPU ID buffer versus CPU raycast.** The ID buffer renders entity IDs
to an offscreen target and reads back the pixel under the cursor — pixel-accurate,
handles skinning and LOD for free because it uses the same draw path, but costs a
render target and a readback stall. CPU raycast against bounding volumes is
cheaper and needs no GPU work, but is only as accurate as the bounds and gets
skinned meshes wrong. **The ID buffer is the better fit here**, because skeletal
animation is core and bounds-based picking on animated characters is visibly
wrong.

Read back the ID buffer *asynchronously* — a synchronous readback stalls the
pipeline and makes selection feel laggy. One frame of latency on selection is
imperceptible.

### Architecture review (2026-08-03)

Two existing Diligent pieces reduce this ticket's cost — check both before
building: `GPUCompletionAwaitQueue.hpp` (GraphicsTools) is purpose-built for
the async-readback pattern the ID buffer needs, and DiligentFX's
`CoordinateGridRenderer` is a ready-made editor viewport grid — a standard
editor feature no ticket currently mentions, and it comes for free with the
DiligentFX dependency (its ImGui settings panel included, per D6).
