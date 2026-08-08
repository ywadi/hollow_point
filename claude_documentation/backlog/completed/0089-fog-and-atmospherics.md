# T0089 — Fog and atmospherics

| | |
|---|---|
| **Status** | ❌ SUPERSEDED |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 11 — World & environment |
| **Order** | 840 |
| **Created** | 2026-08-03 |
| **Superseded** | 2026-08-08 by **[T0088](../open/0088-sky-atmosphere-time-of-day.md)** — *"The atmosphere: sky, fog, light shafts and time of day"*. **Nothing was dropped**: the mapping is below and this ticket's `## Notes / findings` is preserved **verbatim** in T0088 under *Absorbed from T0089* |

## Why this was absorbed rather than left open

**Two of this ticket's seven subtasks existed only to keep it synchronised with
two other tickets**, which is the signature of one subject split three ways
rather than three adjacent subjects:

- **89.4** — *"Derive fog colour from the sky/sun direction (**T0088**)"*.
- **89.7** — *"Keep parameters shared with volumetric fog (**T0091**), not
  duplicated"*.

A ticket whose own plan includes "stay consistent with those two tickets" is a
ticket that should be one of them. Merged, 89.4 is an internal read and 89.7 is
simply how the parameters are declared once.

**Two further tells**, both found by the 2026-08-08 sweep:

- This ticket and T0088 carried **the same D32 note, word for word**, about
  authoring the fog/sky shader against a game-implementable interface. One
  design constraint, written twice, for one component.
- It was filed in **phase 11, "World & environment"**. Fog is a render-layer
  concern that reads the depth buffer and composites into the frame; the
  misfiling is part of what let it drift away from T0148's chain, which is where
  a frame-wide fog pass actually belongs.

**And the deeper cause, which caught five ticket pairs across the render
layer**: this was split from T0091 by *feature variant* — cheap analytic fog
here, expensive volumetric fog there. That is a sound split while the
implementation is ours to build. Under **D40** it is Diligent's, so the variants
are settings on one component.

## Where everything went

| From here | Now |
|---|---|
| Fog parameters as scene settings (89.1) | **T0088.15**, explicitly as **one** set shared by every tier |
| Distance fog, linear and exponential (89.2) | **T0088.14** |
| Height fog (89.3) | **T0088.14** |
| Fog colour from sky/sun (89.4) | **T0088.16** — now an internal read rather than a cross-ticket obligation |
| Interaction with transparents and the skybox (89.5) | **T0088.17**, with the instruction to decide it *when* T0045's transparent queue is built rather than afterwards |
| Runtime animation for weather transitions (89.6) | **T0088.15**; T0090 remains the consumer |
| Shared parameters with volumetric fog (89.7) | **discharged by the merge** — there is one parameter set and one ticket |
| *"A frame-wide fog is a full-screen pass and belongs with T0148's chain"* (T0147/D37) | **T0088.18**, unchanged, including the rule that the two must not each copy the frame |
| The D32 note about a game-implementable interface | **reinterpreted, not dropped**: under D40 there is no fog shader of ours to override, so a game's own fog is a **pass** on T0094's seam. Recorded on T0088 |

## What this ticket got right and the merged ticket keeps

**Fog does a disproportionate amount of visual work** — depth, scale, hiding the
far clip plane and LOD transitions, and it is the main lever for mood. It is also
what makes weather (T0090) read as weather rather than as particles. And it stays
**deliberately cheap**: distance and height fog cost almost nothing, cover the
common case, and remain the fallback on quality settings where volumetrics are
off. T0088.14–88.18 are near-term for exactly that reason and do not wait behind
the atmosphere.
