# T0088 — The atmosphere: sky, fog, light shafts and time of day

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 466 |
| **Created** | 2026-08-03 |
| **Merged** | 2026-08-08 — **absorbed [T0089](../completed/../completed/0089-fog-and-atmospherics.md) (fog and atmospherics) and [T0091](../completed/../completed/0091-volumetric-fog.md) (volumetric fog and light shafts)**, both ❌ SUPERSEDED, and **moved from phase 11 to phase 4**: this is a render component, not a world system, and the misfiling was itself a symptom of the split. See *Why these were three tickets and are now one* |
| **Refs** | [../completed/0087-environment-lighting.md](../completed/0087-environment-lighting.md) — **this ticket inherited T0087's remainder when it closed 2026-08-08**: the visible skybox (87.2), the per-scene environment asset (87.1/87.5) and 87.8's local ambient control. T0170.5 delivered the IBL half and shipped a *procedural default* sky specifically so that a real one is the game's to supply; [../completed/../completed/0089-fog-and-atmospherics.md](../completed/../completed/0089-fog-and-atmospherics.md) and [../completed/../completed/0091-volumetric-fog.md](../completed/../completed/0091-volumetric-fog.md) — **absorbed here**; [0150-compute-pipelines.md](0150-compute-pipelines.md) — gates the high-quality techniques only, see the compute note; [0094-gameplay-extensible-rendering.md](../inprogress/0094-gameplay-extensible-rendering.md) — **a game's own sky is a pass on that seam**, which is how D32's promise is discharged here; [0171-expose-not-replace-sweep.md](../completed/0171-expose-not-replace-sweep.md); **D32**, **D40** |

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

### Why these were three tickets and are now one

**Sky, fog and volumetric fog are one subject split by *feature variant*, and two
of the three were filed in the wrong phase.** The evidence is on the tickets
themselves rather than in an opinion about them:

- **T0089's 89.4 was "derive fog colour from the sky/sun direction (T0088)" and
  its 89.7 was "keep parameters shared with volumetric fog (T0091), not
  duplicated".** Two of seven subtasks existed **only to keep three tickets in
  sync with each other** — which is the signature of one thing split into three,
  not of three things that happen to be adjacent.
- **T0091's sun case was never its own.** `EpipolarLightScattering::FrameAttribs`
  takes a single `const LightAttribs*` (`EpipolarLightScattering.hpp:73`) — one
  **directional** light — so light shafts from the sun are a configuration of the
  component *this* ticket owns. T0091's own note said to *"evaluate it before
  building froxel volumetrics… this is genuinely worth an afternoon"*, which is
  an evaluation that can only be run from inside this ticket.
- **T0088 and T0089 carried the same D32 note verbatim**, about authoring the
  sky/fog shader against a game-implementable interface. One design constraint,
  written twice, in two tickets, for one component.
- **T0088, T0089 and T0091 were all phase 11, "World & environment".** They are a
  DiligentFX render component and two things that read its output; the misfiling
  is what let them drift apart from T0148's chain, which they belong beside.

**And the split's original justification expired with D40.** These were separated
by *implementation stage* and *feature variant* — cheap fog now, expensive fog
later, sky separately — which is a sound way to divide work that is **ours to
build**. The implementation is Diligent's, so the "variants" are enum values and
settings on one component, and three tickets could only produce three partial
designs of it.

**What is genuinely a separate concern is kept separate *inside* this ticket, not
outside it.** Froxel volumetrics for point and spot lights is a Very Complex
subsystem that upstream does not have, whose honest likely outcome is a recorded
decline. It is **88.20, deferred with a written trigger** — the shape T0146.7
established for tessellation, which is the worked precedent for a large deferred
capability living inside its natural owner rather than in a ticket of its own.

## The decision this ticket must take first: sky-as-background or sky-as-source

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

### Fog — absorbed from T0089
- [ ] **Distance fog** with controllable colour, density and falloff — linear and
      exponential
- [ ] **Height fog**, so low-lying mist is possible
- [ ] **Fog colour derives from the sky** rather than being set independently — a
      fixed grey fog under a sunset sky looks immediately wrong, and this is now
      a read within one ticket rather than a cross-ticket obligation
- [ ] Fog is applied correctly relative to **transparency and to the skybox** —
      the approach decided when the transparent queue is built (T0045), not
      afterwards
- [ ] Fog parameters are **scene-authored, serialized and animatable at runtime**,
      for weather transitions (T0090)
- [ ] **One parameter set**, shared by distance, height and any volumetric tier —
      not three that must be kept in sync

### Light shafts and volumetrics — absorbed from T0091
- [ ] **The sun-shaft case is delivered through `EpipolarLightScattering`**, and
      what it does *not* cover is stated in terms of what a scene actually looks
      like — not in terms of what is theoretically missing
- [ ] **Froxel volumetrics are dispositioned** — built, or **declined with the
      trigger recorded**. A decline is a legitimate outcome (88.20)

### And the record
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

**Fog — absorbed from T0089. Cheap, high-value, and it does not wait for the
atmosphere.**

