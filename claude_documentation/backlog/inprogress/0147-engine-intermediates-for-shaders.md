# T0147 — Engine intermediates: scene depth, scene colour, and game-fed inputs for shaders

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 458 |
| **Created** | 2026-08-06 |
| **Refs** | **T0161** ([../completed/0161-game-resource-model.md](../completed/0161-game-resource-model.md)) / **D35** — the other side of the two-namespace line is drawn: game-*declared* resources are reflected into the per-module signature, so the engine-*fed* screen resources this ticket adds belong in the engine's base signature under engine names, and `buildModuleSignatureDesc`'s subtraction will automatically leave them alone once they are declared there; [../completed/0141-custom-shader-materials.md](../completed/0141-custom-shader-materials.md) — its Done-when promises intermediates and no subtask delivers the sampled ones; [0093-visibility-and-fog-of-war.md](../open/0093-visibility-and-fog-of-war.md) — the visibility field arrives with it, through this mechanism; [0094-gameplay-extensible-rendering.md](../open/0094-gameplay-extensible-rendering.md) — 94.4/94.5 are the game-fed half of this; [../completed/0046-frame-render-targets.md](../completed/0046-frame-render-targets.md) — every target already carries `BIND_SHADER_RESOURCE`, and design-gaps item 8 flagged the scene-colour seam into it; [0096-hdr-pipeline-and-tonemapping.md](../open/0096-hdr-pipeline-and-tonemapping.md) — sidedness rules; [0106-vfx-sprites-and-flipbooks.md](../open/0106-vfx-sprites-and-flipbooks.md) — soft particles are this ticket's depth read; [0089-fog-and-atmospherics.md](../open/0089-fog-and-atmospherics.md) — fog is another consumer |

## Why

**T0141's Done-when promised it and nothing delivered it:** *"Custom shaders
receive engine intermediates — visibility (T0093), screen position, depth,
world position — not just a finished colour."* Audited 2026-08-06:
`HpSurfaceInput` carries `ScreenPos` (the raw `SV_POSITION`, so the fragment's
own depth) and `WorldPos` — the *computed-in-shader* intermediates. What no
shader can reach is anything **sampled from the frame**: the scene depth
*texture* (what is behind this transparent fragment), the scene *colour*
(refraction, distortion, frosted glass), T0093's visibility, or any texture a
game's own pass produced (T0094).

**T0141 closed 2026-08-06 with that Done-when at `[~]`**, the interpolated half
shipped and the sampled half named as this ticket's — so nothing here waits on
it any more, and nothing there will tick when this lands. **The evidence goes
in this ticket**, and its closure is what makes the promise true; a closed
T0141 will not be edited again.

The consumers are already queued, which is why this is one mechanism and not
four retrofits: T0106.5's soft particles fade against sampled scene depth;
T0089's fog wants depth; the design-gap survey's item 8 records screen
distortion needing scene colour during transparents "exactly as soft particles
need depth"; T0093's whole architectural constraint is a raw per-pixel factor
reaching material shaders; T0094.5 binds a gameplay texture as a material
parameter.

**Godot's shape here (4.7.1, surveyed 2026-08-06)** is the hint-uniform:
`hint_screen_texture`, `hint_depth_texture`, `hint_normal_roughness_texture` —
the first two available on every renderer, the third Forward+-only. That is
the bar: a shader author *declares* the need, the engine supplies the resource
and its validity rules.

## Done when

- [ ] A surface or lighting-stage shader can sample **scene depth** during the
      transparent pass, and the worked example is a depth-fade (the soft
      particle read, proven before T0106 needs it)
- [ ] A shader can sample **scene colour** during the transparent pass — the
      snapshot point in the frame is decided and documented in
      `08-frame-anatomy.md`'s table (it is a new step in the 10.x sequence) —
      and the worked example is a refraction material
- [ ] The **game-fed input** mechanism exists with T0094: a texture a game
      layer produced is reachable from a material shader by declaration, and
      the fog-of-war dim in T0093's scenario is expressible with it
- [ ] Every intermediate documents **when it is valid** — which passes may
      read it, what it contains before its snapshot, and what an opaque-pass
      read of scene colour does (fails loudly, not garbage)
- [ ] D27's arrival rule is honoured and restated: **no `Visibility` field
      until T0093's mechanism exists**, no normal-roughness promise unless
      something produces it — each field names its owning ticket
- [ ] What is deliberately **not** offered is written down: this engine is
      forward-only (D24), so a G-buffer normal-roughness read either gets a
      cheap forward answer or an honest rejection — decided, not silent

## Subtasks

- [ ] 147.1 Depth SRV plumbing: which target, when it is complete, how the
      surface stage declares the read; placement recorded in frame anatomy
