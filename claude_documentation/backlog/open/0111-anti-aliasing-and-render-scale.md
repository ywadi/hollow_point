# T0111 — Anti-aliasing and render scale: decide before the formats freeze

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 385 |
| **Created** | 2026-08-03 |
| **Blocks** | T0046 |
| **Refs** | T0033, T0047, T0081, T0096, T0101, T0106, [../../documentation/07-design-gaps.md](../../documentation/07-design-gaps.md) item 2, [../completed/0134-pbr-renderer-adoption.md](../completed/0134-pbr-renderer-adoption.md) — **DiligentFX ships `TemporalAntiAliasing` and `PrevCamera` is already written every frame**; also records that this ticket's `Blocks: T0046` window has already closed |

## Why

`anti-alias`, `MSAA`, `FXAA`, `SMAA` -- zero hits across the backlog at survey
time (2026-08-03). `upscal`, `dynamic resolution`, `render scale`, `FSR` --
zero. TAA exists only as a deferred hook: T0096.7 "leave the hook where
Bloom/TAA slot in later", T0101.5 "the TAA/motion-vector hook T0096 leaves
open". So the *mechanism* for one specific AA technique is half-anticipated,
but the *decision* -- does this engine ship aliased, and if not, how -- appears
nowhere.

Two facts sharpen it:

- **DiligentFX ships TAA as a component** (verified during T0096's survey of
  `DiligentFX/PostProcess/`), and the build already produces and stages
  `SuperResolution_64r.dll` (recorded in
  [../../documentation/05-verification-status.md](../../documentation/05-verification-status.md))
  -- an upscaler compiled on every build and referenced by zero tickets.
- **The choice is structural.** MSAA changes T0046's render-target formats
  (sample counts on colour and depth) and every pass that reads depth --
  T0106's soft particles read scene depth, which becomes a per-sample read
  under MSAA. TAA needs motion vectors (T0101.5), camera jitter (T0081's
  projection), and history buffers in T0046. Either one has an ordering
  relationship with tonemapping that T0096 owns.

Deciding "no MSAA ever, keep the TAA-shaped hook, evaluate the upscaler later"
costs a paragraph now. Retrofitting MSAA after T0046's "formats are declared in
one place" freezes touches every declaration and several passes -- which is why
this ticket blocks T0046 rather than trailing it.

Render scale rides along because it is nearly free to decide with AA and
expensive to assume away: the editor already renders the scene into an
offscreen target at panel size (T0033), so the runtime rendering below window
size and blitting/upscaling up is the *same machinery* -- but only if it is
said now, before the world layer assumes it renders at swap-chain size.

## Done when

- [ ] The AA decision is made and recorded in the decision log: MSAA in or out
      of this engine's future; the TAA-shaped hook confirmed (or dropped) with
      its dependency list named; post-process AA (FXAA/SMAA-class) accepted or
      rejected as the cheap fallback
- [ ] T0046's declared formats reflect the decision -- sample counts if MSAA is
      in, history-buffer slots if TAA-shaped, neither if shipping aliased is
      the recorded choice
- [ ] Render scale is decided: the world renders at a scale factor of the
      output size (1.0 included) and is blitted/upscaled to the swap chain --
      or this is explicitly rejected. If accepted, T0046's world targets size
      from render scale, not from the swap chain, and nothing outside the final
      composite assumes otherwise
- [ ] `SuperResolution_64r.dll` is dispositioned: what it implements, what it
      would need from the frame (probably motion vectors and jitter, i.e. the
      TAA prerequisites), and whether it stays a staged-but-unused artefact
- [ ] What is deliberately *not* decided is listed -- e.g. which quality tier
      maps to which AA/scale combination is T0078's quality section, and actual
      TAA integration remains deferred behind T0096.7's hook

## Subtasks

- [ ] 111.1 Decide MSAA in or out, with the costs written down: per-sample
      depth reads for anything sampling scene depth (T0106 soft particles,
      later SSAO/SSR), resolve steps, and format-declaration churn in T0046
- [ ] 111.2 Confirm the TAA-shaped hook end to end: motion vectors (T0101.5),
      jitter injection at projection (T0081), history buffers (T0046), and
      ordering against tonemap (T0096) -- named, not built
- [ ] 111.3 Evaluate DiligentFX's SuperResolution component: what technique,
      what inputs, what it costs to keep the door open
- [ ] 111.4 Render scale: decide, and record the sizing rule for T0046 (world
      targets = output × scale; UI and editor panels always at native size)
- [ ] 111.5 Record the whole decision set in the decision log and cross-check
      T0046/T0096/T0101 say the same thing

## Notes / findings

### Inherited from T0134 (2026-08-05) — TAA ships, and the formats have already frozen

Two facts this ticket should not re-derive:

- **`DiligentFX/PostProcess/TemporalAntiAliasing/` exists and is referenced by
  nothing.** So does `ScreenSpaceReflection`, which typically wants the same
  motion vectors. `PSO_FLAG_COMPUTE_MOTION_VECTORS` is the PBR-side switch, and
  `PBRFrameAttribs::PrevCamera` **is already written every frame** by
  `SceneRenderer` — the history half of TAA's input is in place and costs nothing
  today.
- **This ticket's "decide before the formats freeze" window has closed**, and
  saying so is more useful than leaving the title implying otherwise. It declares
  `Blocks: T0046`, and T0046 is **done**. There are now three sites assuming
  single-sample targets: `FrameTargets::formatFor`, and two pipeline-state
  descriptions in `SceneRenderer` (the world path and T0027.4's depth-less HUD
  path). `grep -rniE "samplecount|SampleDesc|msaa|multisample"` over `engine/`
  returns **zero hits** — MSAA is not deferred here, it is absent and undeclared.

The retrofit is still small and grows monotonically: T0060, T0079 and T0096 each
add pipeline states, and T0106's soft particles add a depth read, which is where
MSAA stops being a format change and becomes a per-sample-resolve change.

**Render scale is in better shape than the ticket assumes.** It warns against the
world layer assuming it renders at swap-chain size — it does not. `SceneView` and
`FrameTargets` own their own size and the scene renders offscreen (T0028.4), and
`SceneRenderLayer` takes its viewport from the resolved camera rather than the
pass (T0027.3). The machinery render scale needs is already the machinery in use.

- This is a *decision* ticket with hooks, not an implementation ticket. The
  danger it exists to prevent is a silent default -- shipping aliased because
  nobody chose, the mirror image of T0003's
  `VK_PRESENT_MODE_IMMEDIATE_KHR` (see T0110).
- A plausible outcome, stated so it can be argued with rather than assumed: no
  MSAA (its cost lands on exactly the depth-reading passes this engine is
  accumulating), TAA-shaped hook kept, FXAA-class as the cheap tier, upscaler
  evaluated when there is a scene to measure. The ticket's job is to make that
  -- or its refutation -- the recorded answer.
- Render scale interacts with T0093's per-pixel visibility and dither patterns
  (screen-space Bayer/blue-noise): dithering at render scale then upscaling
  changes the pattern's appearance. Worth one line in the decision when taken.
