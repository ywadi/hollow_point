# T0098 — Navigation and pathfinding

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Complex |
| **Phase** | 9 — Physics |
| **Order** | 810 |
| **Created** | 2026-08-03 |
| **Refs** | T0044, T0049, T0051, T0061, T0093 |

> **Placeholder epic.** Recorded so the architecture accounts for it, not
> because it is ready to start. Break into real tickets when reached — the
> scope below is markers, not a plan. Filed under Phase 9 because navmesh
> baking consumes physics collision geometry and path following lands on the
> character controller; a separate phase may be warranted when it is broken up.

## Why

The backlog contains no mention of navigation anywhere, yet the systems
already planned imply it strongly: vision cones and alert states (T0093) exist
so that **AI agents** can perceive — and agents that perceive must then *move
somewhere*, through level geometry, without walking through walls. A
character-driven game with skeletal animation (T0041/T0049), a character
controller (T0051) and enemy perception has pathfinding as a near-certain
requirement, and it is the classic subsystem that gets discovered late and
bolted on badly.

It cannot be cleanly bolted on, for the same reason as physics: **navmesh
generation is a content-pipeline step** over level collision geometry, and
**path following is a consumer of the character controller** — both are
systems built earlier, and both are cheap to leave hooks in and expensive to
retrofit.

**Recast/Detour** (`recastnavigation`) is the de-facto standard: zlib licence,
plain C++, no exotic dependencies, used by Unreal and most custom engines.
Expected to cross-compile without drama, but that is exactly the claim this
project has learned to verify first (G2/G3/G4).

## Rough scope

- [ ] Confirm with T0044 that the game has navigating NPCs at all — if it
      somehow does not, close this with that finding
- [ ] Vendor recastnavigation; confirm it cross-compiles to
      `x86_64-windows-gnu`
- [ ] Navmesh baked per scene in the editor, cached like other cooked data
      (discardable, rebuilt on geometry change), serialized with the project
- [ ] Bake input: physics collision geometry (T0051), not render meshes —
      decide and record, because the two diverge the moment LODs exist
- [ ] Agent component: path query, path following, steering parameters
- [ ] Path following integrated with the character controller — desired
      velocity in, the same seam root motion uses (T0049/T0051)
- [ ] Debug draw of the navmesh, paths and agent state (T0061)
- [ ] Off-mesh links (jumps, ladders) — authored, serialized
- [ ] Dynamic obstacles / re-baking tiles — defer until something needs it
- [ ] DetourCrowd for local avoidance — separate decision from pathfinding;
      defer until agent counts demand it

## Notes / findings

- The perception side (vision, hearing) deliberately stays with T0093 and
  gameplay code; this ticket is only *getting from A to B*. Keeping those
  separate mirrors the engine/game split — perception policy is gameplay,
  navigation mechanics are engine.
- Navmesh data is a per-scene cooked asset: it wants the same
  hash-based staleness rule as T0020's binary cache, and export (T0043) must
  ship it — worth remembering in T0043's asset walk when this becomes real.
