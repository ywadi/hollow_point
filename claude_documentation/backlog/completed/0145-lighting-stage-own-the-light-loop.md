# T0145 — The lighting stage: own the light loop, and make the shading model overridable

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 465 |
| **Created** | 2026-08-06 |
| **Blocks** | **T0086** — the shadow lookup lives *inside* the loop this ticket moves, so the loop must land before shadow sampling is written (see "Sequencing"). [T0149](../open/0149-style-bundles.md) — a toon style is a rung-3 override, so styles cannot exist before this |
| **Refs** | **D30** (the decision this executes), **D31** (the mirrored vocabulary), [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md); [../completed/0141-custom-shader-materials.md](../completed/0141-custom-shader-materials.md) — the surface stage this sits behind; [../completed/0142-slang-shader-language.md](../completed/0142-slang-shader-language.md) — the interface mechanism; [0143-extended-material-features.md](../completed/0143-extended-material-features.md) — its features accumulate *inside* this loop; [0093-visibility-and-fog-of-war.md](../open/0093-visibility-and-fog-of-war.md) — visibility must stay out of light accumulation; T0079 — the light data this loop consumes; [../completed/0159-open-the-material-contract.md](../completed/0159-open-the-material-contract.md) — **landed before this, as required**: the hooks are `[mutating]`, so the per-light method this ticket adds can read state the surface stage cached. Two constraints from it: **the order of `g_Frame.Lights[]` is unspecified for directional lights** (equal sort keys through an unstable sort — measured, the fill arrived first), so this ticket must either document an ordering or provide a dominant-light convention; and the rock sample's shadow march caps its bite by the marched light's *share* of incident light because a whole-output hook cannot darken one light's contribution — the per-light method here is what removes that approximation (158.3); **T0146** ([../completed/0146-vertex-stage-hook.md](../completed/0146-vertex-stage-hook.md)) — **the rung below, and it changed the file this one edits.** `HpSurface.slang` now carries two entry points, `vsMain` and `psMain`, compiled on one request; the lighting stage belongs in the pixel half and must leave the vertex half alone. It also settled that a game's stages are **methods on one module**, not separate modules — so the lighting hook is another `IHpMaterial` method and adds no permutation axis, which is what the capability matrix's 'permutation multiplier' note now says for the vertex half |

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

- [x] The engine's pixel shader iterates lights in **its own loop**, calling
      DiligentFX's BRDF functions as the default — and the rendered output is
      **byte-identical** to the current build (the 141.11/142.3 baseline
      discipline; the frame dumps and printed guard values already exist).
      *Every numeric line the 57-case gpu suite prints is unchanged except
      timings, on both targets; and a dedicated case compares a
      standard-material frame against the same scene through the hook at
      **0 differing bytes** of 49152*
- [x] A material overriding the **per-light** method renders — the worked
      example is a toon ramp — while attenuation and cone remain
      engine-computed, untouched by the override. *Cel: 3 distinct colours
      against a smooth control's 99. Cone: the override reads no geometry at
      all, and a narrow spot still leaves the corner **exactly** (0, 0, 0)
      where a wide one lifts it to (88, 88, 88)*
- [x] A material overriding the **whole lighting stage** renders. *A rung-4
      module painting the light count and summed attenuation reads exactly
      (188, 255, 0) for two directional lamps*
- [x] The contract's light and surface types are the **engine's** (D31) — no
      Diligent struct name reaches a game shader, and the conditional-member
      hazard is closed (fields exist even when the feature is off). *`HpLight`,
      `HpShadedSurface` and `HpLightResponse` carry **no** `#if` between them,
      and the extended features that would have introduced one are refused
      rather than conditionally present*
- [x] Lighting **render-mode equivalents are decided deliberately**, not by
      drift: ambient-light-off, receive-shadows-off, and diffuse/specular
      model selection each either exist (as a method default, a material
      datum, or a PSO bit — stated which and why, as 141.15 did) or are
      rejected in writing. *D38's table, plus `vertex_lighting` and `unshaded`
      for completeness*
