# T0045 — Culling, sorting and render queues

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 440 |
| **Created** | 2026-08-02 |
| **Refs** | T0050, T0085, T0086, T0089, T0120, [../completed/0081-camera-system.md](../completed/0081-camera-system.md), [../completed/0027-render-stack.md](../completed/0027-render-stack.md), [../completed/0085-layers-and-masks.md](../completed/0085-layers-and-masks.md) — **the `cullingMask` obligation is discharged**; T0085 honours it in `parseScene`, leaving frustum culling and sorting here; [0152-winding-convention.md](../completed/0152-winding-convention.md) — **queue draws select cull mode against `WindingConvention.hpp`** (D33): single-sided is `CULL_BACK`, `FrontCounterClockwise` is declared not defaulted, and any transparent-queue pixel baseline written before T0152 landed is calibrated against backwards-wound assets; **T0147** ([../completed/0147-engine-intermediates-for-shaders.md](../completed/0147-engine-intermediates-for-shaders.md)) / **D37** — **phase 10.9 is already split into opaque (10.9a) and blend (10.9c)**, with the scene snapshot between them, so "transparents draw after opaques" is done and this ticket inherits the boundary rather than creating it. What is *not* done is the back-to-front sort **within** 10.9c, which is still submission order and is this ticket's. The constraint that comes with it: sort inside the blend pass, never by re-merging the two — the snapshot at 10.9b depends on the boundary existing, and a material that reads the screen is refused outright unless it is in 10.9c |

## Dependency note 2026-08-05 — unblocked, and shader-independent

**What this ticket needs from T0060 is already delivered.** Queues bucket by
material properties and sorting reduces material switches, so what is required is
material **identity and blend mode** — `Guid` plus `AlphaMode` — which landed in
60.1, with per-surface assignment in 60.6.

**Nothing here depends on T0141's surface-stage decision**, because culling and
sorting do not care how a surface is shaded. So this ticket can run before or
after T0141 without rework, which is what makes it safe to slot anywhere in the
sequence — unlike T0086, which is blocked on 141.0.

One thing it *will* want from T0141 when that lands: a **PSO identity** to sort
on. Sorting by material GUID reduces state changes only approximately; sorting by
the pipeline state actually bound is what removes them. Recorded rather than
built, because the PSO identity does not exist yet.

## Why

Diligent is a graphics abstraction, not a game engine: it has **no scene graph,
no culling and no render queues**. Draw submission (T0028) deliberately starts by
drawing everything in whatever order the ECS iterates — correct, but it draws
geometry that is off-screen and thrashes pipeline state.

This is the genuine gap in what Diligent provides, and it is the one worth
closing early because the whole renderer's shape depends on it.

## Done when

- [ ] Frustum culling rejects off-screen objects before submission
- [ ] Objects are bucketed into render queues (opaque, transparent, overlay)
- [ ] Opaque sorted front-to-back; transparent sorted back-to-front
- [ ] Within a queue, sorting reduces pipeline/material switches
- [ ] Culling can be disabled for debugging, and culled counts are visible
- [ ] Measurable: draw calls and triangles drop on a scene with off-screen geometry
- [ ] Culling and sorting cost is visible in Tracy and is not itself the bottleneck

## Subtasks

- [ ] 45.1 Bounding volumes per mesh, computed at import and stored in the asset
- [ ] 45.2 World-space bounds from transform + local bounds
- [ ] 45.3 Frustum extraction from the camera and an AABB/sphere test
- [ ] 45.4 Render queues with an explicit ordering policy
- [ ] 45.5 Sort keys packing material/pipeline/depth so one sort achieves both
      state coherence and depth ordering
- [ ] 45.6 Shape the cull pass so T0050.4 can parallelise it without a redesign
      — see the 2026-08-04 note. This ticket does **not** parallelise it
- [ ] 45.7 Debug counters: visible vs culled, draw calls, triangles
- [ ] 45.8 Profiling zones

## Notes / findings

### Inherited from T0079 (2026-08-05) — lights need culling too

**79.2's other half moved here.** `gatherLights` collects every enabled light,
caps at `kMaxLights` (16) and **warns** when it drops any — but *which* it drops
is registry order, which is not a stable guarantee. That is a placeholder, not a
policy.

Two things to fold in rather than bolt on:

- **Cull lights against the frustum** in the same pass as geometry. A light that
  cannot affect anything visible should never reach selection.
- **Per-object selection already exists** (`hp::selectLightsFor`, nearest-N with
  the layer mask) and runs per draw. If this ticket sorts or batches draws, that
  selection runs per item in the new order — worth checking it is not being done
  redundantly for objects sharing a light set.

Note the layer test T0085 put at the top of `parseScene` is **deliberately
first**: it is one AND, where a frustum test is arithmetic. Keep that ordering
when culling joins it. See [../completed/0079-lighting-system.md](../completed/0079-lighting-system.md).


### Discharged by T0085 (2026-08-05) — the mask filter is done; frustum culling is not

**The obligation this ticket was given by T0027 is met, and it was met
elsewhere.** `Camera::cullingMask` is now honoured in **`parseScene`**, which is
where T0085's own preamble said it belonged, so a world layer and a HUD layer can
share one `Scene` — the limitation T0027's composite test had to work around with
two scenes.

What that leaves for this ticket is unchanged and is the harder half: **frustum
culling, sorting and render queues**. The layer test is one AND per entity and
still runs over *every* drawable entity, so it makes excluded objects cheap, not
free. Insert spatial culling around the same loop; `parseScene`'s output is
deliberately an explicit list so that can happen without restructuring.

Order matters when it lands: the **layer test runs first** and is asserted to,
because it is far cheaper than a frustum test and rejects more in the common
split-camera case. See [../completed/0085-layers-and-masks.md](../completed/0085-layers-and-masks.md).

