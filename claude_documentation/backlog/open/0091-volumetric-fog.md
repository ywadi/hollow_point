# T0091 — Volumetric fog and light shafts

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Very Complex |
| **Phase** | 11 — World & environment |
| **Order** | 850 |
| **Created** | 2026-08-03 |

## Why

Distance and height fog (T0089) tint the image with depth. **Volumetric** fog is
fog that light actually travels through — god rays through a window, a spotlight
beam made visible, mist that thickens where a light shines into it.

It is the difference between fog as a colour filter and fog as part of the scene.
It is also substantially more expensive, which is why it is separated from T0089
rather than being a subtask of it.

## Done when

- [ ] Fog is lit per-volume, so light shafts appear from shadow-casting lights
- [ ] Point, spot and directional lights all contribute
- [ ] Density varies in space — height falloff at minimum, ideally local volumes
- [ ] Temporally stable — no visible noise or crawling
- [ ] Quality setting meaningfully trades cost against fidelity
- [ ] Correct interaction with transparent surfaces
- [ ] Cost is measured and bounded, with a Tracy zone of its own (T0030)

## Subtasks

- [ ] 91.1 Froxel grid — a view-frustum-aligned 3D volume
- [ ] 91.2 Inject light per froxel, sampling shadow maps (T0086)
- [ ] 91.3 Integrate along the view ray to produce scattering and transmittance
- [ ] 91.4 Apply to opaque, then handle transparency
- [ ] 91.5 **Temporal reprojection with jitter** — see notes
- [ ] 91.6 Local density volumes so fog can be placed, not just global
- [ ] 91.7 Quality tiers driven by settings (T0078)
- [ ] 91.8 Compare against `EpipolarLightScattering` for the sun case — see notes

## Notes / findings

**Diligent already ships `EpipolarLightScattering`**, which produces sun shafts
specifically and is what the old `terrain_lab` used. If the sun is the only light
that needs shafts, that may cover the visually important case at a fraction of the
cost. **Evaluate it before building froxel volumetrics** — this is genuinely worth
an afternoon, because the two solve overlapping problems with very different price
tags.

Froxel volumetrics earn their cost when *point and spot* lights need to produce
beams — a torch in mist, a spotlight in a smoky room — which epipolar scattering
does not do.

**Temporal reprojection is not optional.** A froxel grid dense enough to look
smooth without it is far too expensive. Jitter the sample position per frame and
blend with the reprojected previous frame. The consequence is the usual temporal
artefact set: ghosting behind moving lights, and instability on camera cuts, both
of which need handling rather than discovering.

**Resolution is the main cost lever.** Typical grids are ~160×90×64 — far below
screen resolution, because fog is low frequency. Do not be tempted to raise it to
fix an artefact that is actually temporal.

Transparent surfaces need the fog integral evaluated at their depth, not the
opaque depth behind them, or glass and particles sit visibly outside the fog.
