# T0156 — Parallax under triplanar, and the silhouette question

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 461 |
| **Created** | 2026-08-06 |
| **Refs** | [0155-terrain-rendering.md](0155-terrain-rendering.md) — **the driving consumer**, and the reason this is High rather than Medium; T0141.7 (parallax occlusion) and T0141.8 (triplanar) — this is their intersection and both landed 2026-08-06; T0141.9 (tessellation) — **156.6 may change its trigger's urgency**; it is **[T0146.7](0146-vertex-stage-hook.md)** since T0141 closed on 2026-08-06, and the trigger is now *"when a silhouette must change at a density the mesh does not carry"*; [0153-surface-detiling.md](0153-surface-detiling.md) — same family, a multi-tap surface-stage technique, and 153.1's interface question is shared; [0151-shader-variants-and-compile-cost.md](0151-shader-variants-and-compile-cost.md) — another permutation axis; **D26**, **D27**, **D30** rung 2 ([../../documentation/02-decision-log.md](../../documentation/02-decision-log.md)) |

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

- [ ] A **triplanar material with a height map shows parallax**, measured — a
      frame-difference number against the same material with height off, in the
      shape 141.7 used (zero-scale must be **bit-identical**; a real scale must
      move a measurable amount)
- [ ] **Cost is measured per weight configuration** — one, two and three
      contributing axes — and the numbers are in the ticket, not asserted
- [ ] **Blend-region seams are characterised**, and either mitigated or accepted
      with the reasoning written down
- [ ] It is a **game-dev-facing option**, not an engine-internal — reachable from
      a material, and overridable from a game shader per D30 rung 2
- [ ] **It works on a terrain-shaped surface**, not only on a cube — the cube is
      the trivial case and proving it there proves little
- [ ] **What is not delivered is written down**, 156.6 included

## Subtasks

- [ ] 156.1 **Per-axis march with the axis-aligned frames.** The enabling fact
      above; rotate the view vector per projection with the same swizzle the UVs
      use, and reuse 141.7's march
- [ ] 156.2 **Early-out on low weights**, and measure the 1 / 2 / 3-axis cost.
      A weight below a small threshold should skip the march entirely rather
      than marching and multiplying by ~0
- [ ] 156.3 **Height sampling through the three-projection path** — the height
      texture is sampled by UV today and needs the same treatment as the other
      maps under triplanar
- [ ] 156.4 **The blend-region seam.** Where two axes mix, each marched
      independently, displacements disagree and can show a seam — and on terrain
      those regions sit exactly where flat becomes steep, which is where the
      camera looks. Characterise it first, then choose: sharpen the weights
      further (narrower band, harder transition), march in a shared world-space
      direction, or accept it. **Do not skip the characterisation** — this is the
      one quality risk that is specific to this combination
- [ ] 156.5 **The interface question**, shared with T0153.1: this is another
      multi-tap surface technique, and `surfaceCoordinates` (141.7) expresses a
      *coordinate transform*, not a multi-tap blend. Decide with 153.1 rather
      than separately, and reference both ways — two tickets inventing two seams
      for the same shape is the outcome to avoid
- [ ] 156.6 **Evaluate silhouette POM** — see below. **Evaluation, not
      implementation**; the output is a recommendation with numbers and a
      decision recorded either way
- [ ] 156.7 **Measure on a terrain-shaped surface**, not the cube

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

### Why this was not simply part of 141.8

141.8's scope was triplanar sampling, and it landed with the interaction
documented rather than silently broken — `Material.hpp`'s field comment and the
ticket both say parallax is inert under it. That was the right call at the time:
the alternative was widening a subtask mid-flight. Recording it here so the
"inert" note is read as *scoped out*, not as *investigated and rejected*.
