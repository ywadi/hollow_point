# T0149 — Style bundles: the one-click looks

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 542 |
| **Created** | 2026-08-06 |
| **Blocked by** | [0145-lighting-stage-own-the-light-loop.md](0145-lighting-stage-own-the-light-loop.md) — toon is a rung-3 override, so no styles before the lighting stage; [0148-post-process-stack.md](0148-post-process-stack.md) — a style names a post preset; [0096-hdr-pipeline-and-tonemapping.md](0096-hdr-pipeline-and-tonemapping.md) — a style names a tonemap curve |
| **Refs** | **D30** rung 0 ([../../documentation/02-decision-log.md](../../documentation/02-decision-log.md)); [0143-extended-material-features.md](0143-extended-material-features.md) — "ultra realistic" is largely its features switched on; [0087-environment-lighting.md](0087-environment-lighting.md) — a style names environment defaults; [../completed/0060-material-system.md](../completed/0060-material-system.md) — material defaults are style-scoped; T0078 — where a project's chosen style is stored is its territory |

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

## Owner decisions — left open deliberately, this ticket cannot start until they are answered

- **Are shipped styles an engine product or sample content?** Engine-shipped
  styles become API: every game that picked "toon" inherits changes to it on
  engine upgrade — that is a promise with D27's weight. Sample styles are
  copied into a project and frozen there — no promise, less magic. This is a
  studio-product question, not an engineering one.
- **Which styles ship first**, and what does "toon out of box" mean visually?
  A ramp count, an outline yes/no, and a reference image are product choices.
- **Is a style per-project or per-scene?** Per-scene styles imply switching
  cost mid-game (pipeline rebuilds); per-project is cheaper and probably the
  right first answer, but it forecloses the security-camera-in-noir trick.

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

- [ ] 149.1 The bundle asset: fields, serialisation, reflection — after the
      owner questions above are answered
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
