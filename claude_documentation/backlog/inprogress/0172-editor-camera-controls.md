# T0172 — Editor camera: orbit, fly, and opening a model to look at

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Simple |
| **Phase** | 6 — Editor |
| **Order** | 620 |
| **Created** | 2026-08-08 |
| **Splits from** | **T0063**, with **T0173** (viewport picking) as the other half |
| **Refs** | T0068 (input contexts); T0034 (EditorState, for persistence); T0173 — picking needs the view this ticket produces, and must not build a second camera; **T0165** — the engine is right-handed and the camera looks down its own **−Z**. An editor camera's forward and its orbit maths both follow from that. Nothing here may reintroduce a mirror (a negative camera-parent scale is `WindingConvention.hpp`'s item 5, still unhandled). **D40** — Diligent vendors the camera; we add the seam |

## Why

**The editor has no camera control at all.** No orbit, no fly, no trackball —
`grep` across `apps/editor/src/` finds nothing. It opens on whatever the rockcube
module built, from one fixed vantage, and you cannot move.

The cost is not hypothetical and it is not small. **The owner spent a full day
reading offscreen PNG dumps** because that was the only way to see the renderer's
output from a second angle, and the rock cube's black top face (T0158, T0167) was
found **by eye** rather than by any assertion. Every rendering change in this
repository is currently reviewed either by writing a gpu test that captures a
frame, or by not reviewing it.

**Opening an arbitrary model matters as much as the camera**, for the same
reason: today the editor shows only what a compiled-in gameplay module built, so
looking at the Aston Martin — or any asset an importer just produced (T0169) —
means writing a gpu test.

This is separated from picking (**T0173**) on **dependency risk**: a camera
writes a view matrix and touches no draw path, so it carries zero refactor risk
while T0170 is in flight. Picking touches the draw path and does not. T0063
records the full argument.

## Done when

- [ ] The editor viewport can be **flown** — WASD plus mouse-look — and
      **orbited** — drag to rotate, wheel to zoom
- [ ] `hp_editor <path-to-model>` opens an arbitrary glTF/GLB and frames it
- [ ] The camera obeys **T0165**: forward is the camera's own −Z, and the
      view matrix's determinant is positive (no mirror)
- [ ] Nothing here rebuilds what Diligent vendors — the written note says what
      was reused and what the seam is (**D40**)
- [ ] Camera position persists per project across editor restarts

## Subtasks

- [ ] 172.0 **Write down what Diligent already provides** before writing a line
      — `FirstPersonCamera`, `TrackballCamera`, `InputControllerBase`
      (`DiligentSamples/SampleBase/`). **D40 is binding.** The owner's own
      reference does the whole thing in ~20 lines:
      `/media/ywadi/second/dillegent_tests/AstonMartinScene/src/AstonMartinScene.cpp`
      — read it, never edit it, it is the owner's
- [ ] 172.1 Editor camera state, held outside the scene (see notes)
- [ ] 172.2 Orbit / pan / zoom, bound through the input context (T0068)
- [ ] 172.3 Fly mode with configurable speed
- [ ] 172.4 Open a model from the command line (or a drop target — whichever is
      smaller), imported through the VFS like everything else (**D13**)
- [ ] 172.5 Frame the opened model automatically, from its bounds
- [ ] 172.7 Persist camera per project in EditorState (T0034)

## Notes / findings

**The editor camera should not end up in a saved scene file.** T0063's argument
stands: if the editor camera is an entity the scene serializes, it appears in the
hierarchy and ships with the game — the same reasoning that keeps the active
scene *out* of EditorState (T0034), applied in the opposite direction.

That is a statement about **serialization**, not about where the state lives this
week. Until T0033's viewport panel and T0024's project exist, the editor's only
camera *is* the scene's camera, and driving the existing camera entity is
strictly less machinery than inventing an editor-only view path that
`SceneView::render` would then have to be taught about. Whichever way it is done,
the obligation is that saving a scene does not write it.

**T0165, restated because both halves of T0063 need it.** The engine is
right-handed; the camera looks down its own −Z. Diligent's camera classes are
written for a **left-handed** view space (their camera looks down +Z), and the
mirror between the two is absorbed by `hp::projectionMatrix`, which negates the
third row of Diligent's left-handed form. So a seam that converts a Diligent
camera's state into an `hp::Transform` must convert **only the rotation
convention** and must never introduce a det −1 basis. `HandednessConvention.hpp`
is the binding statement.
</content>
</invoke>
