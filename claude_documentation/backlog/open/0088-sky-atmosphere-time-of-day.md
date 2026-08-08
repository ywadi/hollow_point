# T0088 — Sky, atmosphere and time of day

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Complex |
| **Phase** | 11 — World & environment |
| **Order** | 830 |
| **Created** | 2026-08-03 |
| **Refs** | [../completed/0087-environment-lighting.md](../completed/0087-environment-lighting.md) — **this ticket inherited T0087's remainder when it closed 2026-08-08**: the visible skybox (87.2), the per-scene environment asset (87.1/87.5) and 87.8's local ambient control. T0170.5 delivered the IBL half and shipped a *procedural default* sky specifically so that a real one is the game's to supply; [0091-volumetric-fog.md](0091-volumetric-fog.md) — **evaluates against this ticket rather than beside it**: `EpipolarLightScattering` is one directional light, so sun shafts are this ticket's configuration and only point/spot beams are T0091's; [0150-compute-pipelines.md](0150-compute-pipelines.md) — gates the high-quality techniques only, see the compute note; [0094-gameplay-extensible-rendering.md](0094-gameplay-extensible-rendering.md) — **a game's own sky is a pass on that seam**, which is how D32's promise is discharged here; [0171-expose-not-replace-sweep.md](../completed/0171-expose-not-replace-sweep.md); **D32**, **D40** |

## Why

**Rescoped 2026-08-08 by T0171 under D40, and it inherited T0087's remainder in
the same change.** This ticket now owns **the sky**, static and dynamic, because
they are one mechanism with two settings rather than two systems.

**Nothing here is an atmosphere to write.**
`DiligentFX/PostProcess/EpipolarLightScattering/` is a complete physically-based
implementation: **41 fields** in `EpipolarLightScatteringAttribs` and **six
technique enum families** (`EpipolarLightScatteringStructures.fxh:43-90` —
`LIGHT_SCTR_TECHNIQUE`, `CASCADE_PROCESSING_MODE`, `REFINEMENT_CRITERION`,
`EXTINCTION_EVAL_MODE`, `SINGLE_SCTR_MODE`, `MULTIPLE_SCTR_MODE`). It is the
largest single settings surface in the vendored tree. The retired `terrain_lab`
app drove it.

**And there is a second, smaller thing this ticket now owns: nothing draws a
sky at all.** `DiligentFX/Components/interface/EnvMapRenderer.hpp` is complete
and **this engine never constructs it** — `grep EnvMapRenderer engine/` returns
nothing. T0170.5 turned IBL on by generating a procedural equirectangular map
and integrating it into irradiance and prefiltered cubes, so the environment
*lights* the scene and is never *visible*. **An environment map the renderer
samples is not the same thing as a sky a player sees**, and T0087 closed having
delivered only the first.

### The decision this ticket must take first: sky-as-background or sky-as-source

They are not the same and the engine currently does one of them. Three
configurations, and the ticket picks deliberately rather than accreting:

1. **Static skybox** — `EnvMapRenderer` draws an authored HDR cubemap, and the
   same asset feeds `PrecomputeCubemaps`. Cheapest, and it is what most games
   ship. **This is T0087's remainder and it is near-term.**
2. **Procedural atmosphere** — `EpipolarLightScattering` draws the sky from sun
   direction, and the ambient must be re-derived as the sun moves.
3. **Both** — atmosphere for outdoors, authored cubemap for interiors, which is
   also what 87.8's local ambient question is really asking.

**The hard part is (2)'s second clause, not the sky.** Drawing a procedural sky
is cheap; keeping image-based ambient in sync means re-capturing and re-running
the prefilter chain as the sun moves, and `PrecomputeCubemaps` is measurably not
free — T0170.5 found it costs ~60 render passes and, done carelessly, exhausts
Diligent's per-frame dynamic heap and stalls **every later frame** in the
process. Read that note before amortising anything.

### The seam — and D32's promise is discharged by T0094, not by a second interface

**D32 (2026-08-06) said the sky shader would be "authored against an interface a
game can implement — the same default-methods shape as `IHpMaterial`".** Under
D40 there is **no sky shader of ours** to make overridable, so that promise
changes shape rather than lapsing:

- **A sky is a *frame* thing, not a *surface* thing.** It is a pass. A game that
  wants its own sky writes an `IRenderLayer` — **T0094's seam**, ordered before
  the world layers — and it is not an `IHpMaterial` override, because nothing
  about a sky is per-surface.
- **Which scattering technique, which sun colour curve, which cubemap: settings**
  (T0078), not hooks.
- **Building a second `IHpMaterial`-shaped interface for the sky would be the
  ninth mechanism** D35's first half forbids. Say so here rather than
  rediscovering it.

## Done when

- [ ] **The sky-as-background vs sky-as-source decision is recorded**, with what
      was rejected — before anything is wired
