# T0156 — Parallax under triplanar, and the silhouette question

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 461 |
| **Created** | 2026-08-06 |
| **Refs** | [0155-terrain-rendering.md](../open/0155-terrain-rendering.md) — **the driving consumer**, and the reason this is High rather than Medium; T0141.7 (parallax occlusion) and T0141.8 (triplanar) — this is their intersection and both landed 2026-08-06; T0141.9 (tessellation) — **156.6 may change its trigger's urgency**; it is **[T0146.7](../completed/0146-vertex-stage-hook.md)** since T0141 closed on 2026-08-06, and the trigger is now *"when a silhouette must change at a density the mesh does not carry"*; [0153-surface-detiling.md](../open/0153-surface-detiling.md) — same family, a multi-tap surface-stage technique, and 153.1's interface question is shared; [0151-shader-variants-and-compile-cost.md](../open/0151-shader-variants-and-compile-cost.md) — another permutation axis; **D26**, **D27**, **D30** rung 2 ([../../documentation/02-decision-log.md](../../documentation/02-decision-log.md)) |

## Why

**The owner's ask:** parallax under triplanar as *"a proper option for game devs
to have"*, and the driving case is terrain — making rock faces read as rock
without modelling them.

**Today they are mutually exclusive.** `Material.hpp` states it on the field:

> *"`heightTexture`'s parallax is **inert** [under triplanar], since there are no
> UVs to displace. The per-slot UV selectors and channel transforms are all
> bypassed."*

That is an honest description of what 141.8 shipped, **not a claim that the
combination is impossible.** 141.8 was scoped to triplanar alone. This ticket is
the intersection nobody wrote.

## The enabling fact — the expensive half of POM is free here

`HpSurface.slang` projects triplanar on **world axes**:

```
t.uvX = In.WorldPos.zy * scale;
t.uvY = In.WorldPos.xz * scale;
t.uvZ = In.WorldPos.xy * scale;
```

**Each projection's tangent frame is therefore a constant.** For the Z
projection, tangent = world X, bitangent = world Y, normal = world Z. No frame
construction at all.

141.7 had to build a **derivative-based tangent frame** because general UV
mapping needs one — and that was the costly, fiddly part. Under triplanar it
evaporates. The march itself is the same code; only the frame and the view-vector
rotation change, and both are swizzles mirroring the UV swizzles above.

**So this is smaller than it looks.** Do not cost it as "POM again, three times".

## And the performance shape is favourable, which is why terrain is the good case

The weights are sharpened hard — `pow(abs(n), float3(4.0, 4.0, 4.0))` — so on
most pixels one axis dominates and the others are ~0:

| Surface | Contributing axes | Cost |
|---|---|---|
| Flat terrain (normal ≈ +Y) | one | **1×** |
| Slopes and cliff faces | two | **~2×** |
| A 45° corner in all three | three | 3× — rare under a 4th power |
| **An axis-aligned cube face** | one | **1×** |

**The cost peaks where the value peaks, which is the property that makes this
worth doing.** POM displaces along the view vector projected into the tangent
plane, so it does **nothing** head-on and **everything** at grazing angles. Flat
ground under the camera is 1× *and* shows no parallax anyway; a cliff face seen
across is ~2× *and* is exactly where rock must look like rock.

## Done when

- [x] A **triplanar material with a height map shows parallax**, measured — a
      frame-difference number against the same material with height off, in the
      shape 141.7 used (zero-scale must be **bit-identical**; a real scale must
      move a measurable amount) *(2026-08-06 — zero-scale **0.0 mean abs, both
      devices**; 0.08 scale moves the oblique UV-less quad by 7.38 (RTX 4070) /
      7.55 (llvmpipe) mean abs per channel, unlit. See notes)*
- [x] **Cost is measured per weight configuration** — one, two and three
      contributing axes — and the numbers are in the ticket, not asserted
      *(2026-08-06 — table in notes, both devices, isolated runs; and the shape
      is layer-count-dominated, not axis-count-dominated, which is worth
      reading)*
