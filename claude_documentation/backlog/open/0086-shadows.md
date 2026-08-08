# T0086 — Shadow rendering

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 480 |
| **Created** | 2026-08-03 |
| **Inherits a decision from** | [0096-hdr-pipeline-and-tonemapping.md](0096-hdr-pipeline-and-tonemapping.md) — **86.7's four filter modes are selected by a compile-time macro (`SHADOW_MODE`), not a constant-buffer field**, exactly like `TONE_MAPPING_MODE`. T0096 is deciding how this engine handles that class of switch (one PSO per value, a project-wide compile-time choice, or a runtime branch); take its answer rather than inventing a second. T0155's `TEXTURING_MODE` is the third instance |
| **Blocked by** | [../completed/0141-custom-shader-materials.md](../completed/0141-custom-shader-materials.md) **141.10** — the standard material shader. **141.0 is decided (D26): the engine owns the surface stage and Diligent is never modified**, so shadow sampling must be written against *our* pixel shader rather than `RenderPBR.psh`. Waiting is what stops it being built twice. **And T0145** (2026-08-06, D30): the shadow lookup (`FilterShadowMapFixedPCF`, `PBR_Shading.fxh:655`) sits *inside* `ApplyPunctualLight`, which T0145 mirrors into the engine's own lighting stage — its loop must land first, or shadow sampling is written against their loop and moved immediately after |
| **Refs** | T0078, T0085, T0091, T0092, T0093, [../completed/0060-material-system.md](../completed/0060-material-system.md) — needs **cutout** materials (`AlphaMode::Mask`) for alpha-tested shadow casters, delivered by 60.1. **[T0152](../completed/0152-winding-convention.md) blocks this ticket (D33, 2026-08-06).** 141.12's inversion finding was corrected: the engine's chain is glTF-conformant with `FrontCounterClockwise = false`; the *test assets* were wound backwards and the `CULL_FRONT` workaround was anti-conformant. T0152's engine half has landed — the convention is declared (`WindingConvention.hpp`), the assets are re-wound and the cull reverted to `BACK` — so **"cull front faces in the shadow pass" now means what every published bias recipe means by it.** Note the lit-quad baseline moved (211,144,144) → (242,25,25) in that correction: any bias tuned against the old value was calibrated on an inversion; **[T0145](../completed/0145-lighting-stage-own-the-light-loop.md) landed the loop the shadow lookup goes inside, and constrains this ticket in three ways** (D38). (1) The `ENABLE_SHADOWS` block shape is preserved in `HpGetLight` in `engine/shaders/HpSurface.slang` and currently `#error`s — that is where the cascade lookup is written, once, rather than against DiligentFX's loop and then relocated. (2) **The shadow factor arrives as its own `HpLight` field, never multiplied into `Attenuation`** — folding them is exactly Godot's ceiling (proposal #15040, open) and separating them is a specific claim D30 makes about being 'more than Godot'. (3) `HpLight::Index == 0` is the documented dominant light, so 'shadow the dominant light only' is expressible without a material guessing an index. Also: whether a *standard* material needs a `Material::receiveShadows` datum is this ticket's call, decided in D38 as data rather than a PSO bit. **T0152.5 (landed 2026-08-07) constrains the shadow pass one more way**: facing is per-draw, not per-material — a negative-determinant node flips the cull face (`SceneRenderer`'s `singleSidedCull`, from the node matrix's upper-3×3 determinant sign), and the shadow pass's cull-as-bias-policy must apply the same flip per draw or a mirrored caster's bias inverts exactly the way the main pass would have without it; **T0165** — the handedness and winding conventions are now **settled** (right-handed camera, `kFrontFaceCounterClockwise = true`), which is what T0152 blocked this ticket for. **Choose the shadow-pass cull face against that constant, never a literal**, and re-read `WindingConvention.hpp`'s item 4 before tuning a single bias value: a bias tuned against the wrong convention bakes the inversion into every tuned number permanently. The per-draw determinant rule (T0152.5) applies to the shadow pass too — a mirrored node's shadow caster culls the other way. |

**From T0142/D28 (2026-08-06): the material contract is Slang now.** `ShadowFactor`
is the next field the contract gains, and `HpMaterial.slang` names this ticket as
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

**Rescoped 2026-08-08 by T0171 under D40. Read this section before the notes
below — several of them were written against the old scope and say so.**

Shadows split in two, and the halves have completely different costs.

**The shading half is upstream's, in full, and the call site is already ours.**
Measured against the pinned submodule:

- **`DiligentFX/Shaders/Common/public/Shadows.fxh` is a complete cascaded
  shadow-shading library**, not a filter helper. `FindCascade` (`:65`) selects
  the cascade; `FilterShadowMap` (`:219`) filters it and, under
  `FILTER_ACROSS_CASCADES`, **blends across the boundary** (`:238-250`) — which
  is 86.2's second half, written; `SampleFilterableShadowMap` (`:350`) is the
  VSM/EVSM path. The four modes are `BasicStructures.fxh:19-22`.
