# T0079 — Lights and per-object light selection

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Created** | 2026-08-03 |

## Why

DiligentFX provides the *pieces* — `PBR_Renderer` consumes up to 16 lights and 8
shadow casters, `ShadowMapManager` manages cascade textures, `EnvMapRenderer`
handles environment lighting — but there is **no lighting system**: no light
components, no deciding which lights affect which object, no allocating shadow
maps across the lights that need them.

Without it, lighting is whatever was baked into an imported glTF, which is not a
usable authoring story.

## Done when

- [ ] Light components: directional, point, spot, with colour, intensity, range
- [ ] Lights are authored in the editor and serialized with the scene
- [ ] Per-object light selection — the most relevant lights within the shader limit
- [ ] Lights respect the illumination layer mask (T0085)
- [ ] Lights are frustum-culled like geometry (T0045)
- [ ] Exceeding the light limit degrades gracefully, never pops or fails

## Subtasks

- [ ] 79.1 Light components and their reflected properties
- [ ] 79.2 Light gathering and frustum culling per frame
- [ ] 79.3 Per-object light selection — see notes, this is the design decision
- [ ] 79.4 Feed lights into `PBR_Renderer`'s frame attribs
- [ ] 79.5 Illumination layer mask applied during selection (T0085)
- [ ] 79.6 Debug visualisation of light bounds and affected objects (T0061)
- [ ] 79.7 Profiling zones for light gathering and selection

## Notes / findings

**Scope:** this ticket is lights and selection only. **Shadows are T0086** and
**environment/IBL is T0087** — lighting is a large area and splitting it keeps
each piece reviewable.


**Per-object light selection is the real design decision.** `PBR_Renderer` takes
a fixed slot count (16 by default), so with more lights in a scene something must
choose. Options: nearest-N per object (simple, pops when the set changes as the
camera moves), tiled/clustered forward (scales far better, significantly more
work), or deferred (changes the whole pipeline shape). **Start with nearest-N and
measure** — but pick the sort criterion carefully, because naive distance sorting
is exactly what causes visible popping.

The old `terrain_lab` app used `EpipolarLightScattering`, so atmospheric
scattering is available in DiligentFX if outdoor scenes matter.
