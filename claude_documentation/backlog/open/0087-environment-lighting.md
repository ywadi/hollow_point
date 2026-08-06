# T0087 — Environment lighting, IBL and skybox

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 490 |
| **Created** | 2026-08-03 |
| **Refs** | [0134-pbr-renderer-adoption.md](../completed/0134-pbr-renderer-adoption.md) — **decide whether this ticket configures DiligentFX's IBL or supersedes it.** T0028 adopted `GLTF_PBR_Renderer`, which ships an IBL path; [0145-lighting-stage-own-the-light-loop.md](0145-lighting-stage-own-the-light-loop.md) — 145.4 keeps the IBL call a named seam inside the engine's lighting stage; this ticket fills that seam (and the ambient-off render mode 145.5 decides interacts with 87.8's local ambient control); [0152-winding-convention.md](../inprogress/0152-winding-convention.md) — **T0152's engine half landed first** (D33): IBL baselines are view-dependent, and the winding correction already moved the lit-quad value (211,144,144) → (242,25,25), so any IBL number recorded before it would have needed retaking |

**From T0141 (2026-08-06): this ticket owns a check T0141 could not make.** The
engine now samples metallic/roughness from a material's texture, and the guard
that proves it (`tests/gpu/textured_surface_test.cpp`) can only assert that the
*dielectric* set renders correctly. A metal has no diffuse response, so with
`EnableIBL = false` there is nothing for it to reflect and it renders nearly
black — centre (20, 19, 19) — whether the metallic channel is wired to blue,
to green, or to nothing at all. **When environment lighting lands, extend that
test to assert the metal set is brighter and more specular than the rock**, which
is the first assertion that can distinguish those cases.

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

### A symptom this ticket owns, measured 2026-08-06 on T0159

**The black pixel speckle on the rock cube sample is the N·L clamp, not the
parallax or self-shadow math.** Normal-mapped texels tip past every light's
horizon and clamp to pure black with no ambient to catch them; POM aggravates
it at grazing view by relocating which texels are seen. Measured on the sample
with the self-shadow march forced off: the near-black count is bit-identical
either way — 431 at the test pose, 10000 at yaw 0.9 where it peaks (of ~47k
covered pixels). The scene's `normalScale: 0.8` note documents the same
mechanism. When this ticket lands ambient/IBL, those pixels become dark instead
of black and the speckle disappears — worth re-rendering the sample then as the
before/after.

### Inherited from T0134 / D24 (2026-08-05) — configure DiligentFX's IBL, do not supersede it

**D24 answers this ticket's open question: configure.** The mechanism, so it is
not re-surveyed:

- `PBR_Renderer::CreateInfo::EnableIBL` — currently `false`, which is one of the
  three reasons every mesh renders black.
- `PBR_Renderer::PrecomputeCubemaps(ctx, attribs)` takes an environment map SRV
  and produces the irradiance cube and prefiltered environment map, with
  `GetIrradianceCubeDesc()` / `GetPrefilteredEnvMapDesc()` describing the targets
  and `NumDiffuseSamples` / `NumSpecularSamples` controlling quality.
- `Renderer.IBLScale` (a `float4`) and `Renderer.PrefilteredCubeLastMip` are the
  frame parameters. **`PrefilteredCubeLastMip` is the one field Diligent wants to
  own** — `SetInternalShaderParameters(params, prefilteredEnvMapSRV)` sets it, and
  `SceneRenderer` already calls that with `nullptr`. Passing the real SRV is this
  ticket's change.
- `PSO_FLAG_USE_IBL` must come out of `kFeatureMask` in `SceneRenderer.cpp`.
- `Components/EnvMapRenderer.hpp` draws the skybox itself.

Read [../completed/0134-pbr-renderer-adoption.md](../completed/0134-pbr-renderer-adoption.md) first.

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