- **`ShadowMapManager`** (`DiligentFX/Components/interface/ShadowMapManager.hpp`
  — *not* DiligentCore, which this ticket and the capability matrix both said)
  owns the cascade textures and their placement. `DistributeCascades` (`:165`)
  takes `SnapCascades` and `StabilizeExtents` as **booleans** (`:108`, `:112`) —
  so 86.3, *"texel snapping is the difference between shadows that look shipped
  and shadows that look prototyped"*, is a flag rather than a subsystem — plus
  `fPartitioningFactor` (`:120`), two range/centre adjustment callbacks,
  `ConvertToFilterable` (`:169`) for the VSM/EVSM representations, and
  `GetCascadeDSV` (`:88`), which is the target our depth pass renders into.

**The one thing that is *not* true, and it changes this ticket's mechanism:
`PBR_Renderer`'s own shadow path is neither of those.** `PBR_Shading.fxh`
includes `PCF.fxh` and nothing else (`:86-89`) and calls
`FilterShadowMapFixedPCF` against **one array slice per light** (`:645-656`),
with no cascade selection anywhere in the file. `PBRLightAttribs::ShadowMapIndex`
and `PBRShadowMapInfo` feed *that* path. **The two shadow implementations in
DiligentFX are not joined to each other** — the cascaded one is the Shadows
sample's, the single-slice one is the PBR renderer's.

**That is what makes this ticket cheap rather than expensive, and it is a
consequence of owning the surface shader.** Because `HpGetLight` is ours, we can
call the good library from inside our own light loop instead of inheriting the
weak one that came bolted to `RenderPBR.psh`. **Note this corrects the comment
sitting in the seam**: `HpSurface.slang:871` names `FilterShadowMapFixedPCF`
(`PBR_Shading.fxh:655`) as the thing being mirrored, because T0145 wrote it
while mirroring that loop. The call this ticket writes is `Shadows.fxh`'s
`FilterShadowMap` / `SampleFilterableShadowMap`.

**The engine's half is the part Diligent structurally cannot supply**, and it is
all *system*, none of it shading:

1. **The depth-only pass from the light.** Diligent has no scene graph and no
   traversal — that is the engine's by definition (D40), and it is the same walk
   `SceneRenderer` already performs, with a different camera and no shading.
2. **Allocation policy** — which lights get maps, at what resolution, by
   importance rather than by count.
