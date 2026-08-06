# T0156 — Parallax under triplanar, and the silhouette question

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 461 |
| **Created** | 2026-08-06 |
| **Refs** | [0155-terrain-rendering.md](../open/0155-terrain-rendering.md) — **the driving consumer**, and the reason this is High rather than Medium; T0141.7 (parallax occlusion) and T0141.8 (triplanar) — this is their intersection and both landed 2026-08-06; T0141.9 (tessellation) — **156.6 may change its trigger's urgency**; it is **[T0146.7](../open/0146-vertex-stage-hook.md)** since T0141 closed on 2026-08-06, and the trigger is now *"when a silhouette must change at a density the mesh does not carry"*; [0153-surface-detiling.md](../open/0153-surface-detiling.md) — same family, a multi-tap surface-stage technique, and 153.1's interface question is shared; [0151-shader-variants-and-compile-cost.md](../open/0151-shader-variants-and-compile-cost.md) — another permutation axis; **D26**, **D27**, **D30** rung 2 ([../../documentation/02-decision-log.md](../../documentation/02-decision-log.md)) |

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
- [ ] **What is not delivered is written down**, 156.6 included

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
- [ ] 156.5 **The interface question**, shared with T0153.1: this is another
      multi-tap surface technique, and `surfaceCoordinates` (141.7) expresses a
      *coordinate transform*, not a multi-tap blend. Decide with 153.1 rather
      than separately, and reference both ways — two tickets inventing two seams
      for the same shape is the outcome to avoid
- [ ] 156.6 **Evaluate silhouette POM** — see below. **Evaluation, not
      implementation**; the output is a recommendation with numbers and a
      decision recorded either way
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

### Why this was not simply part of 141.8

141.8's scope was triplanar sampling, and it landed with the interaction
documented rather than silently broken — `Material.hpp`'s field comment and the
ticket both say parallax is inert under it. That was the right call at the time:
the alternative was widening a subtask mid-flight. Recording it here so the
"inert" note is read as *scoped out*, not as *investigated and rejected*.
