# T0087 — Environment lighting, IBL and skybox

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
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
