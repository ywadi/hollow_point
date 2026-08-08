# T0153 — Surface de-tiling: breaking texture repetition, exposed to the game

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 460 |
| **Created** | 2026-08-06 |
| **Blocked by** | [0154-noise-generation.md](0154-noise-generation.md) — **a blocker, not a reference** (corrected 2026-08-08 during the merge review). Tier 1 *is* "multiply by low-frequency noise at a different scale" and tier 2 needs a **per-cell hash** with stable per-cell offset and rotation; neither exists, and nothing in `third_party/` supplies them. T0154 must deliver those two primitives in Slang before 153.2 or 153.3 can start. The two tickets were considered for merge and kept separate — see T0154's note for why |
| **Refs** | [../completed/0160-material-declared-parameters.md](../completed/0160-material-declared-parameters.md) — **the declared textures and parameters this ticket was half-blocked on landed 2026-08-06**: a de-tiling module declares its own variation map in `HpTexture0`..`3` and its own strength/scale knobs in `HpMaterialParams`, so what is left here is the per-tap sampling seam alone; [../completed/0141-custom-shader-materials.md](../completed/0141-custom-shader-materials.md) — **141.8's triplanar is what makes this urgent**, and 141.7's `surfaceCoordinates` is the hook that turns out *not* to reach it; [0156-parallax-under-triplanar.md](../completed/0156-parallax-under-triplanar.md) — **156.5 decided 153.1's seam shape with this ticket** (one per-logical-tap sampling seam, coordinates above it, blends above it — see 153.1) and its `HpTriplanarSample` is the fan-out that must route through the seam once it exists; [../completed/0142-slang-shader-language.md](../completed/0142-slang-shader-language.md) — the seam is an `IHpMaterial` method, so D28's interface-with-defaults is the mechanism; [0097-texture-import-pipeline.md](0097-texture-import-pipeline.md) — **the good tier needs an import-time transform**, see 153.5; [0151-shader-variants-and-compile-cost.md](0151-shader-variants-and-compile-cost.md) — each tier is a permutation axis and its cost lands there; [../completed/0149-style-bundles.md](../completed/0149-style-bundles.md) — a style names a de-tiling default; **D26**, **D27**, **D30** rung 2 ([../../documentation/02-decision-log.md](../../documentation/02-decision-log.md)) |

## Why

**The owner's ask, verbatim:** *"we need awesome de-tiling options exposed to the
game dev."*

**Measured 2026-08-06: nothing exists.** Zero matches for detiling, anti-tiling,
stochastic sampling or histogram-preserving blending in `engine/`, and **nothing
in DiligentFX either** — its "stochastic" references are all screen-space
reflections. So this is not a vendored capability sitting unused, which is the
usual finding here. It has to be built.

**141.8's triplanar is what makes it urgent rather than nice.** Triplanar
projects **world-space** coordinates, so a texture repeats on a fixed world grid
that no UV unwrap can hide — and the technique's whole point is surfaces that
have no unwrap. 141.8's own acceptance case is a UV-less quad. The feature that
just landed is the one that shows the artefact worst.

## The tiers, cheapest first — this ticket delivers a ladder, not one technique

| Tier | Cost | What it fixes | What it does not |
|---|---|---|---|
| **Macro variation** — multiply by low-frequency noise at a different scale | ~1 extra sample | repetition read at distance | nothing close up; the grid is still there |
| **Stochastic offset** ("texture bombing") — grid or hex cells, random offset and rotation per cell, blend the taps | 3–4× samples | the grid, genuinely | naive blending averages toward grey and **loses contrast**, which is why tier 3 exists |
| **Histogram-preserving blending** (Heitz & Neyret 2018) | 3 taps **+ an import-time transform** | the grid, with contrast intact | needs 153.5, so it is not shader-only |

**Exposing the ladder is the deliverable, not picking a winner.** Tier 1 is right
for a distant cliff, tier 3 for a hero surface, and the game decides — which is
D30's progressive-disclosure principle applied one level down: a default that is
good, and the ability to take control.

## The interface question, and it must be answered first

**141.7 proved the surface stage's contract by writing parallax against it, and
found it short** — there was no before-sampling hook, so `surfaceCoordinates`
was added while that was still free. **De-tiling is the next stress test, and it
probably fails differently.**

Parallax is a *coordinate transform*: displace the UV, then sample once.
`surfaceCoordinates` reaches that exactly. **De-tiling is a multi-tap blend** —
sample three times at different offsets and combine — so a coordinate hook does
not reach it at all. It needs to wrap the **sample itself**.

That is a real gap in `IHpMaterial` and 153.1 is where it gets decided. Under
D27 adding to the contract is free and removing breaks shipped games, so this is
cheap now and expensive after games author against it. **Do 153.1 before any
tier.**

## Done when

- [ ] **A repeating texture on a large surface shows no visible grid**, measured
      rather than eyeballed — see 153.7, which is the honest hard part
- [ ] **All three tiers exist and are selectable per material**, not a global
      switch and not one hardcoded choice
- [ ] **A game shader can override the sampling**, so de-tiling is a rung-2
      capability a game can replace — not an engine feature it can only
      configure
