# T0148 — The frame after the world: HDR, tonemapping, the post chain, and the styles that name presets over it

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 460 |
| **Created** | 2026-08-06 |
| **Merged** | 2026-08-08 — **absorbed [T0096](../completed/../completed/0096-hdr-pipeline-and-tonemapping.md) (HDR pipeline, tonemapping and the linear-workflow policy) and [T0149](../completed/../completed/0149-style-bundles.md) (style bundles)**, both ❌ SUPERSEDED. See *Why these were three tickets and are now one* |
| **Blocked by** | [0094-gameplay-extensible-rendering.md](../inprogress/0094-gameplay-extensible-rendering.md) — **for the game-facing half only** (148.9). The engine chain can be built first; what must not happen is this ticket inventing a second pass-insertion mechanism, which is exactly what **D35**'s first half forbids |
| **Refs** | [0086-shadows.md](0086-shadows.md) and [0155-terrain-rendering.md](0155-terrain-rendering.md) — **both inherit 148.3's variant decision**: `SHADOW_MODE` and `TEXTURING_MODE` are compile-time macros in the same way `TONE_MAPPING_MODE` is, and this ticket decides how the engine handles that class of switch once; [0151-shader-variants-and-compile-cost.md](0151-shader-variants-and-compile-cost.md) — where that decision's *cost* lands, and where a style switch's pipeline rebuilds are bounded; **T0161** ([../completed/0161-game-resource-model.md](../completed/0161-game-resource-model.md)) / **D35** — a post-process effect's own LUTs and masks come from `buildModuleSignatureDesc` (`engine/src/ModuleResourceSignature.hpp`), built and measured at 34 ns a draw, so this ticket only feeds it; **T0147** ([../completed/0147-engine-intermediates-for-shaders.md](../completed/0147-engine-intermediates-for-shaders.md)) / **D37** — **scene colour is a *material* read at 10.9b, not a pass**; if a post effect wants the pre-transparent image it reads the same `scene.colour.snapshot` target rather than copying again; **T0146** ([../completed/0146-vertex-stage-hook.md](../completed/0146-vertex-stage-hook.md)) — `ModuleSignatureRequest` takes a *list* of stages and unions each resource's `ShaderStages`; and `IShaderResourceBinding::GetVariableByName` takes **one** stage bit and silently finds nothing given a mask; **T0145** ([../completed/0145-lighting-stage-own-the-light-loop.md](../completed/0145-lighting-stage-own-the-light-loop.md)) — **rung 3 is what makes a toon style possible at all**, and 148.11 consumes it; [../completed/0130-camera-lens-model.md](../completed/0130-camera-lens-model.md) — **exposure lives on the camera** (`Camera::exposureEv100`) and this ticket must not add a second one; [../completed/0111-anti-aliasing-and-render-scale.md](../completed/0111-anti-aliasing-and-render-scale.md) / **D25** — the composite seam rules (upscale once, UI native), and TAA's prerequisites; [../completed/0060-material-system.md](../completed/0060-material-system.md), [0097-texture-import-pipeline.md](0097-texture-import-pipeline.md) — the sRGB policy in 148.2 plumbs through both; [../completed/0087-environment-lighting.md](../completed/0087-environment-lighting.md) — a style names environment defaults; T0078 — quality settings and where a project's chosen style is stored; [../completed/0171-expose-not-replace-sweep.md](../completed/0171-expose-not-replace-sweep.md); **D6**, **D23**, **D24**, **D30** rung 0 and rung 5, **D32**, **D40** |

## Why

**Everything that happens to the frame after the world is drawn is one job**, and
it was three tickets: the **policy** that makes the image correct (T0096), the
**chain** that runs over it (T0148), and the **presets** that name a look across
both (T0149). They were split by *implementation stage* — write the policy, then
the plumbing, then the composition — and that split stopped making sense the
moment **D40** established that the implementation is Diligent's. There is no
"build it" stage left to separate from a "configure it" stage.

**Nothing in this ticket is an effect or a curve to write.** Measured against the
pinned submodule:

- **Eleven tone-mapping operators** ship — `ToneMapping.fxh:87`, with
  `TONE_MAPPING_MODE_*` 0–11 at `ToneMappingStructures.fxh:11-22` (Exp, Reinhard,
  Reinhard-mod, Uncharted2, Filmic ALU, Logarithmic, Adaptive-log, AgX, AgX
  Custom, PBR Neutral, Commerce, plus `NONE`).
