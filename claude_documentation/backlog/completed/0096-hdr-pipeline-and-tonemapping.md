# T0096 — HDR pipeline, tonemapping and the linear-workflow policy

| | |
|---|---|
| **Status** | ❌ SUPERSEDED |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 460 |
| **Created** | 2026-08-03 |
| **Superseded** | 2026-08-08 by **[T0148](../open/0148-post-process-stack.md)** — *"The frame after the world"*. **Nothing was dropped**: every Done-when and every subtask has a destination in the table below, and this ticket's entire `## Notes / findings` is preserved **verbatim** in T0148 under *Absorbed from T0096* |

## Why this was absorbed rather than left open

**This ticket and T0148 were two halves of one pass sequence, and the argument
that separated them expired.**

They were split on 2026-08-06, deliberately and with a recorded reason: *"T0096
is policy-heavy and already sized; the chain is capability work that follows the
policy… folding them would make one oversized ticket where the policy half
blocks on the plumbing half's review."*

**That reasoning assumed the chain was capability work.** After **D40** it is
not — bloom, DoF, SSAO, SSR and TAA are vendored, complete and merely
unconstructed, so the chain is plumbing and settings. The asymmetry the split
was protecting against no longer exists, and what remained was a hard blocking
edge (`T0148 blocked by T0096`) between two halves of the same pass sequence.

**And D24 had already put them in the same place.** Tonemapping belongs in a
pass over an HDR target rather than `PSO_FLAG_ENABLE_TONE_MAPPING`, because the
in-shader path tonemaps **per draw, before blending** — which breaks
transparency and leaves bloom no HDR image to read. That pass *is* the first
element of T0148's chain. Kept as separate tickets they fought; merged they are
one ordered list.

## Where everything went

| From here | Now |
|---|---|
| HDR target format (96.1) | **T0148.1** |
| The tonemap pass (96.2) | **T0148.6** |
| **Operator selection** (96.2b) — added by T0171 | **T0148.3**, and it is now explicitly the decision **T0086 and T0155 inherit** for `SHADOW_MODE` and `TEXTURING_MODE` |
| sRGB policy, plumbed through texture load and material binding (96.3) | **T0148.2** |
| Exposure control, serialized (96.4) | **T0148.4**, with T0130's precedence carried in full |
| Ordering against debug draw and UI (96.5) | **T0148.6**'s acceptance |
| Both-backend verification (96.6) | **dropped as obsolete** — written when the engine had two backends. **D29/T0144 removed OpenGL**; there is one backend and no sRGB-framebuffer or Y-flip difference to reconcile. This is the one line with no destination, and this sentence is why |
| The Bloom/TAA hook (96.7) | **absorbed by construction** — the hook existed to keep the chain out of this ticket, and there is now one ticket |
| The D24 correction: *there is no DiligentFX ToneMapping component* | preserved verbatim in T0148 |
| D25's composite-seam inheritance (tonemap + upscale + native UI are one pass) | preserved verbatim in T0148 |
| T0130's exposure-ownership precedence | preserved verbatim in T0148, and restated in its Done-when |
| T0111's cross-ticket obligation on the TAA hook | preserved verbatim in T0148 |

## What this ticket got right, kept because it is the reason the merged ticket is ordered as it is

**Policy first.** The chain and the presets both hang off the linear workflow,
and colour-space bugs are the silent kind: lighting tuned against a clamped LDR
buffer, an sRGB albedo sampled linear, a UI tonemapped with the world. Each
looks plausible on its own, and fixing it later means re-tuning every light and
every material in every scene. T0148's subtask list keeps that order — 148.1
through 148.4 are this ticket's policy, and nothing in the chain starts before
them.
