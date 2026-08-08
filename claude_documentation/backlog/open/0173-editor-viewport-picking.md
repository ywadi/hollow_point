# T0173 — Entity picking in the editor viewport

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 6 — Editor |
| **Order** | 621 |
| **Created** | 2026-08-08 |
| **Splits from** | **T0063**, with **T0172** (editor camera) as the other half |
| **Refs** | **T0172** — provides the view this picks against; do **not** build a second camera here, take the one it produces. **T0120** — the shared render-a-camera-to-texture mechanism; 120.10 names this ticket's ID-buffer render as a migration candidate. **T0033** — panel-space mapping; the viewport rectangle a click must be expressed in. **T0045** — bounds, which focus-on-selection needs. **T0034** — EditorState, where the selection is written. **T0170** — rewriting the submission walk an ID pass would ride on. **T0165** — the engine is right-handed and the camera looks down its own **−Z**; `screenToWorldRay`'s output follows from that, and `worldToScreen` refuses a point at **positive** view-space z, not negative. Nothing here may reintroduce a mirror |

## Why

You cannot build a scene without clicking to select. Nothing else covers it.

**Split from T0063 because of what it depends on.** Picking touches the draw
path: the chosen mechanism renders entity IDs through the same submission walk as
the main view, which is the code **T0170** is rewriting, through the offscreen
mechanism **T0120** exists to own, into a viewport rectangle **T0033** has not
defined yet. Three of its four dependencies are moving, and the renderer moved
twice on 2026-08-08 alone. The camera half depends on none of them, so it went
ahead as T0172 rather than waiting here.

**Read T0120 before starting.** Three independent one-off offscreen renders is
the divergence T0120 was filed to stop, and this is the third of them.

## Done when

- [ ] Clicking an entity in the viewport selects it, writing to EditorState
- [ ] Clicking empty space deselects
- [ ] Picking is accurate for skinned and LOD'd meshes, not just static ones
- [ ] Focus-on-selection frames the selected entity
- [ ] The ID pass reuses T0120's mechanism, or the ticket says why it cannot

## Subtasks

- [ ] 173.1 Focus-on-selection using the entity's bounds (T0045), driving the
      camera T0172 built
- [ ] 173.2 Picking — GPU ID buffer or CPU raycast (see notes; the ID buffer is
      the standing recommendation)
- [ ] 173.3 Map viewport coordinates to a ray or a buffer lookup (T0033 has the
      panel-space mapping)

## Notes / findings

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

### Architecture review (2026-08-03, carried from T0063)

Two existing Diligent pieces reduce this ticket's cost — check both before
building: `GPUCompletionAwaitQueue.hpp` (GraphicsTools) is purpose-built for
the async-readback pattern the ID buffer needs, and DiligentFX's
`CoordinateGridRenderer` is a ready-made editor viewport grid — a standard
editor feature no ticket currently mentions, and it comes for free with the
DiligentFX dependency (its ImGui settings panel included, per D6). **D40** makes
that check binding rather than advisory.

### The selection is EditorState's, not the scene's

A selection is editor state and must not be serialized into the scene file, for
T0063's reason: anything the editor keeps in the scene ships with the game.
</content>
</invoke>