- [x] **Blend-region seams are characterised**, and either mitigated or accepted
      with the reasoning written down *(2026-08-06 — characterised on a
      45° all-band quad and a terrain whose slope sweeps the thresholds:
      no seam, no contour, and high-frequency contrast **rises** in the band
      under POM. Accepted, reasoning in notes)*
- [x] It is a **game-dev-facing option**, not an engine-internal — reachable from
      a material, and overridable from a game shader per D30 rung 2
      *(2026-08-06 — `Material::heightTexture` + `Material::triplanar` now
      compose, no new field; a game module overrides the same
      `IHpMaterial` methods as ever, and the triplanar helpers are in scope
      for its overrides)*
- [x] **It works on a terrain-shaped surface**, not only on a cube — the cube is
      the trivial case and proving it there proves little *(2026-08-06 — a
      33×33 gaussian-hill heightfield, no UVs; zero-scale 0.0, displaced 3.35
      (RTX) / 3.46 (llvmpipe) mean abs per channel with half the frame sky)*
- [x] **What is not delivered is written down**, 156.6 included *(2026-08-06
      — the "Not delivered, and why" note below)*

## Subtasks

- [x] 156.1 **Per-axis march with the axis-aligned frames.** The enabling fact
      above; rotate the view vector per projection with the same swizzle the UVs
      use, and reuse 141.7's march *(2026-08-06 — `HpParallaxMarch` extracted
      frame-agnostic; the triplanar basis marches each live projection with
      `float3(V.zy, abs(V.x))` and the two other swizzles. The prediction
      held: no frame construction at all)*
- [x] 156.2 **Early-out on low weights**, and measure the 1 / 2 / 3-axis cost.
      A weight below a small threshold should skip the march entirely rather
      than marching and multiplying by ~0 *(2026-08-06 — weights under 1% are
      zeroed and the rest renormalised; both the march and the texture taps
      skip a dead axis. Numbers in notes)*
- [x] 156.3 **Height sampling through the three-projection path** — the height
      texture is sampled by UV today and needs the same treatment as the other
      maps under triplanar *(2026-08-06 — the march samples `g_HeightMap` at
      each projection's own UVs with that projection's gradients; no UV0
      involved, and the flag-stripping that required UV0 now exempts
      triplanar)*
- [x] 156.4 **The blend-region seam.** Where two axes mix, each marched
      independently, displacements disagree and can show a seam — and on terrain
      those regions sit exactly where flat becomes steep, which is where the
      camera looks. Characterise it first, then choose: sharpen the weights
      further (narrower band, harder transition), march in a shared world-space
      direction, or accept it. **Do not skip the characterisation** — this is the
      one quality risk that is specific to this combination *(2026-08-06 —
      characterised, then **accepted**; measurements and the argument against
      both mitigations in notes)*