- [x] An **upstream drift guard** exists: a DiligentFX submodule bump that
      changes `ApplyPunctualLight` fails loudly here rather than silently
      diverging from the mirror. *And it was **demonstrated failing** before it
      was trusted — see "The drift guard, shown to work" below*
- [x] Register and compile cost of the interface-shaped loop is **measured**
      against the baseline, not assumed. *Standard material: SPIR-V identical
      to the byte. Custom module: 124 bytes smaller. Compile and fill cost:
      inside the run-to-run spread*
- [x] No field arrives before its system (D27): no shadow factor until T0086,
      no IBL inputs until T0087 — restated here because this contract is where
      both will land. *Neither field exists, and both are enforced rather than
      remembered: `#if ENABLE_SHADOWS` and `#if USE_IBL` each `#error` in the
      lighting stage, so the flag cannot be turned on without writing the term.
      Two-way Refs added to T0086 and T0087 saying where*

## Subtasks

- [x] 145.1 Mirror the punctual loop into the engine's lighting stage:
      attenuation, spot cone, the `ENABLE_SHADOWS` block kept shape-compatible
      for T0086, accumulation into base/sheen/clearcoat kept so T0143's
      features stay reachable. Byte-identical guard green before anything else
      changes. **One deliberate deviation from the wording**: the sheen and
      clearcoat accumulators are not carried, they `#error`. See
      "The extended layers are refused, not carried" below
- [x] 145.2 The mirrored vocabulary (D31): `HpLight` (type, direction, colour,
      range, cone — `float3`s, not `DirectionX/Y/Z`), and the shaded-surface
      struct the stage methods take
- [x] 145.3 The per-light method with the standard default (rung 3)
- [x] 145.4 The whole-stage method with the standard default (rung 4); the IBL
      call stays a named seam inside it for T0087 to fill
- [x] 145.5 Lighting render modes: decide ambient-off, receive-shadows-off,
      BRDF selection — each compile-time or data, recorded per 141.15's
      pattern. *Decided in **D38**, with the table in
      `HpMaterial.slang` so an author reads it where they author. Three of the
      four are rejected as modes and one is deferred with its shape decided;
      the through-line is that rungs 3 and 4 turn most of Godot's lighting
      render modes into ordinary code*
- [x] 145.6 The drift guard: decide the mechanism (a pinned copy of
      `ApplyPunctualLight` compared at build or test time is the obvious one)
      and wire it
- [x] 145.7 Worked example: a toon-ramp material in the sandbox overriding
      only the per-light method — doubles as T0149's seed style. **Placed as a
      gpu-test probe rather than in `samples/`, following this ticket's own
      note** ("not shipped, not a style, not T0149's") over the subtask's
      wording; and `rock_pom.slang` is the *shipped* rung-3 material instead
      (158.3). See "Where the toon material went" below
- [x] 145.8 Measure: register pressure and pipeline compile time,
      interface-shaped loop vs current, on the gpu suite's existing scenes.
      *Numbers in "The costs, measured" below and in **D38**. Register
      pressure is proxied by SPIR-V size — no tool here reads a driver's
      allocation, and that limit is stated rather than papered over*

## Notes / findings

### What landed, 2026-08-07

The engine's pixel shader iterates lights in its own loop; the loop is
`IHpMaterial.lighting()`'s default and its per-light body is
`IHpMaterial.light()`'s. The mirror lives in `engine/shaders/HpSurface.slang`,
the vocabulary in `engine/shaders/HpMaterial.slang`, and **D38** records the two
decisions the loop could not be handed over without: the light *order*, and what
happened to Godot's lighting render modes.

### `HpLightResponse` splits four factors, and the byte-identity guard chose that

The obvious shape for a per-light hook is "return the radiance", one `float3`.
The next-most-obvious is Godot's, "return diffuse and specular". **Neither is
bit-identical**, and the reason is arithmetic rather than taste. Upstream
computes

    (BasePunctualDiffuse + BasePunctualSpecular) * LightIntensity * NdotL

and floating-point multiplication does not distribute, so a hook returning
`D*I*N` and `S*I*N` separately and letting the engine add them produces a
different number in the last bits. Splitting **all four** factors —
`Diffuse`, `Specular`, `Intensity`, `NdotL` — lets the caller write upstream's
expression exactly, and it is also the shape that makes cel shading one line
rather than a rewrite. The design was chosen by the guard, which is the point of
having one.

