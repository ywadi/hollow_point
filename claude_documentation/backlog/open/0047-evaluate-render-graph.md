# T0047 — Evaluate whether a render graph is warranted

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Low |
| **Phase** | 9 — Deferred |
| **Created** | 2026-08-02 |

## Why

Diligent provides no render graph / frame graph — no automatic resource aliasing,
barrier insertion, pass reordering or culling of unused passes. It is a real
absence, and the question of whether to build one will keep resurfacing.

This ticket exists to **answer it deliberately, later, with evidence** — not to
build one. Recording the reasoning now prevents it being built speculatively.

## Done when

- [ ] A decision is recorded in the decision log, either way
- [ ] If yes: scoped into its own tickets with a concrete justification
- [ ] If no: the trigger conditions for revisiting are written down

## Subtasks

- [ ] 47.1 Count the actual passes in the frame once the renderer is real
- [ ] 47.2 Measure whether render-target memory is genuinely under pressure
- [ ] 47.3 Assess how often pass ordering changes in practice
- [ ] 47.4 Decide, and record it

## Notes / findings

**The case against building one now, which is the current position:**

- Its biggest benefit — correct barriers — is already handled by Diligent's
  automatic resource state transitions.
- Its second benefit — pass orchestration — is partly provided by DiligentFX,
  which ships Bloom, DepthOfField, SSAO, SSR, TAA and EpipolarLightScattering as
  self-contained passes.
- `RenderStack` (T0027) already gives ordered, composited passes, which covers
  the practical need.
- Its remaining value scales with pass count and platform count. Frostbite and
  Unreal need one because they have dozens of passes across many platforms. This
  is one game on two backends.

A frame graph is a well-known way to spend months building flexibility that is
never exercised. The cost is not just writing it — every pass afterwards is
written against a more abstract API, and debugging gets harder.

**Revisit if any of these become true:**
- more than roughly 15-20 distinct passes in a frame
- render-target memory becomes a real constraint
- passes need to be enabled/reordered dynamically per-scene or per-quality-level
- a third backend or platform with different barrier semantics appears