- [x] 156.5 **The interface question**, shared with T0153.1: this is another
      multi-tap surface technique, and `surfaceCoordinates` (141.7) expresses a
      *coordinate transform*, not a multi-tap blend. Decide with 153.1 rather
      than separately, and reference both ways — two tickets inventing two seams
      for the same shape is the outcome to avoid *(2026-08-06 — **decided as
      one seam, written into 153.1 as its implementation contract**, with the
      back-reference in T0153's Refs. See "156.5 decided" in notes)*
- [x] 156.6 **Evaluate silhouette POM** — see below. **Evaluation, not
      implementation**; the output is a recommendation with numbers and a
      decision recorded either way *(2026-08-06 — evaluated against current
      sources and this engine's own measurements: **recommendation against**,
      T0146.7's trigger stands reinforced, both tickets updated. See "156.6
      evaluated" in notes)*
- [x] 156.7 **Measure on a terrain-shaped surface**, not the cube *(2026-08-06
      — the heightfield case above, PPMs in `test-frames/triplanar_terrain_*`)*

## 156.6 — silhouette POM, deferred for evaluation

Raised by the owner: *"there is also a SilPOM or SPOM that is expensive to run,
but maybe cheaper than tessellation."*

**The gap it addresses is real and this ticket does not otherwise close it.**
POM is an illusion computed inside the surface, so **the silhouette stays flat** —
a POM cliff has a straight edge against the sky, at any quality setting. Today
the only answer is 141.9 (tessellation), which is deferred behind the trigger
*"when a silhouette must change"*.

The family of techniques marches **past** the original surface boundary — using
an extruded shell or per-triangle prism — and discards fragments whose ray exits
without hitting, producing a silhouette at pixel-shader cost rather than by
adding geometry.

**What the evaluation must establish, because these are the costs that decide
it:**

- **`discard` disables early-Z on most hardware.** That is not a small constant —
  it changes the cost of everything drawn behind, and on terrain that is a lot.
- **Shell overdraw** — the extruded volume is drawn and marched, not the surface.
- Whether it composes with **triplanar** at all, which is this ticket's whole
  subject, or only works UV-mapped.
- Whether it is genuinely **cheaper than tessellation** for our case, or only
  cheaper in the abstract. Tessellation LODs away with distance; a shell may not.

**Prior art to check rather than assume** — relief mapping with silhouettes
(Policarpo & Oliveira) and the quadtree-displacement family are the usual
starting points, and both predate current hardware, so their cost conclusions
may not transfer. Measure on this engine's target before recommending.

**And it may change 141.9's urgency in either direction** — if silhouette POM is
good enough, tessellation's trigger recedes; if it is not, the evaluation is the
argument for pulling 141.9 forward. Record which, and update 141.9's trigger
accordingly with a two-way reference.

## Not in scope

- **Tessellation itself** — 141.9, and it is genuinely different work: new hull
  and domain shaders, which `PBR_Renderer` never creates, so it is C++ pipeline
  construction rather than shader text. The two are complementary in production
  (tessellate near, POM mid, normal-map far), not alternatives.
- **De-tiling under triplanar** — T0153, same family and same interface question,
  but a separate technique.

## Closed 2026-08-06 — the intersection exists, measured on both devices

**The ticket opened on** `Material.hpp` stating parallax is *inert* under
triplanar. It now composes: a triplanar material with a height map marches
each live world-axis projection through the height field, on meshes with no
UVs at all, and the field comment says so instead. The enabling fact held
exactly — the constant frames made this a fraction of 141.7's work — and the
one design change the measurements forced (unifying every triplanar tap on
`SampleGrad` with analytic gradients) made the zero-scale bit-identity claim
structural rather than numerical.

| Claim | Evidence |
|---|---|
| Zero scale marches nowhere | **0.0 mean abs vs the no-map material — bit-identical on both devices**, quad and terrain |
| A real scale moves the frame | 7.38 / 7.55 mean abs per channel (RTX 4070 / llvmpipe) on the oblique UV-less quad; 3.35 / 3.46 on the terrain, unlit throughout |
| Cost lands where the value does | 0.066 ns/px facing, ~1.25 ns/px worst measured (RTX); table and the layer-count finding in notes |
| The blend band does not seam | no contour on a terrain sweeping the thresholds; high-frequency contrast **rises** 9.67 → 14.60 in the all-band 45° case |
| The seam question is decided once | 156.5, written into T0153.1 as its implementation contract, referenced both ways |
| Silhouette POM is closed with evidence | 156.6, recommendation against; T0146.7 reinforced, referenced both ways |

**Final verification, this tree, 2026-08-06:** `zig build all` EXIT 0 with no
`^FAILED:|error:` lines; `zig build test -Dtest=all` fast 310 + integration
89, both targets, zero failures; the gpu suite 29 cases green on both targets
**run in isolation** (the concurrent run contaminates the benchmark, recorded
in notes); `zig build docs` green with `docs/shaders/IHpMaterial.md`
regenerated. On this host the Windows target ran as a real Windows process
via WSL interop on an **NVIDIA RTX 4070 Laptop GPU** and the Linux target ran
natively on **llvmpipe** — every pixel and cost claim above names which.

## Notes / findings

### 156.1–156.4, 156.7 landed 2026-08-06 — the enabling fact held, and the numbers

**What landed.** `HpParallaxMarch` is 141.7's march with the tangent frame
hoisted out (the only part that differed); `HpTriplanarComputeBasis` marches
each live projection in its constant world-axis frame — the view-vector
rotation is the same swizzle the UVs use, `float3(V.zy, abs(V.x))` for X and
so on, with `abs()` because a projection serves both facings of its plane. The
basis is **memoised in per-invocation statics** under the height permutation,
because five channel getters ask for it and only the first should pay for the
marches. C++ side: the height flag no longer excludes triplanar
(`SceneRenderer` resolves both), and the no-UV0 flag strip exempts triplanar,
which feeds its marches from world position. `heightScale` under triplanar is
**projection units** (`1/triplanarScale` metres), so relief depth tracks
tiling exactly as it does under UV mapping — documented on the field.

**Displacement, measured (unlit, so every differing pixel is a displaced
texel).** Isolated runs per target; RTX 4070 Laptop = the Windows target via
WSL interop, llvmpipe = the Linux target on this host:

| scene | zero-scale vs no-map | 0.08 scale vs no-map (RTX / llvmpipe) |
|---|---|---|
| UV-less oblique quad (two live axes) | **0.0 — bit-identical, both devices** | **7.38 / 7.55** mean abs per channel |
| UV-less terrain heightfield | **0.0 — bit-identical, both devices** | **3.35 / 3.46** (half the frame is sky) |

The existing suites hold: UV-mapped parallax 15.47/15.69 displaced with
zero-scale at 0.0, triplanar-without-height variation 11.28/11.32 against a
flat 0.0 control. Full gpu suite 29 cases green on both targets, fast 310 +
integration 89 green on both.

### The sampling unification, which is what made "bit-identical" true

The first cut sampled the marched projections with `SampleGrad` and left the
no-height path on `SampleBias` — and zero-scale then missed bit-identity by
0.0096 (RTX) / 0.0896 (llvmpipe) mean abs: implicit-derivative bias sampling
and explicit-gradient sampling round mip selection differently, and the
early-out reweighting existed in only one permutation. **The fix was to unify,
not to loosen the assertion**: `HpTriplanarSample` now always uses
`SampleGrad` with the basis's analytic gradients (`ddx(WorldPos)` swizzled and
scaled exactly as the UVs are), scaled by `exp2(mipBias)` since `SampleGrad`
has no bias argument, and the sub-1% weight floor applies in every
permutation. Two consequences worth naming:

- **Plain triplanar (141.8's path) changed**: `SampleBias` → `SampleGrad`
  (measured before accepting: under 0.1 mean abs per channel, mip rounding at
  tile boundaries) and it now **skips taps for dead axes** — flat ground pays
  one tap per texture instead of three, a real saving on 141.8's shipped
  path, legal only because the derivatives are explicit.
- Zero-scale bit-identity is now structural: both permutations issue the same
  instructions on the same inputs, and the march at zero scale returns its
  input exactly.

### Cost per weight configuration, and the shape of it (156.2)

384×384 view, quad filling the frame, 32 draws per synchronised iteration
(one draw per sync put the march under the sync noise on the RTX and produced
a negative "cost"), isolated runs — the concurrent two-target run contaminates
llvmpipe by CPU contention. `march cost` = height-on minus height-off,
normalised by covered pixels:

| configuration (live axes) | RTX 4070 Laptop | llvmpipe |
|---|---|---|
| one axis, facing (~8 layers) | **0.066 ns/px** (+0.010 ms/draw) | 16.4 ns/px (+2.42 ms/draw) |
| two axes, 45° oblique (~21+11 layers) | **1.243 ns/px** (+0.183 ms/draw) | 53.7 ns/px (+7.92 ms/draw) |
| three axes, corner (~26+26+9 layers) | **1.213 ns/px** (+0.111 ms/draw) | 92.7 ns/px (+8.45 ms/draw) |

**The finding: cost tracks the summed per-axis layer count, not the axis
count.** The march adapts 8–32 layers by grazing angle per projection, so a
two-axis surface seen obliquely (one projection near-grazing) costs about the
same as a three-axis corner seen comfortably — the ticket's 1×/2×/3× table is
the right *intuition* but the multiplier rides on viewing angle as much as on
weights. The properties that matter survive intact: flat ground facing the
camera is nearly free (0.066 ns/px), and the worst case measured is ~1.25
ns/px on the RTX — about 2.6 ms if an entire 1080p frame were a grazing
two-axis cliff, which no real frame is. A projection edge-on to the *view*
(`|V·axis| ≤ 1e-4`) early-outs flat by 141.7's existing guard, which is the
correct limit: its parallax offset diverges.

### 156.4 — the blend seam, characterised and accepted

**Method.** Two probes on the RTX frames: a quad tilted 45° so its *entire
surface* is the two-axis blend band at weights (0.5, 0, 0.5), and the terrain,
whose slope sweeps continuously through both the 1% weight floor (~18° tilt)
and the 45° equal-weight line — so any threshold pop is a visible contour and
any band artefact sits on the hillsides.

**Measured.** No seam and no contour: the ×3-amplified on/off difference image
shows displacement active across the whole terrain with no line where the
axis count changes. On the all-band 45° quad, high-frequency contrast
(mean-abs-Laplacian of luma) went **9.67 → 14.60** with the marches on —
the disagreement between the two projections' displacements superimposes as
*extra* fine detail, not as the contrast collapse ghosting would produce; the
facing one-axis reference barely moves (15.28 → 15.81). By eye (crops in the
session record): the hillside reads as carved rock, and the 45° quad at
extreme grazing shows POM's classic stretch streaks, which single-axis
UV-mapped POM shows at the same angle — not a blend artefact.

**Why no hard seam exists to find**: the weights are continuous, so
independent marches cannot disagree *discontinuously*; the only discontinuity
in the whole path is the 1% floor, whose pop is bounded by the floor itself —
one part in a hundred of the blend, under one 8-bit level in this content.
In the band, plain triplanar is already a superposition of two projections;
the marches displace the two components of that superposition without adding
a new mechanism.

**Decision: accept, and the mitigations are argued against rather than
untried.** Sharpening the weights further would narrow every triplanar blend
globally (changing 141.8's shipped look) to fix an artefact that measurement
cannot find; marching all projections in a shared world-space direction would
break each march's consistency with its own height-field parametrisation —
the displaced sample would no longer be the view ray's intercept with that
projection's height field — trading a measured non-problem for a small
correctness error everywhere. Revisit trigger: a real asset showing a band
artefact this characterisation missed.

### Traps found on the way, for whoever benchmarks next

- **A few hundred offscreen renders with no frame boundary exhaust Diligent's
  dynamic heap** — "Space in dynamic heap is exhausted!" once per frame, and
  every draw after it silently writes nothing. `stats.submitted` still counts
  the draw, so half the first benchmark's numbers were measurements of a
  blank frame. `RenderLayer::onRender()` (present) is what advances the frame
  and recycles the heap; the bench calls it every iteration now, and
  re-checks coverage *after* the timed loop so a mid-run blackout fails
  loudly.
- **`zig build linux` leaves a stale Windows test `.exe`** — the known
  stale-DLL trap in another coat. The giveaway was assertion line numbers
  from a previous version of the source in the "fresh" run's output.
- **A benchmark configuration can rasterise nothing and still time
  plausibly.** The first three-axis quad was nearly edge-on through the
  camera plane; `covered > 0` is now asserted per mode.

### 156.5 decided 2026-08-06 — one seam, at the tap, and this ticket stays out of the contract

**The observation that settles it**: triplanar(+parallax) and de-tiling are
the *same shape at different levels*. Both map one logical sample onto N
physical taps blended by weights — triplanar fans one fragment into three
projected sites; de-tiling fans one site into 3–4 offset taps. The seam they
share is therefore at the **logical tap**: a method that answers "fetch one
logical sample at this coordinate with these gradients", whose default is a
single `SampleGrad` and whose de-tiled overrides are the tiers. Coordinate
decisions (`surfaceCoordinates`, the triplanar basis and its marches) stay
*above* the seam, and so does blending logical taps (the triplanar weights) —
which is exactly why the two techniques compose without knowing about each
other: 3 live axes × 3 de-tile taps is 153.6's nine-samples number, arrived
at structurally.

**The decision is recorded as 153.1's implementation contract** — signature
constraints included (resource params, explicit gradients, slice,
`exp2(mipBias)` folding, and the slangc static-specialisation check to run
before committing to resource-typed interface parameters). **This ticket adds
nothing to `IHpMaterial`**: a sampling seam with no de-tiling behind it would
violate the contract's own "nothing is exposed before the system behind it
exists" rule, so the marches live in the standard material's private helpers
— already shaped as "N logical taps through one fetch expression"
(`HpTriplanarSample`), so routing them through the seam when T0153 lands is
mechanical. Two tickets, one seam, referenced both ways: the outcome 156.5
existed to avoid is avoided.

### 156.6 evaluated 2026-08-06 — silhouette POM: recommendation against, and the trigger it reinforces

**Method.** A web-research pass over the primary literature, current hardware
evidence and shipping engines (sources below), anchored by this ticket's own
measurements of the march it would extend. The prior-art cost conclusions are
all 2005–2010 and were treated as suspect per the brief; what follows
distinguishes measured from asserted throughout.

**The recommendation: do not build it. Tessellation (T0146.7) remains the
silhouette answer, and its trigger stands — reinforced, because this
evaluation closes the possible pixel-shader escape hatch behind it.**

The grounds, in the order they decide it:

1. **It does not compose with triplanar, which is this ticket's whole
   subject.** Every silhouette-capable variant marches **one** height field
   in **one** parametrisation against the true surface boundary — Policarpo
   & Oliveira's quadric-fit relief mapping (I3D 2006), Dachsbacher &
   Tatarchuk's per-triangle prisms (SI3D 2007), the shell-mapping and
   quadtree families. Triplanar has no such field: its surface is a weighted
   superposition of three height fields whose weights vary across exactly
   the geometry a silhouette crosses, so "where does the ray exit the
   surface" has no single well-defined answer. The research found **no
   published work combining the two** — only one practitioner thread
   asserting artefacts and ~3× cost, reported as opinion, with no
   counter-example anywhere. The one recent production "POM silhouette"
   (Crimson Desert, GDC 2025) is a UV-bounds opacity-mask hack —
   definitionally unavailable to a projection that has no UV bounds.
2. **The real cost lands on everything drawn behind, and terrain is the
   worst surface to pay it on.** The mechanism is `discard`: current,
   measured evidence (therealmjp, April 2025, RDNA3; AMD's live RDNA guide
   says avoid `discard` outright) shows it *degrades* hierarchical-Z for the
   draw rather than fully disabling early-Z — ~32% more pixel-shader
   invocations in the measured front-to-back scene, **even when no pixel is
   ever discarded**. Terrain is the scene's dominant occluder; making its
   draw hostile to depth culling taxes the whole frame. **Not measured on
   this engine**, stated plainly: a meaningful local measurement needs an
   occlusion-heavy scene and belongs to T0045's queues if it is ever
   worth building; the engine already avoids `discard` on opaque draws for
   this reason (the alpha-mask cutout is compile-time gated, argued in
   `HpSurface.slang`).
3. **Shell/prism variants add geometry work that overlaps tessellation's
   without its generality** — per-triangle prism extrusion is vertex/
   pre-rasteriser machinery (T0146's territory) plus overdraw that the
   literature costs only as "longer rendering times" (no modern numbers
   exist; recorded as a gap, not glossed), and a shell does not LOD away
   with distance the way tessellation does.
4. **Nobody ships it for this problem.** CryEngine alone ships true
   silhouette POM, gated to its very-high tier and documented as heavier
   than POM; Unreal documents its POM node as *unable* to break silhouettes
   and answers with Nanite runtime tessellation + displacement (UE5.4, GDC
   2024); Unity HDRP and Godot 4 ship nothing silhouette-correct; Far Cry
   5's terrain builds real cliff geometry. Converging circumstantial
   evidence, not a stated consensus — but it all points one way.
5. **This engine's own numbers anchor the baseline** (the cost table above):
   the in-surface march costs 0.066–1.25 ns/px on the RTX 4070 depending on
   grazing. A silhouette variant multiplies the marched area (a shell is
   fatter than the surface), lengthens each march (past the boundary), and
   adds the culling tax — against a terrain silhouette that tessellation
   changes at geometry rates which LOD with distance.

**Consequence for T0146.7, recorded there with the back-reference**: the
trigger's wording stands — *when a silhouette must change at a density the
mesh does not carry* — and the evaluation coming back negative means the
pressure behind it is real: there is no cheaper pixel-shader answer waiting.
Whoever hits the trigger should not reopen silhouette POM without new
evidence (a published triplanar-compatible formulation, or hardware that
makes `discard` free).

**Sources** (full survey in the session's research report): Policarpo &
Oliveira I3D 2006; Dachsbacher & Tatarchuk SI3D 2007; Jeschke et al. EGSR
2007; Drobot, GPU Pro 1 (2010); therealmjp, "To Early-Z, or Not To Early-Z"
(Apr 2025, RDNA3-measured); AMD GPUOpen RDNA Performance Guide; CryEngine
docs (Silhouette POM); Epic forums + UE5.4 GDC 2024 (Nanite tessellation);
Unity HDRP POM node docs; 80.lv on Crimson Desert (GDC 2025); GDC 2018 Far
Cry 5 terrain talks. All primary silhouette-POM papers predate current
hardware by a decade-plus and none has a modern re-benchmark; that absence
is part of the finding.

### What this does *not* interact with, checked rather than assumed

**The normal map stays ignored under triplanar** (141.8's recorded decision)
and the marches do not change that: `shadingNormal` returns the geometric
normal in triplanar mode, so relief shows in the displaced albedo/ORM but not
in the lighting response. The two decisions are independent — when T0153-era
work reorients tangent-space maps per projection, the marched UVs are exactly
the coordinates that per-plane normal fetch will want, and nothing here has
to move. The unlit measurements above are unaffected either way. Also
checked: `loadTexture`'s sRGB decode of height maps (141.7's known issue,
T0097's territory) warps the height curve identically for the UV and
triplanar marches — monotonic, so displacement direction and every
differential assertion here are unaffected.

### Not delivered, and why — so nothing here is discovered as a gap later

- **Silhouette POM** — evaluated (156.6) and recommended against; a POM
  cliff still has a straight edge against the sky, and the answer to that
  remains T0146.7's tessellation, whose trigger this ticket reinforced.
- **The tap-level sampling seam on `IHpMaterial`** — decided (156.5) but
  deliberately not added: nothing consumes it before de-tiling's first tier,
  and the contract exposes nothing before the system behind it exists.
  T0153.1 implements the decision as written there.
- **A local measurement of the `discard`/early-Z tax** — cited from current
  external measurement instead (156.6); a meaningful local one needs an
  occlusion-heavy scene and belongs with T0045's queues if it is ever worth
  building.
- **Normal-map response under triplanar** — unchanged from 141.8's recorded
  decision: the relief shows in the displaced colour-like maps, not in the
  lighting response, because the normal map is still ignored in triplanar
  mode. The marched UVs are exactly the coordinates per-plane reorientation
  will want when something needs it.
- **Per-axis or authorable knobs** — one `heightScale` serves all three
  projections (documented as projection units on the field), and the 1%
  early-out floor is a shader constant. Neither has a consumer asking for
  more.
- **Not separately verified**: the lit path with parallax-under-triplanar has
  no pixel test of its own — the measurements here are unlit *by design*, so
  every differing pixel is a displaced texel. The lit path consumes the same
  displaced samples through the same getters, and the standard material's
  lit correctness is 141.10/141.11's territory; a lit-parallax-specific
  assertion would smuggle the lighting response into a displacement claim.

### Why this was not simply part of 141.8

141.8's scope was triplanar sampling, and it landed with the interaction
documented rather than silently broken — `Material.hpp`'s field comment and the
ticket both say parallax is inert under it. That was the right call at the time:
the alternative was widening a subtask mid-flight. Recording it here so the
"inert" note is read as *scoped out*, not as *investigated and rejected*.
