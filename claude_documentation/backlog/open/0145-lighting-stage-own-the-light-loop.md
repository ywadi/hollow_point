# T0145 — The lighting stage: own the light loop, and make the shading model overridable

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 465 |
| **Created** | 2026-08-06 |
| **Blocks** | **T0086** — the shadow lookup lives *inside* the loop this ticket moves, so the loop must land before shadow sampling is written (see "Sequencing"). [T0149](0149-style-bundles.md) — a toon style is a rung-3 override, so styles cannot exist before this |
| **Refs** | **D30** (the decision this executes), **D31** (the mirrored vocabulary), [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md); [../inprogress/0141-custom-shader-materials.md](../inprogress/0141-custom-shader-materials.md) — the surface stage this sits behind; [../inprogress/0142-slang-shader-language.md](../inprogress/0142-slang-shader-language.md) — the interface mechanism; [0143-extended-material-features.md](0143-extended-material-features.md) — its features accumulate *inside* this loop; [0093-visibility-and-fog-of-war.md](0093-visibility-and-fog-of-war.md) — visibility must stay out of light accumulation; T0079 — the light data this loop consumes |

## Why

**The owner's goal (D30):** use Diligent PBR and lights as-is, *or* override
them — wholly or in part. The surface stage (D26/T0141) delivered that for
*what a surface is*; nothing delivers it for *how light responds to it*. Today
`HpSurface.slang` calls `ApplyPunctualLight` in a loop and a game can change
everything about a surface except its shading model.

**The seam does not exist in their code — measured, 2026-08-06.**
`ApplyPunctualLight` (`PBR_Shading.fxh:601–721`) computes range attenuation,
the spot cone, the shadow-map lookup with `FilterShadowMapFixedPCF` (:655),
and then calls **`SmithGGX_BRDF` inline** (:690) for the base layer. There is
no point inside it where a different shading model can be substituted — the
clearcoat layer goes through `ApplyDirectionalLightGGX` (:718), the base layer
does not. The coarse seams *are* thin — `GetBaseLayerLighting` is one line,
`ResolveLighting` four terms — but the per-light one is fused shut.

So the engine mirrors the loop body — attenuation, cone, shadow block, ~120
lines — into its own lighting stage, keeps calling `SmithGGX_BRDF` and friends
as **the default**, and exposes two rungs of D30's ladder:

- **rung 3** — a per-light method: the engine hands in an engine-typed light
  (D31) with attenuation and (once T0086 exists) shadow already resolved, and
  the material answers with diffuse and specular. Godot's `light()`, with more
  than Godot gives it.
- **rung 4** — the whole-stage method: loop, IBL hand-off and resolve, with
  the default being exactly the mirrored standard path.

**Godot's ceiling, verified 2026-08-06 against 4.7.1-stable**, because "like
Godot but more" has to be checked, not assumed:

