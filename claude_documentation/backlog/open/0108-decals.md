# T0108 — Decals

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 535 |
| **Created** | 2026-08-03 |
| **Refs** | T0107, T0094, T0096, T0060, T0046; **T0147** ([../inprogress/0147-engine-intermediates-for-shaders.md](../inprogress/0147-engine-intermediates-for-shaders.md)) / **D37** — a screen-space decal reconstructs its target position from **scene depth**, and that read exists now: `HpSceneDepth`/`HpSceneViewDepth` at `In.ScreenUV`. It carries this ticket's two constraints with it — the decal material must be `alphaMode: Blend`, and the depth is the opaque pass's, so a decal cannot land on a transparent surface. What T0147 did **not** give it is per-instance data, which the capability matrix still records as this ticket's other blocker |

## Why

Decals are how a 3D world keeps a record of what happened in it: scorch marks
under explosions, bullet holes, blood, cracks, tyre tracks, and the deliberate
art kind — grime in corners, painted markings on floors.

They are currently an **orphan**. T0094 mentions "decal buffers" once, as an
example of the sort of thing gameplay-extensible rendering should permit. No
ticket owns them, nothing describes how they are drawn, and T0107's explosion
needs one to leave a mark.

Without them, every impact in the game is over the instant its particles retire,
and the world never accumulates evidence of play.

## Done when

- [ ] A decal can be placed at a world transform and projects onto the geometry
      beneath it
- [ ] It respects surface orientation — a decal on a wall does not smear across
      the floor behind it
- [ ] Decals have a lifetime and fade out rather than popping
- [ ] A budget with oldest-first retirement; the world does not accumulate
      decals until the frame dies
- [ ] Spawnable from gameplay and as part of a composed effect (T0107)
- [ ] Works on both targets

## Subtasks

- [ ] 108.1 Choose the technique — deferred/screen-space projection versus mesh
      decals. See notes; this is the fork and it depends on T0025/T0096
- [ ] 108.2 Projection box and the angle cutoff that stops smearing
- [ ] 108.3 Decal material: albedo at minimum, and decide whether normal and
      roughness can be affected too
- [ ] 108.4 Lifetime, fade-out and the retirement budget
- [ ] 108.5 Sorting and layering when decals overlap
- [ ] 108.6 Editor placement and preview for authored (non-gameplay) decals
- [ ] 108.7 Integration with T0107 so an effect can leave one

## Notes / findings

**108.1 depends on a decision that has not been made yet.** Screen-space
projected decals are cheap, composite well, and can modify normals and
roughness — but they need a **G-buffer**, so they presuppose a deferred or
hybrid renderer. Mesh decals (generate geometry clipped to the receiving
surface) work in any renderer including forward, cost more CPU and geometry, and
handle skinned or moving receivers badly. **The renderer's shape is decided in
T0025/T0096 and this ticket must follow it, not lead it.** Do not start 108.1
before that is settled.

**Angle cutoff is what separates a decal system from an artefact generator.**
Projecting onto a surface facing away from the decal's normal produces a stretched
smear along walls and over ledges, which is the single most recognisable
failure. It is a dot-product rejection in the shader and it must be there from
the first version, not added when someone reports the smear.

**Decals and vision are related in this project.** T0093 does vision-based
visibility and fog of war. A scorch mark inside unexplored fog should presumably
not be visible, which makes decals subject to the same visibility rules as
everything else — worth checking against T0093 rather than discovering that
decals leak information about unexplored areas.

**Budget seriously.** Decals are individually cheap and collectively not: a
firefight leaves hundreds, each one overdraw on the surface beneath it. Oldest-
first retirement with a hard cap, in the same spirit as T0080's particle budget.

**Authored decals and gameplay decals are the same system with different
lifetimes.** Art-placed grime is a decal with infinite lifetime and no budget
participation; a bullet hole is a decal with a short one that competes for the
cap. Designing them as one thing with a flag is simpler than two systems, but
the budget must distinguish them or authored art disappears in a firefight.