- **Five post components** ship complete under `DiligentFX/PostProcess/` — Bloom
  (6 settings), DepthOfField (7), ScreenSpaceAmbientOcclusion (11),
  ScreenSpaceReflection (16), TemporalAntiAliasing (6) — with `PostFXContext`
  (`PostProcess/Common/interface/PostFXContext.hpp`) and `GBuffer`
  (`Components/interface/GBuffer.hpp`) as the infrastructure they need.
- **`zig build` compiles all of it on every platform, and `grep Bloom engine/`
  returns nothing.** Zero of them are constructed.

**So the work is policy, plumbing and exposure — and one genuinely new thing.**

## Why these were three tickets and are now one

Each split had a reason at the time, and each reason has expired:

- **T0096 was separated from T0148** on 2026-08-06 because *"T0096 is
  policy-heavy and already sized; the chain is capability work that follows the
  policy… folding them would make one oversized ticket where the policy half
  blocks on the plumbing half's review."* That argument assumed the chain was
  **capability work**. It is not — it is construction and settings — so the
  asymmetry it was protecting against is gone, and what remains is a hard
  blocking edge between two halves of one pass sequence. **D24 already puts
  tonemapping in a pass over an HDR target, and that pass is the first element
  of this chain.** Kept apart they actively fight: the in-shader path tonemaps
  **per draw, before blending**, which breaks transparency and leaves bloom no
  HDR image to read.
- **T0149 predicted its own absorption in its own subtask list**: *"Hold 149.1
  until T0145 and T0148 are shaped — they may absorb it entirely, and a bundle
  format written against primitives that do not exist yet is work done twice."*
  T0145 has landed. A style names a lighting module, material defaults, a post
  preset, a tonemap curve and environment defaults — **it is presets over this
  chain and it has no machinery of its own**, which its own text says is what
  makes it honest.

**The pattern, recorded because it caught three tickets here and five across the
render layer**: splitting by *implementation stage* (vendor it / wire it /
compose it) or by *feature variant* only makes sense while the implementation is
ours to build. Under D40 it is not, so those splits produce partial designs of
one thing instead of a plan.

## The three layers, in order, because later ones depend on earlier ones

**1. Policy — the linear workflow.** Rendering in linear HDR, tonemapping to
display once, and keeping sRGB and linear straight end to end. This is the half
that constrains *other* tickets: albedo and emissive are sRGB, normals and data
and lookup textures are linear, recorded per asset (T0097) rather than guessed
per call site. Colour-space bugs are the silent kind — lighting tuned against a
clamped LDR buffer, or a UI tonemapped along with the world — and each looks
plausible alone while fixing it later means re-tuning every light and every
material in every scene. **That re-tuning is the cost this layer exists to
avoid.**

**2. The chain.** An ordered sequence at frame step 10.10, between the world
layers and the UI layers, with the tonemap as its resolve step and each effect
declaring which side of the tonemap it sits on. `PostFXContext` and `GBuffer`
constructed and threaded through; the five components run; their settings
surfaced as reflected data.

**3. Presets.** A style is **data** — it names a lighting module (or none, for
standard PBR), material defaults, a post preset, a tonemap curve and exposure
defaults, and environment defaults. It has no machinery. A *custom* style is a
**derived** style, which is the owner's own "custom dark noir" example and the
reason a style must be an authorable asset rather than an engine enum.

## The seam — and most of this ticket has none

- **Every engine effect is a *setting*.** Bloom threshold, SSAO radius, DoF
  focus, the tonemap operator, exposure: **data**, through T0078's quality
  section and the scene/camera. A game developer does not implement a
  tonemapper. **A ticket proposing a game-facing hook for one of these is
  wrong** (D40), and this one must not grow one.
- **The one real seam is a game's own effect, and it is a *pass*.** It reads the
  whole frame; nothing about it is per-surface, so `IHpMaterial` is the wrong
  shape and must not be stretched to fit. It is `IRenderLayer` — **T0094's
  transport, inherited whole, not reinvented here**.
