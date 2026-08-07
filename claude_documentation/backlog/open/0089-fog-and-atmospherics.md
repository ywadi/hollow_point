# T0089 — Fog and atmospherics

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 11 — World & environment |
| **Order** | 840 |
| **Created** | 2026-08-03 |
| **Refs** | **T0147** ([../completed/0147-engine-intermediates-for-shaders.md](../completed/0147-engine-intermediates-for-shaders.md)) / **D37** — **the depth read fog wants exists**: `HpSceneViewDepth(In.ScreenUV)` gives view-space distance in metres from the opaque pass's depth, and `HpViewDepth` linearises any device depth without this ticket needing to know the projection's convention. Two things follow. A **per-material** fog is a blended surface today and needs nothing else. A **frame-wide** fog is a full-screen pass and belongs with T0148's chain, reading the same `scene.colour.snapshot`/`scene.depth.snapshot` targets rather than declaring its own — the two must not each copy the frame. Note also that vendored DiligentFX ships atmospheric scattering (see `12-vendored-capabilities.md`), which is the first thing this ticket should evaluate |

## Why

Fog does a disproportionate amount of visual work: it conveys depth and scale,
hides the far clip plane and LOD transitions, and is the main lever for mood. It
is also what makes weather (T0090) read as weather rather than as particles.

Nothing currently covers it.

## Done when

- [ ] Distance fog with controllable colour, density and falloff
- [ ] Height fog, so low-lying mist is possible
- [ ] Fog colour can derive from the sky rather than being set independently
- [ ] Parameters are scene-authored and serialized, and animatable at runtime
- [ ] Fog is applied correctly relative to transparency and to the skybox
- [ ] Feeds volumetric fog (T0091) rather than duplicating it

## Subtasks

- [ ] 89.1 Fog parameters as scene settings (T0078)
- [ ] 89.2 Distance fog in the shading path — linear and exponential falloff
- [ ] 89.3 Height fog
- [ ] 89.4 Derive fog colour from the sky/sun direction (T0088)
- [ ] 89.5 Correct interaction with transparent surfaces and the skybox
- [ ] 89.6 Runtime animation of parameters, for weather transitions (T0090)
- [ ] 89.7 Keep parameters shared with volumetric fog (T0091), not duplicated

## Notes / findings

**From D32 (2026-08-06):** when the fog shader is built, it is authored
against an interface a game can implement — the same default-methods shape as
`IHpMaterial` — not as a sealed engine file. One design constraint now; the
retrofit D27 exists to prevent, later.

**Transparency plus fog is where this goes wrong.** Fog applied per-pixel on
opaque geometry is straightforward; transparent surfaces need it applied
consistently or they visibly float out of the fog. Decide the approach when the
transparent queue is built (T0045), not afterwards.

**Fog colour should usually follow the sky.** A fixed grey fog under a sunset sky
looks immediately wrong. Sampling the sky in the view direction is a cheap and
large improvement, and it is what makes T0088's time of day feel cohesive.

**Volumetric fog is wanted and is now T0091** — froxel grids, light injection and
temporal reprojection are a large enough job to be their own ticket. This one
stays deliberately cheap: distance and height fog cost almost nothing and cover
the common case, and they remain the fallback on lower quality settings where
volumetrics are disabled. The two should share parameters (colour, density,
height falloff) rather than each having their own.

Fog also usefully hides LOD transitions (T0040) and the far plane, which makes it
a performance tool as much as an aesthetic one.