- [ ] 147.2 Scene-colour snapshot: where in 10.x it is copied (after opaque +
      sky, before transparents is the obvious point — decide against T0096's
      HDR ordering), full or half resolution decided by measurement
- [ ] 147.3 The contract fields and their validity docs — the
      `HpMaterial.slang` arrival table grows rows with owners, exactly as it
      already does for `ShadowFactor`/`Visibility`
- [ ] 147.4 Game-fed texture slots, designed with T0094.5 rather than beside
      it — one mechanism, referenced both ways
- [ ] 147.5 Worked examples with pixel assertions: depth-fade and refraction
      in the gpu suite
- [ ] 147.6 The normal-roughness disposition (offer a forward-friendly
      answer, or reject with the reasoning)

## Notes / findings

### The design, decided before any of it was built (2026-08-07)

**The frame gained a pass split, and that is the whole shape of this ticket.**
`SceneRenderer::render` submitted the draw list in one walk, in list order, so
a blended surface could be drawn *before* the opaque geometry behind it — and
there was no point in the frame at which "the opaque image" existed. Both
problems have one answer:

| Step | What | Why here |
|---|---|---|
| 10.9a | **Opaque pass** — every primitive whose alpha mode is `Opaque` or `Mask` | |
| 10.9b | **Scene snapshot** — copy the bound colour and depth into the caller's snapshot targets | the only moment the opaque image is complete and no blended surface has touched it |
| 10.9c | **Blend pass** — every primitive whose alpha mode is `Blend` | reads 10.9b |

The split is a correctness fix on its own (transparents now draw after opaques,
which they did not) and it is what makes a snapshot point *nameable*.

**Copy rather than alias, and this was the decision most at risk of "works on
one driver".** The alternative was to bind the depth buffer through a
`TEXTURE_VIEW_READ_ONLY_DEPTH_STENCIL` view during the blend pass and sample
the live attachment — `VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL` permits
exactly that. It was rejected: it needs the *caller* to bind a different DSV for
one of two passes, it makes the depth a shader reads depend on how many blended
surfaces have already drawn into it, and it has no counterpart for colour — so
the engine would have carried two mechanisms with two validity rules. One copy
mechanism, one rule, every backend.

**Two engine names in the base signature, no new samplers.** `g_SceneColour`
and `g_SceneDepth` are `MUTABLE` texture SRVs on the base signature (the ticket's
own Refs argued this, and `buildModuleSignatureDesc`'s subtraction leaves them
alone for free). They are sampled through the **existing** palette
(`HpSamplerLinearClamp`, `HpSamplerPointClamp`), so the immutable-sampler count
is unchanged at 13 and only the sampled-image count moves, 6 → 8.

**The validity rule is enforced at pipeline build, not documented and hoped
for.** A module that names either resource in a pipeline whose alpha mode is not
`Blend` is refused **by name**: `SurfacePipeline::build` returns null, the
renderer substitutes the missing-material checkerboard, and one log line says
which module and why. That is the Done-when's "fails loudly, not garbage",
achieved without any per-pass rebinding — the alpha mode is already in the
`PSOKey`.

**The snapshot costs nothing when nothing wants it, and the demand is exact
rather than one frame late.** The opaque walk *scans* the primitives it skips:
it records whether the blend pass has any work at all and whether any of its
modules either names a screen resource or has never been compiled (unknown
counts as wanting). Only then is the copy issued. A scene with no blended
material never pays; a scene with blended materials that ignore the screen pays
nothing from the second frame and one copy on the first.

**Game-fed textures ride the module signature, by name.** A game layer calls
`setGameTexture("visibility", view)`; a module declares
`Texture2DArray visibility;` exactly as T0161 already allows, and the module SRB
binder resolves the name against the `.hpmat` first and the game feed second.
No new signature, no new declaration syntax — which is the point: T0161's
mechanism was already the right one, and what was missing was a *source* for the
bytes.

### Scene colour has a cost and a trap worth naming now

The snapshot is a full copy of the HDR target once per frame (when any
material declares the need — it should cost nothing when none does, which is a
pipeline-flag question for the material system). The trap is recursion:
a refractive surface sampling scene colour does not see *other* transparents
drawn after it. That is the standard limitation every engine ships (Godot's
screen texture has it too); it is documented, not solved.

### The sidedness rule applies

T0096: "if a pass cannot say which side of the tonemap it is on, that is a
design smell." Scene colour sampled by materials is **pre-tonemap HDR** by
construction here; the docs in 147.3 must say so, because a shader author
porting a Godot LDR trick will otherwise be surprised by values above 1.