- [ ] 88.14 **Distance and height fog** in the shading path, linear and
      exponential. The depth read it wants **already exists**:
      `HpSceneViewDepth(In.ScreenUV)` gives view-space distance in metres from
      the opaque pass, and `HpViewDepth` linearises any device depth without this
      ticket knowing the projection's convention (T0147/D37)
- [ ] 88.15 **Fog parameters as one scene-authored, serialized set** (T0078),
      animatable at runtime for T0090's weather transitions — **shared** by every
      tier rather than duplicated per tier
- [ ] 88.16 **Fog colour from the sky/sun direction** — now an internal read
- [ ] 88.17 **Correct interaction with transparents and the skybox.** Fog on
      opaque geometry is straightforward; transparent surfaces need it applied
      consistently or they visibly float outside the fog. **Decide when T0045's
      transparent queue is built, not afterwards**
- [ ] 88.18 **A frame-wide fog is a full-screen pass and belongs in T0148's
      chain**, reading the same `scene.colour.snapshot` / `scene.depth.snapshot`
      targets rather than declaring its own — **the two must not each copy the
      frame**. A *per-material* fog is a blended surface today and needs nothing

**Light shafts and volumetrics — absorbed from T0091.**

- [ ] 88.19 **Deliver the sun-shaft case through `EpipolarLightScattering`**, and
      then measure what is still missing with the component actually running.
      This was T0091's 91.8 and it is now reachable
- [ ] 88.20 **Froxel volumetrics: deferred, with a trigger.** Upstream has
      nothing for beams from **point and spot** lights — a torch in mist, a
      spotlight in a smoky room — and epipolar scattering cannot do them at all.
      **The trigger is a game the studio intends to make needing a visible
      spotlight beam badly enough to pay froxel prices**, and the answer is a
      *decision*, not an assumption in either direction: ⬆️ genuinely ours has to
      be argued (D40), and **a recorded decline closes this cleanly**. If it is
      built, it is: a view-aligned froxel grid; light injection sampling shadow
      maps (**T0086 must expose sampling without the shading pass** — its notes
      already record this as one of three such consumers); ray integration;
      application to opaque then transparents; **temporal reprojection with
      jitter, which is not optional**; local density volumes; and quality tiers.
      It is **Very Complex** and it is the only part of this ticket that is not
      configuration

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

---

## Absorbed from T0089 — its findings, kept verbatim

*The `89.x` numbers refer to its old subtask list; the mapping is in its
`## Descoped` table.*

**From D32 (2026-08-06):** when the fog shader is built, it is authored
against an interface a game can implement — the same default-methods shape as
`IHpMaterial` — not as a sealed engine file. One design constraint now; the
retrofit D27 exists to prevent, later.

**Transparency plus fog is where this goes wrong.** Fog applied per-pixel on
opaque geometry is straightforward; transparent surfaces need it applied
consistently or they visibly float out of the fog. Decide the approach when the
transparent queue is built (T0045), not afterwards.

**Fog colour should usually follow the sky.** A fixed grey fog under a sunset sky
looks immediately wrong. Sampling the sky in the view direction is a cheap and
large improvement, and it is what makes T0088's time of day feel cohesive.

**Volumetric fog is wanted and is now T0091** — froxel grids, light injection and
temporal reprojection are a large enough job to be their own ticket. This one
stays deliberately cheap: distance and height fog cost almost nothing and cover
the common case, and they remain the fallback on lower quality settings where
volumetrics are disabled. The two should share parameters (colour, density,
height falloff) rather than each having their own.

Fog also usefully hides LOD transitions (T0040) and the far plane, which makes it
a performance tool as much as an aesthetic one.

---

## Absorbed from T0091 — its findings, kept verbatim

*The `91.x` numbers refer to its old subtask list; the mapping is in its
`## Descoped` table. These are the notes that make 88.20 buildable if its trigger
ever fires.*

**Diligent already ships `EpipolarLightScattering`**, which produces sun shafts
specifically and is what the old `terrain_lab` used. If the sun is the only light
that needs shafts, that may cover the visually important case at a fraction of the
cost. **Evaluate it before building froxel volumetrics** — this is genuinely worth
an afternoon, because the two solve overlapping problems with very different price
tags.

Froxel volumetrics earn their cost when *point and spot* lights need to produce
beams — a torch in mist, a spotlight in a smoky room — which epipolar scattering
does not do.

**Temporal reprojection is not optional.** A froxel grid dense enough to look
smooth without it is far too expensive. Jitter the sample position per frame and
blend with the reprojected previous frame. The consequence is the usual temporal
artefact set: ghosting behind moving lights, and instability on camera cuts, both
of which need handling rather than discovering.

**Resolution is the main cost lever.** Typical grids are ~160×90×64 — far below
screen resolution, because fog is low frequency. Do not be tempted to raise it to
fix an artefact that is actually temporal.

Transparent surfaces need the fog integral evaluated at their depth, not the
opaque depth behind them, or glass and particles sit visibly outside the fog.
