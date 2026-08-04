# T0061 — Debug draw service

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Simple |
| **Phase** | 4 — Render layer |
| **Order** | 500 |
| **Created** | 2026-08-03 |
| **Refs** | T0117 |

## Why

Many systems need to draw diagnostic geometry, and each one building its own is
waste. A single immediate-mode service — call it from anywhere, drawn this frame,
cleared next — serves all of them.

Notably it is **the one reverse dependency physics has on the renderer** (T0051):
Jolt renders collision shapes through a debug-draw interface. Culling (T0045)
wants bounds and frustums, LOD (T0040) wants level colouring, animation (T0049)
wants skeletons, and the editor wants selection outlines and grids.

## Done when

- [ ] `DebugDraw::Line/Box/Sphere/Frustum/Text` callable from anywhere
- [ ] Immediate mode — submitted geometry is drawn this frame and cleared
- [ ] Depth-tested and always-on-top modes both available
- [ ] Renders as its own layer in the RenderStack (T0027)
- [ ] Compiled out entirely in shipping builds
- [ ] Cheap enough that thousands of lines do not distort what is being profiled

## Subtasks

- [ ] 61.1 Immediate-mode API with per-frame buffering
- [ ] 61.2 Primitives: line, box, sphere, capsule, frustum, arrow, text
- [ ] 61.3 A debug render layer batching everything into few draw calls
- [ ] 61.4 Depth-tested vs overlay modes
- [ ] 61.5 Categories with independent toggles (physics, culling, animation…)
- [ ] 61.6 Thread-safe submission, since jobs will want to draw
- [ ] 61.7 Compile out in shipping builds

## Notes / findings

**Batch aggressively.** The naive implementation issues a draw call per line and
becomes the slowest thing in the frame, which then distorts every profile taken
while it is on. One dynamic vertex buffer per category, filled and drawn once.

**Thread-safe submission matters** because culling and animation run on workers
(T0050) and are exactly the systems worth visualising. Per-thread buffers merged
at frame end avoids locking on a hot path.

Jolt provides `DebugRenderer` as an abstract interface for precisely this — the
physics integration implements it against this service rather than the other way
round, keeping the dependency pointing one way.

### Cross-ticket obligations (2026-08-04, T0124 backfill)

- **T0117** owns the text stack `DebugDraw::Text` renders through (117.5) —
  it sits at Order 495, directly before this ticket, precisely so no ad-hoc
  glyph renderer grows here and then has to be reconciled away.
