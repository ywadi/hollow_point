# T0148 — The post-process stack: engine effects and game effects at one seam

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 492 |
| **Created** | 2026-08-06 |
| **Blocked by** | [0096-hdr-pipeline-and-tonemapping.md](0096-hdr-pipeline-and-tonemapping.md) — the HDR target, the tonemap pass and the 10.10 seam must exist before a chain can hang off them |
| **Refs** | **T0161** ([../completed/0161-game-resource-model.md](../completed/0161-game-resource-model.md)) / **D35** — a post-process effect's own LUTs and masks come from `buildModuleSignatureDesc` (`engine/src/ModuleResourceSignature.hpp`): the pass signature's names at index 0, the module's signature beside it, sampler state from the engine palette — the mechanism is built and measured at 34 ns a draw, so this ticket only feeds it; **D32** ([../../documentation/02-decision-log.md](../../documentation/02-decision-log.md)); `08-frame-anatomy.md` step **10.10** — "one seam, wanted by three tickets" is this ticket's ground; D25 — the composite seam rules (UI native, upscale once); [0094-gameplay-extensible-rendering.md](0094-gameplay-extensible-rendering.md) — game layers are the transport for game post passes; [0149-style-bundles.md](0149-style-bundles.md) — a style names a post preset, so it consumes this; [../completed/0111-anti-aliasing-and-render-scale.md](../completed/0111-anti-aliasing-and-render-scale.md) — TAA's prerequisites gate that one effect; T0078 — quality settings (design-gaps item 4: this becomes its fifth consumer) |

## Why

**No post-process stack exists, and the pieces are all waiting on one.**
Verified 2026-08-06: DiligentFX ships `Bloom`, `ScreenSpaceAmbientOcclusion`,
`ScreenSpaceReflection`, `DepthOfField` and `TemporalAntiAliasing` as
components (T0096 confirmed the list, and D6 notes their ImGui tuning panels);
`zig build` compiles them on every platform; **zero tickets integrate any of
them**. T0096 deliberately stops at the tonemap — its 96.7 is "leave the hook
where Bloom/TAA slot in later... do not integrate them yet". This ticket is
where they slot in.

**Decided here: this is a separate ticket, not a fold into T0096 — and the
reason is recorded** because the alternative was considered. T0096 is
policy-heavy (linear workflow, sRGB rules, exposure ownership, the seam's
existence) and already sized; the chain is capability work that *follows* the
policy, wants T0096 finished first, and grows independently (each effect is
its own integration with its own quality knobs). Folding them would make one
oversized ticket where the policy half blocks on the plumbing half's review.
The seam stays T0096's; the chain that hangs on it is this ticket's.

**The game-facing half is what makes this more than wiring.** Godot added
`CompositorEffect` in 4.3 — callbacks at five fixed points (pre-opaque,
post-opaque, post-sky, pre-transparent, post-transparent), compute-only,
render-thread, no light-loop access. HollowPoint already has a stronger
primitive: `IRenderLayer` from a gameplay module (T0027/T0094, D22) can own
draws at an ordered point. What is missing is the *post-effect shape of it* —
a game effect that is "a Slang fragment shader over the frame, with declared
inputs and parameters", without the game writing pipeline code. That is rung 5
of D30's ladder made cheap, and it is the noir grain / underwater warp /
security-camera static that T0149's custom styles will name.

## Done when

- [ ] An ordered post chain runs between the world layers and the UI layers at
      10.10, and **each effect declares which side of the tonemap it sits on**
      — T0096's rule, now enforced by the chain's own structure
- [ ] **Bloom works end to end** on the HDR target, behind a quality setting —
      the proof the chain reads what T0096 built
- [ ] SSAO, SSR, DoF and TAA are each **dispositioned** — integrated, or
      deferred with the blocker named (TAA waits on T0111's prerequisites;
      SSR/SSAO need depth/normal inputs whose forward-renderer availability is
      T0147's 147.6 question) — none left as silent maybes
- [ ] A **game-authored post effect** — a Slang fragment shader with declared
      parameters, authored like a material — inserts at a stated point in the
      chain, orders deterministically against engine effects, and survives
      hot reload (T0094.7's rules)
- [ ] The chain composes with D25's rules: upscale happens once, UI stays
      native and post-tonemap
- [ ] Cost per effect is visible (profiling zones per effect, T0030's shape)

## Subtasks

- [ ] 148.1 The chain structure at 10.10: ordered entries, per-effect
      enable/quality, ping-pong targets via T0046's `declarePingPong`
- [ ] 148.2 Bloom, first and alone — it has the fewest inputs (the HDR colour
      target) and proves the seam
- [ ] 148.3 Disposition SSAO / SSR / DoF / TAA, one decision each, with the
      named blockers
- [ ] 148.4 The game post-effect shape: a Slang shader + parameters through
      the same reflection path materials use (T0142.9, now **T0032.8** —
      the mechanism is still undecided there), inserted via the
      RenderStack transport T0094 owns
- [ ] 148.5 Quality settings: this is design-gaps item 4's fifth consumer —
      wire to T0078's (still unbuilt) quality section rather than inventing a
      sixth shape
- [ ] 148.6 Per-effect profiling zones

## Notes / findings

### 2026-08-06 — this matters more than "noir needs it", and the Godot/Unreal comparison is why

This ticket was justified largely by dark noir needing grading, vignette and
grain. That undersells it. **Post-processing is half of *every* style**, not one
style's requirement — and it is the piece both comparison engines lean on
hardest:

- **Unreal**: Post Process Volumes are blendable at runtime and are where a
  look actually lives. Unreal has no built-in toon shading model, so a
  post-process material is the *standard* workaround.
- **Godot**: `WorldEnvironment` carries tonemap, glow, SSAO, fog and colour
  adjustments, and swapping one is cheap because it is parameters rather than
  pipelines.

The shared lesson: both engines split a look into a **cheap layer** (parameters
and post, switchable instantly) and an **expensive layer** (shader structure,
needing precompiled pipelines). This stack is the cheap layer, which makes it
the part of a style that can actually change at runtime without a stall — and
therefore the part that carries most of the owner's "changeable dynamically"
requirement. See T0149 and T0151.

### Why the game shape is a fragment shader and not a compute hook

Godot's `CompositorEffect` is compute-only, which forces every simple colour
grade through a dispatch and a storage image. The common case — read a
texture, write a colour — is a fullscreen fragment pass, cheaper to author and
to run on this engine's single (Vulkan) backend, and it reuses the whole
material toolchain: Slang, the module system, reflection-driven parameters,
hot reload. Compute post effects become possible when T0150 lands, as an
addition, not the entry point.

### Godot reference (4.7.1, surveyed 2026-08-06)

`CompositorEffect` (4.3+): five fixed callback stages; flags to request
resolved colour/depth, motion vectors, normal-roughness (Forward+ only);
render-thread, `RenderingDevice`/GLSL, no access to the light loop or BRDF;
Mobile/Forward+ only. The comparison to keep honest: Godot's five insertion
points include **pre-opaque and mid-frame** hooks; this chain as specified is
post-world only. Mid-frame insertion for games exists here through
`IRenderLayer` ordering (T0027) — if a game asks for a pre-opaque *effect*
specifically, that is T0094's transport, and this ticket should not grow it.