- `light()` receives `ATTENUATION` with **shadow already folded in**; no raw
  shadow factor exists, and the open proposal asking for one
  (godot-proposals #15040) shows the ceiling is felt.
- The light loop is fixed engine C++; a `light_post()` for cel shading has
  been an open proposal since 2019 (#484). Rung 4 does not exist there.
- Inside `light()` the only type signal is `LIGHT_IS_DIRECTIONAL` — omni and
  spot are indistinguishable.

Rung 3 with a mirrored `HpLight` (which keeps the light's type, range and
cone) plus rung 4 at all is the "more".

## Sequencing — cheaper before T0086, recorded on both sides

The shadow lookup sits inside the loop body this ticket mirrors. If T0086
builds cascades and sampling against Diligent's loop first, the loop move
relocates shadow sampling a second time — the same both-ways argument T0141
recorded for the pixel shader, one level deeper. T0086's header carries the
matching entry. What this ticket does **not** do is build shadows: the
mirrored loop keeps the `ENABLE_SHADOWS` block shape so T0086 drops its
sampling into *our* loop, once.

## Done when

- [ ] The engine's pixel shader iterates lights in **its own loop**, calling
      DiligentFX's BRDF functions as the default — and the rendered output is
      **byte-identical** to the current build (the 141.11/142.3 baseline
      discipline; the frame dumps and printed guard values already exist)
- [ ] A material overriding the **per-light** method renders — the worked
      example is a toon ramp — while attenuation and cone remain
      engine-computed, untouched by the override
- [ ] A material overriding the **whole lighting stage** renders
- [ ] The contract's light and surface types are the **engine's** (D31) — no
      Diligent struct name reaches a game shader, and the conditional-member
      hazard is closed (fields exist even when the feature is off)
- [ ] Lighting **render-mode equivalents are decided deliberately**, not by
      drift: ambient-light-off, receive-shadows-off, and diffuse/specular
      model selection each either exist (as a method default, a material
      datum, or a PSO bit — stated which and why, as 141.15 did) or are
      rejected in writing
- [ ] An **upstream drift guard** exists: a DiligentFX submodule bump that
      changes `ApplyPunctualLight` fails loudly here rather than silently
      diverging from the mirror
- [ ] Register and compile cost of the interface-shaped loop is **measured**
      against the baseline, not assumed
- [ ] No field arrives before its system (D27): no shadow factor until T0086,
      no IBL inputs until T0087 — restated here because this contract is where
      both will land

## Subtasks

- [ ] 145.1 Mirror the punctual loop into the engine's lighting stage:
      attenuation, spot cone, the `ENABLE_SHADOWS` block kept shape-compatible
      for T0086, accumulation into base/sheen/clearcoat kept so T0143's
      features stay reachable. Byte-identical guard green before anything else
      changes
- [ ] 145.2 The mirrored vocabulary (D31): `HpLight` (type, direction, colour,
      range, cone — `float3`s, not `DirectionX/Y/Z`), and the shaded-surface
      struct the stage methods take
- [ ] 145.3 The per-light method with the standard default (rung 3)
- [ ] 145.4 The whole-stage method with the standard default (rung 4); the IBL
      call stays a named seam inside it for T0087 to fill
- [ ] 145.5 Lighting render modes: decide ambient-off, receive-shadows-off,
      BRDF selection — each compile-time or data, recorded per 141.15's
      pattern
- [ ] 145.6 The drift guard: decide the mechanism (a pinned copy of
      `ApplyPunctualLight` compared at build or test time is the obvious one)
      and wire it
- [ ] 145.7 Worked example: a toon-ramp material in the sandbox overriding
      only the per-light method — doubles as T0149's seed style
- [ ] 145.8 Measure: register pressure and pipeline compile time,
      interface-shaped loop vs current, on the gpu suite's existing scenes

## Notes / findings

### The struct economics, measured (2026-08-06)

The per-light hook's natural vocabulary is cheap to promise:
`SurfaceReflectanceInfo` is four primitive fields
(`Common/public/PBR_Common.fxh:362`). The coarse hooks drag
`SurfaceShadingInfo` — 12 fields, nested `BaseLayerShadingInfo` /
`ClearcoatShadingInfo`, and members that exist only under `#if ENABLE_*` —
which is exactly why D31 mirrors rather than re-exports: a re-exported struct
changes shape per permutation, so a game shader reading `.Sheen` compiles on
some materials and not others.

### T0093's constraint applies to this stage and is easy to lose here

Visibility is independent of illumination — a dark room inside a vision cone
is *visible and dark*. Whatever term a game applies for visibility applies
**after** shading, never folded into light accumulation. The stage's shape
must keep that reachable: the resolve step is where a visibility factor can
act, and the per-light method must not be the only place a game can intervene.

### What was rejected (full argument in D30)

Handing games `PBR_Shading.fxh` (D27's trap); toon-by-post-process as the
mechanism (a post pass sees only the summed lighting — it cannot ramp per
light, which is Godot proposal #484's exact complaint); Slang dynamic dispatch
as the transport (works on the pinned compiler, but demands bindless for
textures and pays a per-wave switch — T0151 keeps it as the escape hatch).

### Sources for the Godot claims

Godot 4.7.1-stable docs (`spatial_shader.html`: `light()` built-ins,
`ATTENUATION` "based on distance **or shadow**"); godot-proposals #15040
(expose shadow separately — open), #484 (`light_post()` — open since 2019).
Surveyed 2026-08-06.
