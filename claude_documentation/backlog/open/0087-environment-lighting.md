# T0087 — Environment lighting, IBL and skybox

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 490 |
| **Created** | 2026-08-03 |

## Why

Split out of T0079. Punctual lights alone produce harsh, unconvincing PBR —
surfaces facing away from every light go black. Image-based lighting from an
environment map is what makes physically-based materials actually look right, and
it is largely provided already by `EnvMapRenderer`.

## Done when

- [ ] A skybox renders behind the scene
- [ ] Environment maps provide diffuse irradiance and specular reflection
- [ ] The environment is authorable per scene as an asset
- [ ] Ambient intensity is controllable, so it can be tuned per scene
- [ ] Prefiltering happens offline or once at load, never per frame
- [ ] Materials without an environment still render sensibly
- [ ] The skybox is in the right place in the render order

## Subtasks

- [ ] 87.1 Environment map as an asset (HDR cubemap or equirectangular)
- [ ] 87.2 Skybox rendering via `EnvMapRenderer`
- [ ] 87.3 Irradiance and prefiltered specular generation
- [ ] 87.4 Cache prefiltered results so they are not recomputed each load
- [ ] 87.5 Scene-level environment settings, serialized
- [ ] 87.6 Feed IBL inputs into `PBR_Renderer`
- [ ] 87.7 Consider a procedural sky, given `EpipolarLightScattering` exists
- [ ] 87.8 Disposition local ambient control -- interiors under a scene-global
      IBL -- before scenes are lit (see the 2026-08-03 amendment)

## Notes / findings

**`EnvMapRenderer` and `PBR_Renderer` already implement most of this** — the work
is asset plumbing and authoring, not writing IBL. Check what prefiltering Diligent
provides before implementing any of it.

**Prefiltering is expensive and must be cached.** Generating irradiance and
specular mip chains per scene load is a visible stall. Compute once and store
alongside the asset — this is a natural fit for the binary cook step in T0020.

**Skybox draw order:** after opaque geometry with depth testing on, so it only
fills unwritten pixels. Drawing it first wastes fill rate on every pixel later
covered by geometry.

DiligentFX ships `EpipolarLightScattering`, which the old `terrain_lab` used — so
a procedural atmosphere is available if outdoor scenes matter, rather than a fixed
cubemap.

### Amendment (2026-08-03) -- interiors under a global IBL have no answer

The design-gap survey (`documentation/07-design-gaps.md`, item 6) found
`lightmap`, `light probe`, `reflection probe` and `global illumination` at zero
hits in every sense. The lighting stack is punctual lights (T0079), shadows
(T0086), and **one environment map per scene** with a global ambient intensity
(this ticket). The consequence: a single scene-global IBL illuminates interiors
with sky light -- a basement is lit by the same environment as the street above
it. Every engine with authored indoor/outdoor spaces grows *some* answer, and
this backlog had none, not even a rejection.

T0093's visibility decision raises the stakes beyond cosmetics: "a dark room
inside the vision cone is *visible and dark* -- lit by whatever lights exist",
so interior light levels are gameplay-legible.

The failure mode if this waits is the one T0096 names for colour-space bugs:
lights get tuned per scene to compensate for ambient leakage, then every one of
those scenes needs re-tuning when a real mechanism lands. Hence 87.8:
**disposition a cheap first answer before scenes are lit.** The candidate is an
ambient/IBL-intensity *volume* or per-room scalar applied where 87.6 feeds IBL
inputs into `PBR_Renderer` -- deliberately not probes, not lightmaps, not GI,
none of which should be built speculatively. "Rejected, scenes will be lit to
tolerate it" is also an acceptable disposition; silence is not.

A single all-outdoor or all-interior game would need only half of this. The
engine does not get to assume that, because the next game is the one that needs
the other half. **This ticket owns both**, and the live question is scheduling:
which half is built first, and whether the transition between them is designed
in now -- cheap -- or retrofitted later, which is not.