- [ ] **A skybox is visible**, via `EnvMapRenderer`, in the right place in the
      render order (after opaque with depth test on, so it fills only unwritten
      pixels — T0087's note, inherited)
- [ ] **An environment map is authorable per scene as an asset** and reaches
      both the draw and `PrecomputeCubemaps` — the seam T0170.5 left open when
      it shipped a *procedural default* precisely so a game could supply a real
      one
- [ ] A procedural sky driven by sun direction, not a fixed cubemap
- [ ] The sun is a directional light whose direction, colour and intensity follow
      time of day
- [ ] Sunset/sunrise reddening falls out of scattering rather than being faked
- [ ] **Ambient/IBL updates as the sky changes** — amortised, and **measured
      against T0170.5's dynamic-heap trap**, not merely spread over frames
- [ ] Time of day is controllable: paused, scrubbed, or advancing at a set rate
- [ ] Night: moon or a night sky, and ambient that does not go pure black
- [ ] **87.8 is dispositioned** — local ambient control for interiors under a
      scene-global environment. "Rejected, scenes will be lit to tolerate it" is
      an acceptable answer; silence is not
- [ ] Cost is bounded — sky and probe updates must not spike the frame
- [ ] **The matrix rows flip** — `EnvMapRenderer` (🔧) and atmospheric
      scattering (🔌) in
      [`12-vendored-capabilities.md`](../../documentation/12-vendored-capabilities.md)

## Subtasks

- [ ] 88.1 **Record the sky-as-background vs sky-as-source decision.** First
- [ ] 88.2 **Construct `EnvMapRenderer` and draw a skybox** — the static half,
      independent of everything below, and the smallest thing on this ticket
- [ ] 88.3 **Environment map as a scene-authored asset** (HDR cubemap or
      equirectangular), reaching both the draw and `PrecomputeCubemaps`.
      Inherited from T0087 (87.1/87.5)
- [ ] 88.4 **Construct and drive `EpipolarLightScattering`**, and **choose which
      techniques we support** — see the compute note below; 88.1's decision and
      T0150's absence both constrain this
- [ ] 88.5 **Its settings as reflected scene data** — 41 fields is far too many
      to expose raw; decide what a scene author sees and what is a quality tier
- [ ] 88.6 Time-of-day service, with rate control and scrubbing. **A service,
      not a component** — global state, so a project-scoped autoload (T0076)
- [ ] 88.7 Sun direction from time, latitude and date; colour and intensity from
      elevation
- [ ] 88.8 **Dynamic environment probe capture and prefilter, amortised** —
      read T0170.5's dynamic-heap finding first
- [ ] 88.9 Night sky and moon
- [ ] 88.10 **87.8: local ambient control**, or its recorded rejection
- [ ] 88.11 Editor: scrub time of day and see it update live
- [ ] 88.12 Profiling zones — sky and probe updates are easy to make expensive
- [ ] 88.13 **Add the rows before building** — D40

## Notes / findings

### Surveyed 2026-08-06 — scattering is **not** fully blocked behind compute (T0150)

The natural assumption is that `EpipolarLightScattering` waits for the compute
subsystem this engine does not have. **Measured, only its high-quality path
does.**

| Needs compute | Does not |
|---|---|
| LUT precompute — 6 passes, **one-time**, only for `SINGLE_SCTR_MODE_LUT` and `MULTIPLE_SCTR_MODE_UNOCCLUDED/OCCLUDED` | ray march, coordinate texture, coarse inscattering, min/max shadow map, luminance, sun disc, ambient sky light |
| `RefineSampleLocations` — **per-frame**, only under `LIGHT_SCTR_TECHNIQUE_EPIPOLAR_SAMPLING` | — |

Everything in the right column is `InitializeFullScreenTriangleTechnique`, i.e. a
pixel shader, **including the min/max shadow-map steps despite their names**.

**So a reduced configuration ships before T0150**: `LIGHT_SCTR_TECHNIQUE_BRUTE_FORCE`
+ `SINGLE_SCTR_MODE_INTEGRATION` + `MULTIPLE_SCTR_MODE_NONE` needs **zero**
compute, at a real cost — no epipolar acceleration, no multi-scattering. Full
quality needs T0150 first. That is a sequencing option this ticket did not have
before, and 88.1 should decide which it targets rather than inheriting the
assumption that it is blocked.

The full settings surface — ~40 struct fields and 6 technique enums, the largest
of anything surveyed — is inventoried in
[../../documentation/12-vendored-capabilities.md](../../documentation/12-vendored-capabilities.md).

**From D32 (2026-08-06):** when the sky shader is built, it is authored
against an interface a game can implement — the same default-methods shape as
`IHpMaterial` — not as a sealed engine file. One design constraint now; the
retrofit D27 exists to prevent, later.

**Dynamic ambient is the expensive part, not the sky itself.** Drawing a
procedural sky is cheap. Keeping image-based ambient in sync means re-capturing
the environment and re-running the prefilter chain (T0087) as the sun moves — and
doing that every frame is far too slow.

The standard answer is amortisation: update the probe every N frames, or spread
the mip chain across frames, and interpolate between two prefiltered sets. Decide
this early, because "recompute the probe each frame" works fine in a test scene
and collapses in a real one.

**Time of day is a service, not a component** — it is global state, so it belongs
in a project-scoped autoload (T0076). Systems read from it rather than each
tracking their own notion of time.

Do not let night go fully black. Physically it nearly does; visually it makes the
game unplayable. A moonlight floor and raised ambient are deliberate art
decisions, not bugs to fix.