- **What a game authors**: a Slang fragment shader over the frame with declared
  parameters and its own resources (D35's `buildModuleSignatureDesc`).
- **What the default is**: the engine's chain, in order, unchanged for a game
  that adds nothing.
- **A style may not change the frame anatomy or the pass structure.** It is
  presets, not a renderer — and if one ever genuinely needs a different pass
  structure, that is **D24's revisit clause** arriving for real and it should
  arrive as that argument rather than smuggled in as a bundle field.

## Done when

### Policy
- [ ] The world renders into a **linear HDR** target — format decided and
      recorded (RGBA16F vs R11G11B10, memory vs precision) in T0046's declarations
- [ ] **sRGB vs linear is explicit at every texture bind**, recorded per asset
      (T0097), not guessed per call site
- [ ] **The exposure model is decided and recorded**, with T0130's precedence
      honoured: exposure lives on the camera as `Camera::exposureEv100`, and
      auto-exposure *writes* it rather than shadowing it. **A second exposure
      value on the post stack would multiply with the camera's, and every
      individual number would look reasonable while the image is wrong**
- [ ] A deliberately overbright test scene shows highlights rolling off rather
      than clipping
- [ ] The editor viewport (T0033) displays the tonemapped image, not raw HDR

### The chain
- [ ] **`PostFXContext` and `GBuffer` are constructed and driven**, and the chain
      runs at 10.10 between the world layers and the UI layers, with **each
      effect declaring which side of the tonemap it sits on**
- [ ] **Tonemapping happens once, in a pass calling upstream's `ToneMap()`** —
      not per draw. UI, HUD and debug draw composite **after** it
- [ ] **How the operator is selected is decided and recorded** — it is a
      **compile-time macro**, so this is a variant question, and **T0086 and
      T0155 inherit the answer** for `SHADOW_MODE` and `TEXTURING_MODE`
- [ ] **Bloom works end to end** on the HDR target, behind a quality setting —
      the proof the chain reads an HDR image rather than a tonemapped one
- [ ] **Every component's settings are reflected engine data**, serialized and
      reachable from T0078's quality section — one shape, not five
- [ ] SSAO, SSR, DoF and TAA are each **dispositioned** — integrated, or deferred
      with the blocker named (TAA waits on T0111's prerequisites; SSR/SSAO need
      depth/normal inputs whose forward-renderer availability is T0147's 147.6
      question) — none left as silent maybes
- [ ] The chain composes with **D25**'s rules: upscale happens once, UI stays
      native and post-tonemap
- [ ] Cost per effect is visible (profiling zones, T0030's shape)

### Game-facing and presets
- [ ] A **game-authored post effect** inserts at a stated point in the chain via
      **T0094's** layer transport, orders deterministically against engine
      effects, and survives hot reload (T0094.7's rules)
- [ ] **Two styles are demonstrably switchable on the same scene** — standard PBR
      and a toon — with **zero shader edits and zero material rewiring**
- [ ] A style is a **serialised asset** in the reflected-property format, like a
      material: readable, diffable, authorable by hand — and **cloned on use**,
      so an engine upgrade cannot change a shipped game's look
- [ ] **Deriving a custom style works**: replace one layer of an existing bundle
      and everything else is inherited — the worked example is "dark noir"
      derived from "realistic"
- [ ] **Switching cost is measured and bounded** — a style switch is pipeline
      rebuilds, and the number goes here rather than being assumed
- [ ] **What a style may not change is written down** (frame anatomy, pass
      structure, anything D17/D22 own)

### And the record
- [ ] **The matrix rows flip** — five component rows plus tone mapping in
      [`12-vendored-capabilities.md`](../../documentation/12-vendored-capabilities.md),
      or the reason they did not

## Subtasks

**Policy first — the chain hangs off it and the presets name it.**

- [ ] 148.1 **The HDR target format**, decided and added to T0046's declarations
- [ ] 148.2 **Write the sRGB policy down** — which semantic slots are sRGB — and
      plumb it through texture load (T0023/T0097) and material binding (T0060)
- [ ] 148.3 **Decide operator selection**, and record it where T0086, T0155 and
      T0151 will find it: one PSO per operator (upstream's own pattern — see the
      note on the macro), a project-wide compile-time choice, or a runtime branch
- [ ] 148.4 **Exposure**, as a camera/scene setting, serialized, with T0130's
      precedence stated in the code and not only here

**Then the chain.**

- [ ] 148.5 **Construct `PostFXContext` and `GBuffer`**, and thread them through
      an ordered chain at 10.10 with per-effect enable/quality and ping-pong via
      T0046's `declarePingPong`. **Read `PostFXContext.hpp:190-206` first** — the
      12 named slots are what a component expects to find filled
- [ ] 148.6 **The tonemap pass**, calling `ToneMap()`. Ordering verified against
      debug draw (T0061) and UI (T0069) — both post-tonemap
- [ ] 148.7 **Bloom, first and alone** — fewest inputs, and it proves the chain
- [ ] 148.8 **Settings as reflected data**, one shape for all five plus the
      tonemap, wired to T0078. Design-gaps item 4's fifth consumer — do not
      invent a sixth
- [ ] 148.9 Disposition SSAO / SSR / DoF / TAA, one decision each
- [ ] 148.10 Per-effect profiling zones

**Then the game-facing half and the presets, which are composition over the
above and must not start before it.**

- [ ] 148.11 **The game post-effect shape**: a Slang shader plus declared
      parameters and resources (D35), inserted via **T0094's** transport.
      **Do not design an insertion mechanism here** — if T0094 has not landed
      this is blocked, and that is the correct outcome rather than a second
      mechanism
- [ ] 148.12 **The style asset**: fields, serialisation, reflection, and the
      **clone-on-use** semantics the owner chose — the editor copies, the project
      owns the copy
- [ ] 148.13 **The realistic baseline style** — today's output, named. The
      identity bundle, and the proof the mechanism costs nothing when inert
- [ ] 148.14 **The toon style** — consumes T0145's rung-3 `light()` override.
      Note `tests/gpu/lighting_stage_test.cpp` carries a throwaway cel module as
      a *probe*; **re-author rather than promote it**, which is the point
- [ ] 148.15 Ultra-realistic (T0143's features on, environment defaults set) and
      **dark noir as a *derived* style** — the derivation proof
- [ ] 148.16 The inspector picker (lands with editor scaffolding, T0032+; until
      then the asset is hand-authored, which 148.12's format makes real)
- [ ] 148.17 **Measure the switch**
- [ ] 148.18 **Add the rows before building** — D40

## Still unticketed, carried from T0149 so it is not lost

**A project- or scene-wide material override — "render everything with this
material".** T0149 named it as *"the cheapest large win here, most of what 'one
click' means mechanically, and a small addition on `resolveMaterialSlot`"*, and
it had no ticket. It still does not. It is **not** in this ticket's Done-when —
recorded here so it is visible, and it should become its own ticket or a subtask
of T0060's successor rather than being quietly absorbed.

---

## Notes / findings

### 2026-08-06 — this matters more than "noir needs it", and the Godot/Unreal comparison is why

This ticket was justified largely by dark noir needing grading, vignette and
grain. That undersells it. **Post-processing is half of *every* style**, not one
style's requirement — and it is the piece both comparison engines lean on
hardest:

- **Unreal**: Post Process Volumes are blendable at runtime and are where a
  look actually lives. Unreal has no built-in toon shading model, so a
  post-process material is the *standard* workaround.
- **Godot**: `WorldEnvironment` carries tonemap, glow, SSAO, fog and colour
  adjustments, and swapping one is cheap because it is parameters rather than
  pipelines.

The shared lesson: both engines split a look into a **cheap layer** (parameters
and post, switchable instantly) and an **expensive layer** (shader structure,
needing precompiled pipelines). This stack is the cheap layer, which makes it
the part of a style that can actually change at runtime without a stall — and
therefore the part that carries most of the owner's "changeable dynamically"
requirement. See T0149 and T0151.

### Why the game shape is a fragment shader and not a compute hook

Godot's `CompositorEffect` is compute-only, which forces every simple colour
grade through a dispatch and a storage image. The common case — read a
texture, write a colour — is a fullscreen fragment pass, cheaper to author and
to run on this engine's single (Vulkan) backend, and it reuses the whole
material toolchain: Slang, the module system, reflection-driven parameters,
hot reload. Compute post effects become possible when T0150 lands, as an
addition, not the entry point.

### Godot reference (4.7.1, surveyed 2026-08-06)

`CompositorEffect` (4.3+): five fixed callback stages; flags to request
resolved colour/depth, motion vectors, normal-roughness (Forward+ only);
render-thread, `RenderingDevice`/GLSL, no access to the light loop or BRDF;
Mobile/Forward+ only. The comparison to keep honest: Godot's five insertion
points include **pre-opaque and mid-frame** hooks; this chain as specified is
post-world only. Mid-frame insertion for games exists here through
`IRenderLayer` ordering (T0027) — if a game asks for a pre-opaque *effect*
specifically, that is T0094's transport, and this ticket should not grow it.

---

## Absorbed from T0096 — its findings, kept verbatim

*These were written against T0096 as a standalone ticket. The `96.x` numbers
refer to its old subtask list; the mapping is in its `## Descoped` table.*

### Inherited from T0111 / D25 (2026-08-05) — you own the composite seam

**Three separate things all want the same pass, and D25's instruction is: do not
build three.**

1. **Tonemap** — D24 already recommends a pass over an HDR target rather than
   `PSO_FLAG_ENABLE_TONE_MAPPING`.
2. **The render-scale upscale** — D25 sizes world targets from
   `output x renderScale`, so something must resolve them to native.
3. **UI at native resolution** — T0027.5 composites every layer into one target, so
   a HUD inherits the world's render scale unless the stack becomes two-phase.

All three sit between the world layers and the UI layers. Whichever ticket reaches
this seam first should build it so the others plug in.

Also from D25: **MSAA is out**, which removes the resolve-before-tonemap trap this
ticket would otherwise have to handle (resolving multisampled HDR before tonemapping
lets bright samples dominate the average and produce fireflies). And `AverageLogLum`
in `PBRRendererShaderParameters` is the auto-exposure hook, with **T0130.4 already
deciding exposure lives on the camera** — auto-exposure writes `Camera::exposureEv100`
rather than shadowing it.

See [../completed/0111-anti-aliasing-and-render-scale.md](../completed/0111-anti-aliasing-and-render-scale.md).

### Inherited from T0134 / D24 (2026-08-05) — and this ticket's Refs stated a false premise

**Correction first, because it is written into this ticket's own Refs and into
T0134's Done-when, and it is wrong.** The claim was that DiligentFX ships its own
`ToneMapping` *component* alongside the in-shader path, so the two could
double-apply. **There is no such component.**

- `Components/interface/ToneMapping.hpp` declares exactly two functions —
  `ReverseExpToneMap` and `ToneMappingUpdateUI`. It is a UI helper, which is why
  **D6** lists it among the ImGui-calling headers.
- `Shaders/PostProcess/ToneMapping/` contains only `ToneMapping.fxh` and
  `ToneMappingStructures.fxh` — a shader **include**, shared rather than staged.
- `PostProcess/` on the C++ side has **no ToneMapping directory**: only `Bloom`,
  `DepthOfField`, `EpipolarLightScattering`, `ScreenSpaceAmbientOcclusion`,
  `ScreenSpaceReflection` and `TemporalAntiAliasing`.

**Tonemapping in DiligentFX happens in the PBR pixel shader and nowhere else** —
`RenderPBR.psh:530-540`, under `#if ENABLE_TONE_MAPPING`, calling
`ToneMap(OutColor.rgb, TMAttribs, g_Frame.Renderer.AverageLogLum)` with
`TONE_MAPPING_MODE` as a compile-time define. Twelve modes ship, including AgX
and PBR-neutral. So there is **no double-apply hazard to design around.**

**The real fork, and D24's recommendation — take the pass:**

- *In-shader* (`PSO_FLAG_ENABLE_TONE_MAPPING`): no extra pass, no HDR target.
  But it tonemaps **per draw, before blending** — which breaks transparency and
  leaves DiligentFX's `Bloom` component with no HDR image to read.
- *A pass over an HDR target*: what `TargetFormat::ColourHDR` (`RGBA16_FLOAT`)
  already exists for, and the only option compatible with bloom and correct
  transparency. `Renderer.AverageLogLum` is the auto-exposure hook, and
  **T0130.4 already decided exposure lives on the camera** — so auto-exposure
  writes `Camera::exposureEv100` rather than shadowing it.

**Ordering, which T0027 flagged and the current architecture gets right by
accident:** tonemapping must apply to the world and not to UI composited over it.
Today that holds because it would happen inside the world layer's own pixel
shader. **A tonemap pass must preserve it** — it belongs between the world layers
and the UI layers, which is what `IRenderLayer::order` exists to express.

Read [../completed/0134-pbr-renderer-adoption.md](../completed/0134-pbr-renderer-adoption.md) first.

**T0130 put exposure on the camera, and this ticket must not add a second one.**
`hp::Camera::exposureEv100` is the exposure, as EV100, and the reasoning is that
exposure is a property of a view: a frame can hold a main view, a security
monitor and a portal looking at the same world and needing different exposures,
which a single post-process value cannot express.

The precedence T0130 set: **this ticket owns the tonemap curve and any
auto-exposure, and auto-exposure writes `exposureEv100` rather than shadowing
it.** An exposure value held on the post-process stack as well would multiply
with the camera's, and the failure mode is that every individual number looks
reasonable while the image is wrong.

`hp::exposureMultiplierFromEv100` converts to the linear multiplier a shader
wants.


- **Ordering interactions already implied elsewhere, collected here:** fog
  (T0089) must apply in HDR before tonemap; T0093's visibility dimming is a
  material-level term so it is naturally pre-tonemap; UI (T0069) and debug
  draw are post-tonemap. If a pass cannot say which side of the tonemap it is
  on, that is a design smell.
- DiligentFX post-process components carry ImGui settings panels (see D6) —
  usable for free in the editor while tuning.
- Auto-exposure is deliberately deferred: it needs luminance
  reduction/histogram work and interacts with every lighting decision. A fixed
  exposure value per scene is the right starting point and is what most
  stylised games ship with anyway.

### Cross-ticket obligations (2026-08-04, T0124 backfill)

- **T0111** owns the AA/render-scale decision and either confirms or drops the
  TAA-shaped hook 96.7 leaves open — its 111.2 names the full dependency list
  (motion vectors, jitter, history buffers, ordering against this tonemap).
  Check T0111's recorded decision before wiring anything into the hook.

### Cross-ticket obligation — T0130 (2026-08-05)

**Exposure ownership is contested between this ticket and T0130, and one of you
must own it.** A camera is where exposure naturally belongs conceptually — it is
a lens property in every physical model — while the HDR chain here is where it is
actually applied. T0130 asks for the decision to be recorded with a stated
precedence rather than each ticket assuming the other handles it.

The failure this prevents is concrete: exposure implemented in both places,
multiplying, and a scene that is correct only when one of them happens to be at
its neutral value.

---

## Absorbed from T0149 — its owner decisions and findings, kept verbatim

*The `149.x` numbers refer to its old subtask list; the mapping is in its
`## Descoped` table. Its **Blocked by** on T0145 is discharged — that ticket
landed 2026-08-06.*

## Owner decisions — answered 2026-08-06, and they shrink this ticket

- **Content, not engine API — and cloned on use.** *"they can be content
  applied by the editor and 'cloned' to use so that even if version changed
  they are shipped."* The editor copies a style into the project; the project
  owns its copy from then on. **This removes D27's weight entirely** — an
  engine upgrade cannot change a shipped game's look, because the game is not
  referencing the engine's copy.

  **The named cost, so nobody files it as a bug:** a cloned style does not
  receive improvements either. Fix the toon style and existing projects keep
  their copy until somebody re-clones. For content that is normal; it is only
  surprising if you were expecting API semantics.
- **Realistic and toon are the first two**, named by the owner as the examples.
  They are deliberately opposite ends: realistic exercises IBL, shadows,
  tonemapping and T0143's features; toon exercises the rung-3 BRDF override and
  an outline pass. Between them they cover both halves of what a style *is*.
  Ultra-realistic and noir stay as later derivations (149.4, 149.5).
- **Per project, changeable at runtime by the game.** *"Probably per project but
  the game dev has control to change within each project dynamically."*

  **This is a hard constraint, not a preference**, and it lands on T0151 and
  T0141.3 rather than here: a style switch that changes the shading model
  changes pipelines, and a pipeline built on demand is a visible stall. Every
  style's pipelines must be cooked or cached ahead. See the Godot/Unreal note
  below — both engines built substantial machinery for exactly this, and it is
  the single most expensive consequence of the word "dynamically".

## This ticket is much smaller than it was, and the reason is the Godot/Unreal comparison

**Neither Godot nor Unreal has a style system.** Unreal ships no toon mode;
Godot ships none either. What both ship are *primitives* — per-object material
override, post-process volumes, material parameter instances, environment
settings — and a "style" is something a game or a marketplace package
assembles out of them.

Following them, **the engine builds the primitives and a style becomes
content.** That is both the easier and the stronger answer, and it is why the
three questions above stopped being blocking: with no engine-side style
*system*, there is no API promise to size, no versioning policy to write, and
no per-scene-versus-per-project machinery to build.

What that leaves as genuinely missing, ranked — and note that most of it is
owned elsewhere:

| Primitive | Status | Owner |
|---|---|---|
| Per-surface material assignment | **landed** (141.12, the `Assigned` row) | — |
| **BRDF override** | not started | **T0145** |
| Post-process chain | nothing exists | **T0148** |
| Project/scene-wide material override | does not exist | **unticketed** — see below |
| Precompiled pipelines so a switch does not stall | not started | T0151 + 141.3 |

**Material switching alone is not sufficient, and this is the load-bearing
finding.** Swapping a material changes the *surface* — albedo, roughness,
normals. It cannot change *how light is applied*, and toon shading is
fundamentally quantised `N·L`, which lives in the light loop. This is precisely
why toon in Unreal is painful (fixed shading models, so people reach for
post-process materials or engine-source edits) and why Godot can do it (it has
`light()`). **T0145 is what makes this ticket possible at all**, which is why
it is the blocker rather than a nice-to-have.

**Still to ticket:** a project- or scene-wide material override — "render
everything with this material". It is the cheapest large win here, most of what
"one click" means mechanically, and a small addition on `resolveMaterialSlot`.
It was not raised until the Godot comparison made it obvious.

## Done when

- [ ] Two styles are demonstrably switchable on the **same scene** — standard
      PBR and a toon — with **zero shader edits** and zero material rewiring,
      from the inspector
- [ ] A style is a **serialised asset** in the reflected-property format, like
      a material — readable, diffable, authorable by hand
- [ ] Deriving a custom style works: replace one layer of an existing bundle
      (the post preset, say) and everything else is inherited — the worked
      example is a "dark noir" derived from "realistic"
- [ ] Switching cost is **measured** and bounded — a style switch is pipeline
      rebuilds, and T0141.3's cache plus T0151's work decide whether that is a
      hitch or a stall; the number goes here
- [ ] What a style may **not** change is written down (frame anatomy, pass
      structure, anything D17/D22 own) — a style is presets, not a renderer

## Subtasks

- [ ] 149.1 The bundle asset: fields, serialisation, reflection — **and the
      clone-on-use semantics the owner chose**, which is the part that decides
      whether this is content or API. The editor copies; the project owns the
      copy. **Hold this until T0145 and T0148 are shaped** — they may absorb it
      entirely, and a bundle format written against primitives that do not
      exist yet is work done twice
- [ ] 149.2 The toon style — consumes T0145's 145.7 ramp material as its seed,
      adds the style-scoped defaults around it
- [ ] 149.3 The realistic baseline style (today's output, named — the identity
      bundle, and the proof the mechanism adds zero cost when inert)
- [ ] 149.4 Ultra-realistic: T0143's features on, environment defaults set —
      mostly a data exercise proving 143.4's fields compose
- [ ] 149.5 Dark noir as a *derived* style: realistic minus saturation plus
      grain — proves derivation, and is the owner's own example
- [ ] 149.6 The inspector picker (lands with editor-phase scaffolding, T0032+;
      until then the asset is hand-authored, which 149.1's format makes real)
- [ ] 149.7 Measure the switch

## Notes / findings

### Why this is not "a second renderer per style"

D24's revisit clause named "a stylised non-PBR renderer" as grounds to reopen
it. D30 answers instead: toon here is an override *inside* the one forward
path — same shadow maps, same culling, same passes — so styles multiply looks
without multiplying renderers. If a style ever genuinely needs a different
pass structure, that is D24's revisit for real, and it should arrive as that
argument rather than smuggled in as a bundle field.

### The Godot comparison that motivates rung 0

Godot's equivalent journey — StandardMaterial3D, then "convert to
ShaderMaterial" — is a **one-way** door: the conversion generates the whole
shader as code, and from then on the inspector's material knobs are gone;
there is no way back and no partial step. (4.7.1, surveyed 2026-08-06.)
HollowPoint's ladder is specifically built so no rung is a one-way door, and
styles are the rung-0 proof: picking a style loses nothing, deriving one loses
nothing, and the shader remains overridable underneath.
