# T0061 — Debug draw service

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Simple |
| **Phase** | 4 — Render layer |
| **Order** | 500 |
| **Created** | 2026-08-03 |
| **Refs** | T0117, [../completed/0081-camera-system.md](../completed/0081-camera-system.md) |

## Inherited from T0081 (2026-08-05)

**Frustum debug draw (81.8) moved here.** `hp::extractFrustum` is built and
tested against real matrices — six planes, normals pointing inward — so what is
missing is not the maths but a way to draw a line, which is this ticket's whole
subject. T0081 closed rather than parking a finished camera system behind it.

Also worth knowing: **a missing *mesh* deliberately draws nothing and gets no
placeholder** (T0023, T0028). Inventing a cube would put geometry in the world no
artist authored, so the visible marker for an unloaded mesh belongs here too.

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

### Inherited from T0079 (2026-08-05) — light bounds, and what a light actually reaches

**79.6 moved here.** Two visualisations, and the second matters more than it
sounds:

- **Light bounds** — a point or spot light's `range` as a sphere or cone. Cheap,
  and it makes "why is this object dark" answerable by looking.
- **What a given light actually affects.** Selection is nearest-N with an
  illumination layer mask (`hp::selectLightsFor`), so an object can be unlit for
  three different reasons that look identical: out of range, crowded out by
  nearer lights, or excluded by the mask. Only the third is a mistake, and none
  is visible.

That pairs with T0085's 85.8, already here: the same question for a camera's
culling mask, where `DrawParseStats::culledByLayer` counts what nobody can see.
**Both are the same widget** — "show me what this viewer reaches" — and should
not become two. See [../completed/0079-lighting-system.md](../completed/0079-lighting-system.md).


### Inherited from T0085 (2026-08-05) — visualising what a camera or light actually affects

**85.8 moved here.** With layer masks honoured, "why is this object not
rendering?" gains a new answer that is invisible from the viewport: *its layer is
not in the camera's mask*.

`DrawParseStats::culledByLayer` already counts it, separately from "no mesh" and
"nothing in the scene" precisely because the three look identical on screen. A
debug view that lists or highlights what a given camera or light actually affects
is what turns that count into an answer. Same for lights once T0079 lands. See
[../completed/0085-layers-and-masks.md](../completed/0085-layers-and-masks.md).

**T0081.8 is blocked on this ticket, and is the only thing it is blocked on.**
`hp::extractFrustum(viewProjection, clip)` already returns the six world-space
planes with normals pointing inwards and distances normalised, so drawing a
camera's frustum needs no new maths — only the ability to draw lines. When this
ticket lands, close 81.8 with it.


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
