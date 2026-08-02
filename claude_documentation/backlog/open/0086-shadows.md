# T0086 — Shadow rendering

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Created** | 2026-08-03 |

## Why

Split out of T0079 because shadows are their own substantial problem — resource
management, quality tuning and a whole class of visual artefacts that lighting
otherwise hides.

`ShadowMapManager` in DiligentFX handles cascade *textures*; deciding which lights
get shadows, at what resolution, and making them look right is ours.

## Done when

- [ ] Cascaded shadow maps for the directional light
- [ ] Shadows for point and spot lights, within a budget
- [ ] Shadow-casting respects the shadow mask (T0085)
- [ ] Cascade transitions are not visibly seamed
- [ ] Shadows are stable when the camera moves — no shimmering edges
- [ ] A resolution/quality setting that meaningfully trades cost for quality
- [ ] Shadow passes are individually visible in Tracy (T0030)
- [ ] Peter-panning and acne are tuned, not left to defaults

## Subtasks

- [ ] 86.1 Directional cascades via `ShadowMapManager`
- [ ] 86.2 Cascade split selection, and blending across boundaries
- [ ] 86.3 **Texel snapping for stabilisation** — see notes
- [ ] 86.4 Point light shadows (cube or dual-paraboloid) and spot shadows
- [ ] 86.5 Shadow atlas/array allocation across lights, by importance
- [ ] 86.6 Depth bias and normal-offset tuning
- [ ] 86.7 Filtering — PCF at minimum
- [ ] 86.8 Cull shadow casters per light, not just per camera
- [ ] 86.9 Quality settings wired to project/user config (T0078)

## Notes / findings

**This machinery is also used for visibility, not only for lighting** (T0093).
Vision cones render occlusion maps through the same path. Keep shadow map
rendering and sampling reusable rather than hard-wired to the lighting pass — a
vision source needs the depth map and the sampling function without the shading.


**Shimmering is the artefact that makes shadows look amateur**, and it is caused
by the shadow map's texel grid moving with the camera. The fix is snapping the
light's projection to texel boundaries so the grid stays fixed in world space.
Budget time for it — it is not optional polish, it is the difference between
shadows that look shipped and shadows that look prototyped.

**Acne and peter-panning are a trade-off, not a bug to eliminate.** Too little
bias gives surface acne; too much detaches the shadow from the caster. Normal-offset
bias is usually a better default than constant depth bias.

**Budget by importance, not by count.** Eight shadow-casting lights at full
resolution is a lot of memory and a lot of extra draw calls, since every caster is
drawn again per light. A distant point light deserves a small map or none.

**Shadow casters need their own culling** — the set visible to the *light* is not
the set visible to the camera. Reusing camera culling produces shadows that pop in
as their caster enters view, which is very noticeable.