- [ ] **It works under triplanar**, which is the case that motivated it, with
      the combined sample count measured and written down
- [ ] The **import-side dependency is settled with T0097**, referenced both ways
- [ ] **What is not delivered is written down** — including any tier deferred,
      with the trigger that would revisit it

## Subtasks

- [ ] 153.1 **Implement the seam — its *shape* is already decided, with
      T0156.5 (2026-08-06), so implement that decision rather than reopening
      it.** One per-logical-tap sampling method on `IHpMaterial`: it carries
      the texture and sampler, the coordinate, the **explicit screen-space
      gradients** for that coordinate, and the array slice; its default is a
      single `SampleGrad`; de-tiling tiers replace how *one logical tap* is
      fetched. The layering rule that makes the techniques compose:
      **coordinate decisions stay above the seam** (`surfaceCoordinates` for
      UV materials, the triplanar basis and its per-axis parallax marches for
      triplanar ones), **and so does blending logical taps** (triplanar's
      weights). T0156 proved the layering from above: its `HpTriplanarSample`
      is already exactly "N logical taps through one fetch expression", so
      when this seam lands, its three `SampleGrad` calls route through it
      mechanically and de-tiling under triplanar-with-parallax needs no
      special case — 153.6's nine-taps number is this composition, and
      T0156's sub-1% weight floor already trims it to live axes only.
      Constraints T0156 paid to learn: `SampleGrad` is mandatory on both
      sides of the seam (marched coordinates *and* per-cell offsets break the
      implicit derivative); fold the frame mip bias in as `exp2(mipBias)` on
      both gradients, which T0156 measured as exact; and verify on the pinned
      slangc that an interface method taking resource-typed parameters
      specialises statically — if it does not, the fallback is per-slot
      methods, argued then, not silently.
      **Why T0156 did not add the seam itself**: nothing consumes it before
      the first tier exists, and the contract's own rule (D27, restated in
      `docs/shaders/index.md`) is that nothing is exposed before the system
      behind it — so T0156's marches live in the standard material's private
      helpers, shaped for this seam but not published as it
- [ ] 153.2 **Tier 1, macro variation** — the cheap win, and the one that ships
      as a sane default
- [ ] 153.3 **Tier 2, stochastic offset** — hex or grid cells, per-cell offset
      and rotation, blended. Record which grid and why
- [ ] 153.4 **Tier 3, histogram-preserving blending** — the contrast-preserving
      version; blocked on 153.5
- [ ] 153.5 **The import-side transform (T0097)** — tier 3 needs a
      histogram-transformed texture plus an inverse LUT, generated at import.
      **This is not shader work and it is why tier 3 is not shader-only.** Agree
      the artefact format with T0097 and reference it both ways
- [ ] 153.6 **Measure the cost, especially under triplanar** — triplanar already
      takes three samples per texture, one per axis; a 3-tap blend on top is
      **nine samples per texture per surface**. That is a budget question, not a
      detail, and the number belongs in the ticket
- [ ] 153.7 **Decide how "no visible tiling" is asserted** — see below
- [ ] 153.8 **Register the permutation axes with T0151** — three tiers is a new
      axis multiplying the PSO space, which is exactly what T0151 exists to
      bound

## How do you test "it does not look repetitive"?

**This is the subtask most likely to be waved through, and it should not be.**
Every gpu test in this engine asserts pixels; none of them can eyeball a frame.
"No visible grid" is a perceptual claim and needs a mechanical proxy.

Two candidates, neither yet evaluated:

- **Autocorrelation of the frame** at the tile period — a tiled texture
  correlates strongly with itself shifted by exactly one tile, and de-tiling
  should collapse that peak. Threshold on the peak height.
- **FFT peak detection** at the tile frequency — the same claim in the frequency
  domain, and possibly easier to threshold.

The measurement this engine already trusts is the *variation* statistic the
textured-surface tests use, and it is not sufficient here: a perfectly tiled
texture has high variation. **Variation proves the texture is being sampled;
it says nothing about repetition.** Whatever 153.7 chooses must distinguish those
two, and the choice should be recorded with the reason.

## Not in scope, so it is a decision rather than an omission

- **Detail maps and layer blending** — adjacent and often confused with
  de-tiling, but a different feature (a second texture at a different scale,
  authored). T0143's territory if it is wanted.
- **Virtual texturing / megatextures** — the other answer to repetition, and a
  different order of magnitude. Not this ticket, and no ticket today.
- **Terrain-specific splatting** — a terrain system's job, not the surface
  stage's.

## Notes / findings

### Measured 2026-08-06 — the gap is real and it is ours

`grep -rniE 'detil|de-til|anti-til|stochastic|histogram.?preserv|hex.?til|texture.?bomb|by-example'` over `engine/`, `tests/` and `claude_documentation/`: **no matches.** The same pattern over `DiligentFX/` and `DiligentSamples/`: matches only in `ScreenSpaceReflection/README.md`, all referring to stochastic *reflection* sampling.

So the usual first question here — "is it already in `third_party/`?" — has been asked and answered. It is not.
