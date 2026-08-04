# T0045 — Culling, sorting and render queues

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 440 |
| **Created** | 2026-08-02 |
| **Refs** | T0085, T0086, T0089, T0120 |

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
- [ ] 45.6 Parallel culling via the job system (T0026)
- [ ] 45.7 Debug counters: visible vs culled, draw calls, triangles
- [ ] 45.8 Profiling zones

## Notes / findings

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
(`ViewFrustum`, `ExtractViewFrustumPlanesFromMatrix` with its OpenGL flag,
`GetBoxVisibility`, bound-box transform). Consume it (T0056), do not re-derive
it. World-space bounds come from T0101's world transforms.

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