### The extended layers are refused, not carried

145.1's wording asked for "accumulation into base/sheen/clearcoat kept so
T0143's features stay reachable". **That is not what shipped, deliberately.**
`ENABLE_SHEEN`, `ENABLE_CLEAR_COAT`, `ENABLE_ANISOTROPY`, `ENABLE_IRIDESCENCE`,
`ENABLE_TRANSMISSION`, `ENABLE_VOLUME` and `USE_IBL` each `#error` in the
lighting stage, and so does `ENABLE_SHADOWS`.

The alternative was writing those paths blind: the sheen block samples an
`AlbedoScalingLUT` nothing binds, the clearcoat block reads a
`Shading.Clearcoat` nothing fills, and no permutation the engine builds can
execute either. That is exactly the untestable carrying D24 refused, and a
"kept" path that silently does nothing is the failure mode this whole family of
tickets exists to prevent. The `#error` is the same shape as the
`PRIMITIVE_ARRAY_SIZE` guard already at the top of the file: T0143, T0087 and
T0086 each get a **named place to write** instead of a silent hole to fall
through, and each now carries a two-way Ref saying so.

### Where the toon material went, and why not `samples/`

The subtask said "in the sandbox"; this ticket's own earlier note said the
opposite and was right — *"not shipped, not a style, not T0149's ... its only
job is to find out whether the interface is short while that is still cheap to
fix"*. So the cel and ramp probes are modules inside
`tests/gpu/lighting_stage_test.cpp`, where the banding is **asserted** (3
distinct colours against a smooth control's 99) rather than looked at, and
T0149 re-authors its style rather than promoting a probe.

The *shipped* rung-3 material is `rock_pom.slang`, whose shadow march moved
from `surface()` to `light()` — which is 158.3, closed here. Making the rock
cube toon would have destroyed the parallax showcase it exists to be.

**What the probe found about the interface**, which is what it was for: nothing
missing. Cel, ramp, custom BRDF, engine-attenuation read-back, light-count and
light-vocabulary probes all wrote against the contract with no reach into an
engine internal. The one addition it argued for was `HpLight::Index` — without
it "the dominant light" is unnameable, and 158.3 needs exactly that.

### The costs, measured (145.8) — D30 asked for this by name

Against the commit immediately before the mirror, RTX 2080, Linux target:

| | before | after |
|---|---|---|
| standard material `psMain` | 12504 B | **12504 B** |
| standard material `vsMain` | 7280 B | **7280 B** |
| custom module `psMain`, shaded | 19172 B | **19048 B** |
| custom module `psMain`, unshaded | 19160 B | **19036 B** |
| cold compile, most-permutations case, median of 3 | 7.79 s (7.86/7.79/7.78) | 7.84 s (7.84/8.19/7.81) |
| fill cost, 512×512, 1 lamp vs 9, median of 3 | 0.0375 ns/px/light | 0.0352 ns/px/light |

**The standard material's bytecode is identical to the byte**, which is the
strongest form the answer could have taken: the interface-shaped loop costs it
nothing at all. A custom module's got *smaller* — upstream's dead `NdotV`/
`NdotL` and its sheen scaffolding cost more than the mirror's repack, and
`HpLight::Range` and the two cone cosines are eliminated when nothing reads
them, which is D31's bet holding.

Compile and fill differences are **inside the run-to-run spread** and should not
be read as improvements; the 8.19 s outlier shows the band.

**What this does not measure: a driver's register allocation.** SPIR-V size is a
proxy, and no tool in this tree reads the real number. Stated rather than
glossed, because D30 asked about *occupancy*.

**A number that looks like a regression and is not.** The gpu suite prints
"cooked archive: N bytes", and it moved 7380653 → 7499793 across the mirror.
That statistic is not an instrument for anything here: `cookShaders` seals
whatever the *developer cache* has accumulated, and three consecutive runs of
the same case on an unchanged build gave 10254949, 10274781, 10294613. It grows
with machine history.

### The drift guard, shown to work (145.6)

`tools/pin_upstream_shading.py` writes `tests/fixtures/upstream_shading.pinned`
— the exact text of every upstream function this engine **copied** (4:
`ApplyPunctualLight`, `GetBaseLayerIBL`, `GetBaseLayerLighting`,
`ResolveLighting`) or **published as contract** (2: `GetAngularInfo`,
`SmithGGX_BRDF`, whose out-parameter semantics `HpLightResponse::NdotL`'s
documentation promises). `tests/fast/upstream_drift_test.cpp` re-extracts and
diffs on every run of the fast suite.

**Demonstrated failing before it was trusted.** Perturbing one pinned line
produced:

    DiligentFX's ApplyPunctualLight (PBR/public/PBR_Shading.fxh) changed, and
    engine/shaders/HpSurface.slang copies it -- port the change, re-run the gpu
    byte-identity guards, then re-pin.
      Re-pin with: python3 tools/pin_upstream_shading.py
      @@ line 26 of the function @@
      -  RangeAttenuation *= saturate(1.0 - (Distance2*Distance2) / Light.Range8);
      +  RangeAttenuation *= saturate(1.0 - (Distance2*Distance2) / Light.Range4);

Functions the engine merely **calls** are deliberately absent from the pin —
`GetSurfaceReflectanceMR`, `GetPerturbNormalInfo`, every `PBR_Textures.fxh`
getter. The compiler already fails loudly on a signature change, and pinning
them would fire on unrelated upstream churn until somebody bumped the pin
without reading it, which is how a guard becomes a ritual. The guard's own diff
function has a unit test, for the reason this repository keeps relearning: a
check nobody has seen fail is a check nobody knows works.

### The offer upstream is **not** made, and that is the one thing left open

D30's amendment says to offer the hook to DiligentFX regardless — *"a merged
upstream seam is strictly better than a maintained copy, and if it lands the
copy disappears"*. **No issue or PR was opened.** The mirror is written to be
offerable (the loop body is unchanged from theirs; what would go upstream is a
callback seam inside `ApplyPunctualLight` between the attenuation and the BRDF)
but making the approach is an owner action on a third-party repository, not a
code change here. Recorded as owed rather than ticked.

### Two things a later reader will want and this ticket did not build

- **A `Material` datum for "receive shadows"**, if a standard material ever
  needs one. D38 decided the *shape* (data, never a PSO bit) and left the call
  to T0086, which is where the system arrives.
- **A second lighting *module*.** Every stage is a method on one `IHpMaterial`,
  so a game does not ship a lighting module and the permutation multiplier is
  1 — which is the open question T0146 halved and this closes.


### Owner decision 2026-08-06 — the mirrored-loop maintenance is accepted, and the drift guard is built properly

The cost this ticket names — roughly 120 lines copied from `ApplyPunctualLight`,
re-diffed against DiligentFX on every submodule bump — was put to the owner in
plain terms: *every time we upgrade Diligent, somebody re-reads their ~120 lines
to see if anything needs porting across.*

**Accepted, with a steer that changes how to build it:** *"Claude Code will be
doing the work so we should optimize for the right path."* So the drift guard is
not a minimum-effort tripwire to spare a human a chore — it should be built to
catch a real upstream change and show what moved. **Do not compromise the
architecture to shrink a cost that is not a human one.**

**Offer the hook upstream regardless**, and not to save the effort: a merged
upstream seam is strictly better than a maintained copy, and if it lands the
copy disappears. This is T0141's C1/C3 reasoning applied one layer down — write
it as something offerable, use it either way.

### Build one toon material early, as a probe rather than a style

T0149's toon style is content and arrives last. That leaves a gap: **nothing
stress-tests this stage's interface before it is frozen**, and D27's promise is
that adding to a contract is free while removing breaks shipped games.

So build a throwaway toon material *during* this ticket — not shipped, not a
style, not T0149's. Its only job is to find out whether the interface is short
while that is still cheap to fix. This is the same role parallax (141.7) plays
for the surface stage, and that precedent found a real answer rather than
confirming one.

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