3. **The caster cull** — the set visible to the *light*, not to the camera
   (T0045's frustum test called with a different frustum).
4. **`ShadowFactor` on `HpLight`**, filled in the reserved block.

**And one thing upstream genuinely does not have: point and spot shadows.**
`ShadowMapManager` is directional-only — `DistributeCascadeInfo::pLightDir`
(`:106`) is a single direction, and there is no cube or dual-paraboloid path
anywhere in DiligentFX. 86.4 is therefore the only part of this ticket that is
built from scratch, and it should be **scoped or declined deliberately** rather
than inherited from the old list.

## The seam, in `IHpMaterial`'s terms

**There is no new seam and this ticket must not invent one.** The surface seam
is `IHpMaterial` and it is built: a game overrides `light()` to receive an
`HpLight` per light, and **the default implementation is the standard shadowed
path**. What this ticket adds is a field on that struct.

- **What a game overrides**: `light()`, exactly as today.
- **What the default is**: the cascaded lookup, with the project's chosen
  `SHADOW_MODE`.
- **What arrives**: `HpLight::ShadowFactor`, **its own field, never multiplied
  into `Attenuation`** (D38 — folding them is Godot's ceiling and separating
  them is a specific claim D30 makes).
- **Which mode is used is a *setting*, not a seam** — see D40: a game developer
  picks PCF/VSM/EVSM2/EVSM4 out of quality settings (T0078); they do not
  implement one.

## Done when

- [ ] **Cascaded directional shadows render, with `ShadowMapManager` and
      `Shadows.fxh` doing the cascade maths** — and the ticket names what was
      written here instead of taken, with the reason (D40's required artefact)
- [ ] `HpLight::ShadowFactor` exists, is separate from `Attenuation`, and a
      game's `light()` override can read it — with the generated shader
      reference (`docs/shaders/`) regenerated to carry it
- [ ] **All four `SHADOW_MODE_*` values are selectable as a setting**, and the
      *price* of each is measured rather than assumed — VSM/EVSM need
      `ConvertToFilterable`'s separate filterable texture
      (`InitInfo::Is32BitFilterableFmt`), so each is real memory and a real pass
- [ ] Shadow-casting respects the shadow mask (T0085), which is separate from
      the illumination mask
- [ ] Cascade transitions are not visibly seamed — via `FILTER_ACROSS_CASCADES`,
      not a hand-written blend
- [ ] Shadows are stable when the camera moves — via `SnapCascades` /
      `StabilizeExtents`, with a *measurement* that they are on and working
- [ ] Depth bias and normal-offset are tuned **against T0165's settled
      convention** and against a scene with an ambient floor (T0087's IBL
      landed, so a shadowed pixel is no longer pure black)
- [ ] **Point and spot shadows are dispositioned** — built, or declined with the
      cost written down. Not left as a silent maybe
- [ ] Shadow passes are individually visible in Tracy (T0030)
- [ ] A resolution/quality setting that meaningfully trades cost for quality

## Subtasks

- [ ] 86.1 **The depth-only pass from the light** — the engine's walk, rendering
      into `ShadowMapManager::GetCascadeDSV(i)`. This is the substantive new
      code in this ticket
- [ ] 86.2 **Construct and drive `ShadowMapManager`**: `Initialize`,
      `DistributeCascades` per frame, `ConvertToFilterable` when the mode needs
      it. Fill `ShadowMapAttribs`. **Check `UseRightHandedLightViewTransform`
      against T0165** — the engine is right-handed now and this is a flag that
      will silently produce a plausible wrong answer
- [ ] 86.3 **Write the `ENABLE_SHADOWS` block in `HpGetLight`** — a call to
      `FilterShadowMap` / `SampleFilterableShadowMap`, plus `ShadowFactor` on
      `HpLight` in the same change (D27: no field before its system).
      **Correct `HpSurface.slang:871`'s comment while you are there**
- [ ] 86.4 **Cull shadow casters per light** — call T0045's frustum test with
      the light's frustum. If T0045 has not landed, this is the constraint it
      inherits: *culling must be callable with an arbitrary frustum*
- [ ] 86.5 **Allocation across lights by importance**, and the `ShadowMapIndex`
      linkage. Note this is the one place `PBRLightAttribs::ShadowMapIndex`
      still matters even though we do not use upstream's sampling path
- [ ] 86.6 Depth bias and normal-offset tuning — **last**, and against a scene
      with ambient
- [ ] 86.7 **Expose all four filter modes as a setting** (T0078), with the
      memory and pass cost of each measured. Owner's decision 2026-08-06:
      *"we need to have all since they all exist as an option to the game dev"*
- [ ] 86.8 **Point and spot shadows: decide.** Upstream has nothing here.
      Cube or dual-paraboloid, or declined for now with the trigger recorded
- [ ] 86.9 Quality settings wired to project/user config (T0078)
- [ ] 86.10 **Add the row to `12-vendored-capabilities.md` before writing any
      of it** — D40's rule, and this ticket's own scope changed once already
      because nobody had

## Notes / findings

### 2026-08-08, T0171 — what this rescope supersedes, so the older notes are not read as current

The `Why`, `Done when` and `Subtasks` above were rewritten under **D40**. Two
notes below are now wrong in part and are kept because their *other* halves are
still load-bearing:

- **"Inherited from T0134 / D24 — the shadow plumbing already exists"** is about
  `PBR_Renderer`'s single-slice PCF path. `EnableShadows`,
  `MaxShadowCastingLightCount`, `PCFKernelSize`, `PBRShadowMapInfo` and
  `PSO_FLAG_ENABLE_SHADOWS` are all real and all belong to the path we are **not
  sampling through**, because we own the pixel shader and can call the cascaded
  library instead. What survives from it: `PBRLightAttribs::ShadowMapIndex` is
  still the per-light linkage (86.5), and the frame-attribs sizing still has to
  be right if the flag is ever raised.
- **"From T0145/D30 — your sampling call site moves into our loop"** is entirely
  correct and is the reason this ticket is now cheap. Only the *function named*
  in the seam's comment changed.

**The measurement that produced the rescope**, so it is not re-derived: three
separate greps of the pinned submodule established that `Shadows.fxh` (cascade
selection, cross-cascade blend, four filter modes) and `PBR_Shading.fxh`
(one slice, fixed PCF, no cascades) are **two unconnected implementations**, and
that `ShadowMapManager` is **directional-only**. Paths and line numbers are in
the `Why` above and in
[`12-vendored-capabilities.md`](../../documentation/12-vendored-capabilities.md).

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
- `CreateInfo::PCFKernelSize` (default 3) is the *PCF* filtering knob — one of
  four modes, see 86.7. The full surface (cascade distribution, snapping,
  stabilisation, partitioning factor, per-light bias) is inventoried in
  [../../documentation/12-vendored-capabilities.md](../../documentation/12-vendored-capabilities.md);
  read it before building any of it.
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

### From T0145/D30 (2026-08-06) — your sampling call site moves into our loop

T0145 mirrors `ApplyPunctualLight`'s body (attenuation, cone, the
`ENABLE_SHADOWS` block, kept shape-compatible for exactly this handoff) into
the engine's lighting stage. Build cascades and filtering as planned, but the
per-fragment lookup is written **once, in the engine's loop** — and the raw
shadow factor becomes a contract field on the per-light method when this
ticket lands (D27's arrival rule; it is the field Godot cannot expose, see
T0145's notes).

### Cross-ticket obligations (2026-08-04, T0124 backfill)

The notes above record T0093 reusing this machinery for visibility. Two more
non-lighting consumers have since been filed, which hardens that note into a
design rule:

- **T0091.2** samples shadow maps per froxel for volumetric light injection.
- **T0092.5**'s rain-occlusion map is "a shadow map with the sky as the
  light", rendered and sampled with no shading at all.

Keep shadow-map rendering and sampling callable without the lighting pass, or
each of the three consumers rebuilds the machinery with its own bugs.
