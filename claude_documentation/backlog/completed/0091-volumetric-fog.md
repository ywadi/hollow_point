# T0091 — Volumetric fog and light shafts

| | |
|---|---|
| **Status** | ❌ SUPERSEDED |
| **Priority** | Medium |
| **Complexity** | Very Complex |
| **Phase** | 11 — World & environment |
| **Order** | 850 |
| **Created** | 2026-08-03 |
| **Superseded** | 2026-08-08 by **[T0088](../open/0088-sky-atmosphere-time-of-day.md)** — *"The atmosphere: sky, fog, light shafts and time of day"*. **Nothing was dropped**: the mapping is below and this ticket's `## Notes / findings` is preserved **verbatim** in T0088 under *Absorbed from T0091*, because those notes are what make **88.20** buildable if its trigger ever fires |

## Why this was absorbed rather than left open

**This ticket was two things, and measurement moved one of them and shrank the
other.**

**The sun case was never this ticket's.**
`EpipolarLightScattering::FrameAttribs` takes a single `const LightAttribs*`
(`EpipolarLightScattering.hpp:73`) — **one directional light**. So light shafts
from the sun are a *configuration of a component T0088 owns*, not a system to
build here. This ticket already suspected it, in its own words: *"If the sun is
the only light that needs shafts, that may cover the visually important case at
a fraction of the cost. **Evaluate it before building froxel volumetrics** —
this is genuinely worth an afternoon."* That evaluation can only be run from
inside the ticket that constructs the component, which is why it moved rather
than being repeated.

**What remained is a single deferred decision, and it belongs inside its natural
owner rather than in a ticket of its own.** Beams from **point and spot**
lights — a torch in mist, a spotlight in a smoky room — are ⬆️ genuinely ours:
there is no froxel volumetric implementation anywhere in DiligentCore, DiligentFX
or DiligentSamples. But **D40 cuts both ways**, and reaching "ours" by default is
the same mistake facing the other way. The honest likely outcome is a **recorded
decline**, and a Very Complex ticket whose first subtask is *"decide whether this
ticket should exist"* is precisely the kind of thing that makes a backlog feel
like a mess.

**The precedent for how it is kept is T0146.7's tessellation**: a large deferred
capability living as a subtask with a **written trigger** inside the ticket that
naturally owns it. That is **T0088.20**.

## Where everything went

| From here | Now |
|---|---|
| **Compare against `EpipolarLightScattering` for the sun case** (91.8, promoted by T0171 to 91.1) | **T0088.19** — and it is now *reachable*, because the same ticket constructs the component |
| The build-or-decline decision | **T0088.20**, with the trigger written down and a decline recorded as a legitimate close |
| Froxel grid, light injection, ray integration, opaque-then-transparent application, temporal reprojection, local density volumes, quality tiers (91.1–91.9) | **T0088.20**, as the contents of the deferred capability — all of it named so nothing has to be re-derived |
| Shadow-map sampling per froxel | **T0088.20**, and the obligation it places on **T0086** is unchanged and still recorded there: shadow-map rendering and sampling must be callable **without** the lighting pass. T0086's notes name three such consumers |
| Shared parameters with distance/height fog | **T0088.15** — one parameter set, which was the point of T0089's 89.7 |

## The two facts most worth not re-deriving

- **Temporal reprojection is not optional.** A froxel grid dense enough to look
  smooth without it is far too expensive. The consequence is the usual temporal
  artefact set — ghosting behind moving lights, instability on camera cuts — and
  both need handling rather than discovering.
- **Resolution is the main cost lever, and raising it is usually the wrong fix.**
  Typical grids are ~160×90×64, far below screen resolution, because fog is low
  frequency. An artefact that looks like insufficient resolution is usually
  temporal.
