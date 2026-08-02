# T0045 — Culling, sorting and render queues

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Created** | 2026-08-02 |

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

Deliberately out of scope: occlusion culling, portals, spatial acceleration
structures (BVH/octree). Frustum culling over a linear list is entirely adequate
until object counts get large, and a spatial structure can be inserted behind the
same interface later.
