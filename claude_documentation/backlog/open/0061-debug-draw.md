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

## Verdict 2026-08-08 (T0171): ⬆️ **genuinely ours**, and the argument is measured

**This ticket was expected to be the cheapest win on the board — "construct the
three renderers DiligentFX already ships and put an `hp::` API in front". Read
from source, that is wrong**, and the correction is worth more than the
assumption was.

DiligentFX ships **three specific visualisations**, not a debug-draw API:

| What it is | What it actually does |
|---|---|
| `Components/interface/BoundBoxRenderer.hpp` | **One box per `Prepare` + `Render` pair** (`:139`, `:143`) — `RenderAttribs` carries a single `BoundBoxTransform` and a single colour. Colour, dash pattern and a reverse-depth option are the knobs |
| `Components/interface/CoordinateGridRenderer.hpp` | A **full-screen, ray-marched, depth-aware infinite grid** with three plane flags and three axis flags. This is an *editor grid* |
| `Components/interface/VectorFieldRenderer.hpp` | A line grid whose directions come from a **2D vector-field texture** — i.e. motion vectors. Not a general arrow API |

**And there is nothing else.** No line renderer, no sphere, no capsule, no
frustum, no text, and nothing batched — in DiligentCore, DiligentFX *or*
DiligentTools. So the primitives this ticket exists to provide have **no upstream
implementation at all**.

**Worse, the one that looks closest is the trap.** This ticket's own note says
*"the naive implementation issues a draw call per line and becomes the slowest
thing in the frame, which then distorts every profile taken while it is on."*
Driving `BoundBoxRenderer` once per bounding box **is** that naive
implementation, with a constant-buffer write per box on top. It is right for
**one** highlighted box — an editor selection outline — and wrong for the
hundred a culling visualisation draws.

**A correction to the capability matrix worth keeping**: these three were
recorded as *"unreferenced"*. They are not — `HnRenderBoundBoxTask.cpp:141-209`
and `HnPostProcessTask.cpp:181-512` construct and drive all three. They are
unreferenced **by this engine**, and Hydrogent is the worked example of how to
drive them if any of the three rows below is picked up.

### What comes off this ticket

- **The editor coordinate grid → T0032/T0033.** `CoordinateGridRenderer` is a
  complete answer to a thing the editor wants and this ticket does not.
- **Selection outline → T0032**, if it wants one box drawn well.
- **Motion-vector visualisation → T0111**, if motion vectors ever exist.

None of the three is a debug-draw primitive, and keeping them here would make
this ticket look half-vendored when it is not.

## Done when

- [ ] `DebugDraw::Line/Box/Sphere/Capsule/Frustum/Arrow/Text` callable from
      anywhere
- [ ] Immediate mode — submitted geometry is drawn this frame and cleared
- [ ] **Batched**: thousands of lines cost a handful of draw calls, measured.
      This is the acceptance criterion, not a nice-to-have — an unbatched
      implementation distorts every profile taken while it is on
- [ ] Depth-tested and always-on-top modes both available
- [ ] Renders as its own layer in the RenderStack (T0027)
- [ ] Compiled out entirely in shipping builds
- [ ] **T0081.8 closes with it** — `hp::extractFrustum` already returns the six
      planes; this ticket is the only thing it is blocked on

## Subtasks

- [ ] 61.1 Immediate-mode API with per-frame buffering
- [ ] 61.2 Primitives: line, box, sphere, capsule, frustum, arrow, text
- [ ] 61.3 A debug render layer **batching everything into few draw calls** —
      one dynamic vertex buffer per category, filled and drawn once
- [ ] 61.4 Depth-tested vs overlay modes
- [ ] 61.5 Categories with independent toggles (physics, culling, animation…)
- [ ] 61.6 Thread-safe submission, since jobs will want to draw
- [ ] 61.7 Compile out in shipping builds
- [ ] 61.8 **Hand off the three vendored visualisations** — grid to T0032/T0033,
      selection box to T0032, vector field to T0111 — rather than absorbing them

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
