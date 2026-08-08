# T0063 — Editor camera controls and entity picking

| | |
|---|---|
| **Status** | ❌ SUPERSEDED |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 6 — Editor |
| **Order** | 620 |
| **Created** | 2026-08-03 |
| **Superseded by** | **T0172** (editor camera) and **T0173** (viewport picking) |
| **Refs** | T0120; T0165 |

## Why it was split (2026-08-08)

This ticket held two halves with **different dependency risk**, and carrying them
together held the cheap one hostage to the expensive one.

**A camera writes a view matrix and touches no draw path.** It reads input, it
moves a transform, and the renderer never learns it exists. So it carries **zero
refactor risk while T0170 is in flight** — the render loop can change shape
underneath it without a line of camera code moving.

**Picking does touch the draw path, and does not.** The chosen mechanism is a GPU
ID buffer rendered through the same submission walk as the main view, read back
asynchronously — which is exactly the code T0170 is rewriting, exactly the
offscreen-render mechanism T0120 exists to own, and exactly the viewport-space
mapping T0033's panel framework has not defined yet. Every one of those is
moving. The renderer moved twice on 2026-08-08 alone.

The consequence was concrete and it is why this was not left alone: **the editor
has had no camera control of any kind** — no orbit, no fly, no trackball; `grep`
across `apps/editor/src/` found nothing — and the owner spent a full day reading
offscreen PNG dumps because of it. The rock cube's black top face (T0158, T0167)
was found *by eye*, not by any assertion. That cost was being paid to keep a
camera queued behind a picking design that is blocked on three unfinished
tickets.

So the split is by **risk and dependency**, not by size:

| | Was | Now |
|---|---|---|
| Editor camera state, held outside the scene | 63.1 | **172.1** |
| Orbit / pan / zoom through the input context | 63.2 | **172.2** |
| Fly mode with configurable speed | 63.3 | **172.3** |
| Persist camera per project | 63.7 | **172.7** |
| Focus-on-selection using entity bounds | 63.4 | **173.1** |
| Picking — GPU ID buffer or CPU raycast | 63.5 | **173.2** |
| Map viewport coordinates to a ray or buffer lookup | 63.6 | **173.3** |

Both successors carry **T0165**'s constraint verbatim, because both compute a
view: the engine is right-handed, the camera looks down its own **−Z**, and
`worldToScreen` refuses a point at **positive** view-space z. Nothing in either
may reintroduce a mirror.

Everything this ticket recorded — the "not an entity in the scene" argument, the
ID-buffer-versus-raycast comparison, the `GPUCompletionAwaitQueue` and
`CoordinateGridRenderer` findings, and T0120's migration obligation — was copied
to whichever successor owns it. Nothing was dropped.

## Original content, kept for the record

### Why

Scene authoring is a stated priority, and these two are the floor for it: you
cannot build a scene without moving the view and clicking to select. Neither is
covered by any existing ticket.

### Done when

- [ ] Orbit, pan, zoom and a fly/WASD mode in the viewport
- [ ] Focus-on-selection (frame the selected entity)
- [ ] Clicking an entity in the viewport selects it, writing to EditorState
- [ ] Clicking empty space deselects
- [ ] Picking is accurate for skinned and LOD'd meshes, not just static ones
- [ ] The editor camera is **not** an entity in the scene — see notes
- [ ] Camera position persists per project across editor restarts

### Notes / findings

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

#### Architecture review (2026-08-03)

Two existing Diligent pieces reduce this ticket's cost — check both before
building: `GPUCompletionAwaitQueue.hpp` (GraphicsTools) is purpose-built for
the async-readback pattern the ID buffer needs, and DiligentFX's
`CoordinateGridRenderer` is a ready-made editor viewport grid — a standard
editor feature no ticket currently mentions, and it comes for free with the
DiligentFX dependency (its ImGui settings panel included, per D6).

#### Cross-ticket obligations (2026-08-04, T0124 backfill)

- **T0120** provides the shared render-a-camera-to-texture mechanism and names
  this ticket's ID-buffer render as a migration candidate (120.10). Check what
  T0120 has built before implementing 63.5 as another bespoke offscreen render
  — three independent one-off renders is the divergence T0120 was filed to
  stop.
</content>
</invoke>
