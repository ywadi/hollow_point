# T0149 — Style bundles: the one-click looks

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 542 |
| **Created** | 2026-08-06 |
| **Blocked by** | [0145-lighting-stage-own-the-light-loop.md](../completed/0145-lighting-stage-own-the-light-loop.md) — toon is a rung-3 override, so no styles before the lighting stage; [0148-post-process-stack.md](0148-post-process-stack.md) — a style names a post preset; [0096-hdr-pipeline-and-tonemapping.md](0096-hdr-pipeline-and-tonemapping.md) — a style names a tonemap curve |
| **Refs** | **D30** rung 0 ([../../documentation/02-decision-log.md](../../documentation/02-decision-log.md)); [0143-extended-material-features.md](0143-extended-material-features.md) — "ultra realistic" is largely its features switched on; [0087-environment-lighting.md](0087-environment-lighting.md) — a style names environment defaults; [../completed/0060-material-system.md](../completed/0060-material-system.md) — material defaults are style-scoped; T0078 — where a project's chosen style is stored is its territory; **[T0145](../completed/0145-lighting-stage-own-the-light-loop.md) landed the primitive, and a toon style is now content rather than a blocker.** Rung 3 is `IHpMaterial.light()`: cel is `R.NdotL = ceil(R.NdotL * bands) / bands`, ramp is `R.Diffuse` from a declared texture with `R.NdotL = 1`. `tests/gpu/lighting_stage_test.cpp` carries the throwaway cel module T0145's notes asked for as a *probe* — it is deliberately **not** this ticket's style, and re-authoring rather than promoting it is the point. Two things this ticket inherits: `HpLight::Index == 0` is the documented dominant light (D38), so a style may spend on one light without guessing; and D38 rejected `diffuse_*`/`specular_*` render modes because a style ships a module and a mode would be a second mechanism |

## Why

**The owner's goal, verbatim:** *"we will eventually ship different 1-click
styles for developers (toon shaded out of box, realistic, ultra realistic,
custom dark noir)... this can open up a world of custom styles."*

This is rung 0 of D30's ladder — the rung where a junior developer picks a
look in the inspector and never sees a shader — and it is deliberately the
**last** rung built, because it is pure composition of the rungs below it.
That is what makes it honest: a style has no machinery of its own to go wrong,
it *names* choices the other tickets made overridable.

**A style is data.** The working definition this ticket refines:

| A style names | Which rung | Owner |
|---|---|---|
| a lighting module (or none = standard PBR) | 3/4 | T0145 |
| material defaults (what a new material starts as) | 1 | T0060 |
| a post-chain preset | 5 | T0148 |
| a tonemap curve and exposure defaults | — | T0096 |
| environment defaults | — | T0087 |
| extended-feature enablement | — | T0143 |

**And a custom style is a derived style.** "Custom dark noir" in the owner's
list is the tell: the product is not four looks, it is the mechanism where a
developer takes "realistic", swaps the post preset, desaturates the palette,
and has *their* style — progressive disclosure applying inside the style
system itself. A style must therefore be an asset a game can author, not an
enum the engine ships.

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