**T0081 supplies the frustum, and it must not be recomputed here.**
`hp::extractFrustum(viewProjection, clip)` returns the six world-space planes,
normalised so `intersectsSphere` distances are real distances. Computing a
frustum in a second place is how culling and LOD selection (T0040) drift and
start disagreeing about what is visible — which presents as objects popping in
one system and not the other.

**`Camera::cullingMask` is honoured as of T0085** — superseding what this
paragraph used to say, which was that nothing read it and consuming it was this
ticket's job. It is filtered in `parseScene`, before any other check. It remains
a bitmask over *object* layers — **not** a `RenderStack` layer, which is a
compositing pass, and confusing the two is the trap T0081's header comment calls
out.


**Transparent objects must not be depth-sorted the same way as opaque.** Opaque
front-to-back maximises early-Z rejection; transparent needs back-to-front for
correct blending. Two queues with different policies, not one clever comparator.

**Sort key packing is the trick worth doing properly.** Encoding
`[queue][material][pipeline][depth]` into one integer means a single sort gives
both state coherence and correct ordering. Retrofitting this later means
rewriting submission.

Bounding volumes belong in the **asset**, computed once at import (T0038/T0039),
not recomputed per frame. This is a dependency on the asset format worth noting
before it is finalised.

**Skinned meshes break the import-time-bounds assumption** (added 2026-08-03,
architecture review). A character's bind-pose AABB is wrong the moment it
animates — arms extend, weapons swing, and the mesh visibly pops out of view at
screen edges when culled by stale bounds. The standard fix is cheap:
conservative bounds computed at import *across all clips* (or bind-pose bounds
inflated by a factor), optionally refined at runtime from the sampled pose for
hero characters. Whichever is chosen, it must be chosen — skeletal animation is
core to this engine (T0041), so the bounds story cannot silently assume static
meshes.

Deliberately out of scope: occlusion culling, portals, spatial acceleration
structures (BVH/octree). Frustum culling over a linear list is entirely adequate
until object counts get large, and a spatial structure can be inserted behind the
same interface later.

### Second review pass (2026-08-03) — do not block Phase 4 on the Phase 7 asset format

45.1 stores bounds "in the asset, computed at import", but the cooked-mesh
container that would hold them is a decision T0039's note places at the start
of Phase 7 — one phase *after* this ticket. Untangle it by making import-time
bounds an optimisation, not a prerequisite: **compute bounds at load time**
(one pass over positions at import/first-load, cached in memory) for Phase 4,
and move them into the cooked asset when T0039's container exists. Same
interface, no waiting, and the skinned-mesh conservative-bounds question above
is unaffected.

Also: the frustum/AABB math in 45.3 already exists — `AdvancedMath.hpp`
(`ViewFrustum`, `ExtractViewFrustumPlanesFromMatrix` — its OpenGL flag is
always false here since D29/T0144; note `hp::extractFrustum` in
`CameraSystem.cpp` already derives the same planes for the [0, 1] clip space —
`GetBoxVisibility`, bound-box transform). Consume it (T0056), do not re-derive
it. World-space bounds come from T0101's world transforms.

### Ordering fix (2026-08-04) — this ticket no longer parallelises culling

T0124's sweep found parallel culling claimed by two tickets — 45.6 here and
50.4 in T0050 — and, worse, that this ticket sits at **Order 440** while the
thread-ownership rules and debug asserts that make parallel work safe are
written at **520**. Working the board in order would have done the dangerous
thing first, unguarded, and the failure mode T0050 names is "intermittent
corruption that is extraordinarily hard to debug".

**Resolved by removing the parallel work from here, not by resequencing.**
T0050.4 is the single owner. Two problems close with one edit: the duplicate
ownership disappears, and so does the hazard — because once this ticket does no
parallel work, there is no unguarded parallel work before the rules exist. That
also matches how T0026 was handled, deliberately placed immediately before its
first real consumer rather than early.

**What stays here is the shape, and it is not optional.** The risk in deferring
is that culling gets written in a way that makes parallelising it a rewrite, at
which point the deferral has cost more than it saved. So 45.6 becomes a design
constraint, taken from T0050's own ownership table:

- **Jobs compute, the main thread submits.** The cull pass must be a pure
  function of (frustum, bounds array) producing a visibility/index result — not
  something that submits, binds or mutates as it walks.
- **No writes to the entt registry from the cull pass.** Parallel reads of the
  registry are safe and parallel writes are not, so results go into a per-pass
  output buffer that the main thread applies. Writing culling as an in-place
  `registry.emplace<Visible>` loop is the specific thing that cannot be
  parallelised later.
- **Nothing in the cull pass touches resource state.** Diligent's state
  transitions are not thread-safe and only the main thread may perform them —
  this is the single fact T0050 says determines the entire threading model.

Write it single-threaded, in that shape, and T0050.4 becomes a change of driver
rather than a redesign. None of this costs anything today; all of it is
expensive to retrofit.

### Cross-ticket obligations (2026-08-04, T0124 backfill)

- **T0085.3**: the camera culling mask is applied in *this* culling pass — one
  AND per object, never per-pixel. T0079/T0086 assume masked-out work is never
  submitted at all.
- **Culling must be callable with an arbitrary frustum**, not only the primary
  viewer's: T0086.8 culls shadow casters per light and T0120.3 culls per
  render-texture camera, and both consume this pass. An API hard-wired to
  "the" camera forces both to fork it.
- **T0089.5** applies fog to transparents, and its note says to decide the
  approach "when the transparent queue is built (T0045), not afterwards" —
  when 45.4 defines the queue interfaces, record how a per-pixel fog term will
  reach the transparent path.
