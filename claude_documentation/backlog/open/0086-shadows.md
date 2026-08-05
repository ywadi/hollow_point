# T0086 — Shadow rendering

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 480 |
| **Created** | 2026-08-03 |
| **Blocked by** | [../inprogress/0141-custom-shader-materials.md](../inprogress/0141-custom-shader-materials.md) **141.10** — the standard material shader. **141.0 is decided (D26): the engine owns the surface stage and Diligent is never modified**, so shadow sampling must be written against *our* pixel shader rather than `RenderPBR.psh`. Waiting is what stops it being built twice |
| **Refs** | T0078, T0085, T0091, T0092, T0093, [../completed/0060-material-system.md](../completed/0060-material-system.md) — needs **cutout** materials (`AlphaMode::Mask`) for alpha-tested shadow casters, delivered by 60.1 |

**From T0142/D28 (2026-08-06): the material contract is Slang now.** `ShadowFactor`
is the next field the contract gains, and `HpMaterial.fxh` names this ticket as
its owner. Under D28 that field is added to the **`IHpMaterial` interface** as a
method with a default implementation, not to an HLSL struct — and the D27 rule
still governs it: *nothing is exposed before the system behind it exists*, so the
field arrives **with** working shadows and not before. A `ShadowFactor` that
returns 1.0 because this ticket has not landed is worse than no field, because a
game shader written against it fails silently.

Also note `EnableShadows` is currently **false** in `SurfacePipeline::configure`,
and turning it on needs three things moved together — the `CreateInfo` flag, the
PSO flag, and the shadow-map bindings. T0143's notes record what happens when
only one of the three moves.

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

### Inherited from T0085 (2026-08-05) — the shadow-casting mask is yours

**85.5 moved here.** An object can be **lit by a light without casting its
shadow** — common for characters, foliage and anything with a cheap fake shadow —
so illumination and shadow casting need **two** masks, not one.

`hp::LayerMask` is the type; use it rather than a bare `uint32_t`. T0079 owns the
illumination mask on the light; this ticket owns the casting mask, and the two
must stay separable. Note `PBRLightAttribs::ShadowMapIndex` (D24) is already the
per-light linkage. See [../completed/0085-layers-and-masks.md](../completed/0085-layers-and-masks.md).

### Inherited from T0134 / D24 (2026-08-05) — the shadow plumbing already exists

Recorded so this ticket does not re-survey what T0134 enumerated. Per D24, the
engine **configures** DiligentFX here rather than superseding it:

- `PBR_Renderer::CreateInfo::EnableShadows` and `MaxShadowCastingLightCount`
  (default 8) — both currently off/unused.
- `PBRShadowMapInfo` occupies a tail region of the frame-attribs buffer, sized by
  `GetPRBFrameAttribsSize(LightCount, ShadowCastingLightCount)`.
- `PBRLightAttribs::ShadowMapIndex` is **already in the light struct** T0079 will
  populate — `-1` for a light that casts none. So the light/shadow linkage is
  decided; this ticket allocates the maps and fills the indices.
- `CreateInfo::PCFKernelSize` (default 3) is the filtering knob.
- `Components/ShadowMapManager.hpp` manages cascade textures.
- `PSO_FLAG_ENABLE_SHADOWS` must come out of `kFeatureMask` in
  `SceneRenderer.cpp`.

Read [../completed/0134-pbr-renderer-adoption.md](../completed/0134-pbr-renderer-adoption.md) first.

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

### Cross-ticket obligations (2026-08-04, T0124 backfill)

The notes above record T0093 reusing this machinery for visibility. Two more
non-lighting consumers have since been filed, which hardens that note into a
design rule:

- **T0091.2** samples shadow maps per froxel for volumetric light injection.
- **T0092.5**'s rain-occlusion map is "a shadow map with the sky as the
  light", rendered and sampled with no shading at all.

Keep shadow-map rendering and sampling callable without the lighting pass, or
each of the three consumers rebuilds the machinery with its own bugs.
