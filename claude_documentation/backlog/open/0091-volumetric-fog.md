# T0091 — Volumetric fog and light shafts

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Very Complex |
| **Phase** | 11 — World & environment |
| **Order** | 850 |
| **Created** | 2026-08-03 |
| **Blocked by** | [0088-sky-atmosphere-time-of-day.md](0088-sky-atmosphere-time-of-day.md) — **hard.** `EpipolarLightScattering` takes one directional light, so the sun-shaft case is that ticket's configuration of a component we already vendor. Until it runs, 91.1's comparison has nothing to compare against and the *remaining* need cannot be judged; [0086-shadows.md](0086-shadows.md) — 91.4 samples shadow maps per froxel, and T0086's notes already record this ticket as one of three consumers needing shadow sampling callable **without** the shading pass |
| **Refs** | [../completed/0171-expose-not-replace-sweep.md](../completed/0171-expose-not-replace-sweep.md); [0094-gameplay-extensible-rendering.md](0094-gameplay-extensible-rendering.md) — if this ships, it is a pass on that seam; **D40** |

## Why

**Rescoped 2026-08-08 by T0171 under D40, and it got smaller in a specific way.**

Distance and height fog (T0089) tint the image with depth. **Volumetric** fog is
fog that light actually travels through — god rays through a window, a spotlight
beam made visible, mist that thickens where a light shines into it.

**The sun case is not this ticket's, and that is the change.** Measured:
`EpipolarLightScattering::FrameAttribs` takes **one `const LightAttribs*`**
(`EpipolarLightScattering.hpp:73`) — a single **directional** light. So light
shafts from the sun are a *configuration of a component we already vendor*, and
they belong to **T0088**, which owns that component. Nothing here should
reimplement them, and this ticket should not start until T0088 has, because the
sun case is the visually important one at a fraction of the price.

**What is genuinely left is the expensive half, and upstream has nothing for
it**: beams from **point and spot** lights — a torch in mist, a spotlight in a
smoky room. Epipolar scattering cannot do them at all; there is no froxel
volumetric implementation anywhere in DiligentCore, DiligentFX or
DiligentSamples.

**So this ticket is ⬆️ genuinely ours, and it must be argued rather than
assumed.** D40's rule cuts both ways: reaching "ours" by default is the same
mistake facing the other way. The argument to make is not *"Diligent lacks it"*
— that is established — but *"a game the studio intends to make needs a
spotlight beam badly enough to pay froxel prices"*, and that is a scoping
question with a real answer. **A recorded decline is a legitimate outcome of
this ticket.**

## Blocked, and deliberately

- **On T0088**, hard: until the sun case ships through
  `EpipolarLightScattering`, the *remaining* need cannot be judged, and 91.8's
  comparison has nothing to compare against.
- **On T0086**, for 91.2: injecting light per froxel means sampling shadow maps,
  and T0086's note already records this ticket as one of three non-lighting
  consumers that need shadow sampling callable without the shading pass.

## Done when

- [ ] **91.8 first: the sun case is measured through T0088's component**, and
      what remains is stated in terms of what a scene actually looks like
      without it — not in terms of what is theoretically missing
- [ ] **A decision is recorded**: build froxel volumetrics, or decline with the
      trigger. **Declining is a valid close** and preferable to an open
      Very-Complex ticket nobody starts
- [ ] If built: fog is lit per-volume, so beams appear from **point and spot**
      lights — the case epipolar scattering cannot do
- [ ] If built: density varies in space — height falloff at minimum, ideally
      local volumes
- [ ] If built: temporally stable — no visible noise or crawling
- [ ] If built: correct interaction with transparent surfaces
- [ ] If built: quality tiers, and cost measured and bounded with its own Tracy
      zone (T0030)

## Subtasks

- [ ] 91.1 **Evaluate the sun case against T0088's `EpipolarLightScattering`
      configuration** — first, and with the component actually running. This was
      91.8; it is now the entry point, because the answer decides whether the
      rest of this ticket exists
- [ ] 91.2 **Record the build-or-decline decision**, with the reasoning
- [ ] 91.3 Froxel grid — a view-frustum-aligned 3D volume
- [ ] 91.4 Inject light per froxel, sampling shadow maps (T0086 — which must
      expose sampling without the shading pass)
- [ ] 91.5 Integrate along the view ray for scattering and transmittance
- [ ] 91.6 Apply to opaque, then handle transparency
- [ ] 91.7 **Temporal reprojection with jitter** — see notes; not optional
- [ ] 91.8 Local density volumes so fog can be placed, not just global
- [ ] 91.9 Quality tiers driven by settings (T0078)
- [ ] 91.10 **Add the row before building** — D40. The matrix already carries
      *"froxel volumetrics — ⬆️ upstream absent"* with this ticket as owner

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
