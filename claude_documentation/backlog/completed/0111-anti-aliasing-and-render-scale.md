# T0111 — Anti-aliasing and render scale: decide before the formats freeze

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 385 |
| **Created** | 2026-08-03 |
| **Blocks** | T0046 — **discharged as "no change" (D25)**; the window had already closed when the decision was made, and it cost nothing because MSAA is out |
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

- [x] The AA decision is made and recorded in the decision log — **D25**: MSAA
      out (it antialiases coverage, not shading), TAA confirmed as the target with
      its three prerequisites named, FXAA/SMAA-class rejected because **none
      ships** and it would have to be written
- [x] T0046's declared formats reflect the decision — **no change is the
      decision**: no sample counts, since MSAA is out. History buffers need no new
      format; `declarePingPong` already covers them
- [x] Render scale is decided — **accepted**, with the sizing rule recorded.
      `FrameTargetDesc::scale` already implements the mechanism, and the world
      already renders offscreen rather than at swap-chain size
- [x] `SuperResolution` is dispositioned — **and the ticket's premise was wrong
      twice**: it is in DiligentCore not DiligentFX, and it is a unified
      abstraction over four backends rather than one upscaler. Adopted **as the
      abstraction**; temporal backends need exactly TAA's prerequisites
- [x] What is deliberately *not* decided is listed — see 111.5 below

## Subtasks

- [x] 111.1 Decide MSAA in or out — **out**, with the costs written down
- [x] 111.2 Confirm the TAA-shaped hook end to end — motion vectors, jitter and
      history buffers named against their owning tickets; **skinned meshes and
      GPU particles flagged as the two hard cases**
- [x] 111.3 Evaluate DiligentFX's SuperResolution component — **it is not
      DiligentFX's and it is not a component**; corrected and dispositioned
- [x] 111.4 Render scale: decided, sizing rule recorded — **and it surfaced a
      collision with T0027.5's single-target compositing**, which is the finding
      this subtask existed to produce
- [x] 111.5 Record the whole decision set in the decision log and cross-check
      T0046/T0096/T0101 — **D25**, with obligations written onto T0027, T0033,
      T0046, T0081, T0096, T0101 and T0106

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

### Decided 2026-08-05 — TAA is the target, MSAA is out, render scale is in

Recorded as **D25**. Measured in the tree rather than recalled; two of the
measurements reverse what this ticket assumed.

#### 111.1 — MSAA is out

**Not because it is bad.** MSAA is the best available answer for *geometric
edges*: exact, deterministic, no ghosting, no blur. And the usual argument
against it does not apply here — its reputation comes from deferred renderers,
and `PBR_Renderer` is **forward**, which is where MSAA is cheapest and most
natural. It is also the only option available today with no prerequisites.

It is out because of what it does not do, and what it costs *this* engine:

- **It antialiases coverage, not shading.** The rasteriser evaluates coverage at
  N samples but runs the pixel shader **once** per pixel, so specular shimmer and
  normal-map crawl — the dominant aliasing in a PBR renderer — are untouched.
  Alpha-tested geometry needs alpha-to-coverage, a separate mechanism.
- **Every pass that reads scene depth becomes per-sample, or needs a resolve.**
  T0106.5 soft particles explicitly require depth readable while drawing
  transparents; SSAO and SSR later. This is the concrete cost and it lands on
  work already planned.
- **DiligentFX's post-process stack operates on single-sample textures**, so MSAA
  forces a resolve before them — and resolving *before* tonemapping in HDR is a
  known trap: bright samples dominate the average and produce fireflies.
- **Memory**, once T0096 brings `RGBA16F`: 4x at 1440p is roughly 118 MB colour
  plus 59 MB depth.

**So `TargetFormat` gains no sample count and `FrameTargets::formatFor` stays as
it is.** Reversing this means touching that function and the pipeline
descriptions in `SceneRenderer` — bounded, and the price of not carrying a sample
count through every declaration for a feature that does not solve the problem.

#### 111.2 — the TAA hook, confirmed end to end

**TAA is the target.** MSAA cannot touch shading aliasing, and **no
FXAA/SMAA/CMAA ships in Diligent at all**, so the cheap post-process tier would
have to be written or vendored — rejected for now on that basis rather than on
quality.

DiligentFX's `TemporalAntiAliasing` is **competent middle-tier and says so about
itself** — its README opens *"We needed to add temporal antialiasing to our
project that could run in WebGL"*. But it carries the pieces that decide whether
a TAA is good or bad: **variance clipping** (optionally Gaussian-weighted),
**Catmull-Rom bicubic history sampling**, and **disocclusion rejection**, after
Emilio López's reference implementation. Naive TAA is bad; this is not naive.

Two properties worth knowing before planning around it:

- **It is not an upscaler.** DLSS and UE's TSR antialias *and* upscale; this is
  fixed-resolution. Upscaling is a separate pass here.
- `Skip rejection` disables the disocclusion check *"for static images to achieve
  honest supersampling"* — free high-quality screenshots and thumbnails
  (**T0120.2**).

**The three prerequisites, named and not built:**

| Prerequisite | State today | Owner |
|---|---|---|
| **Motion vectors** | `PSO_FLAG_COMPUTE_MOTION_VECTORS` is masked off in `kFeatureMask`; **`PBRFrameAttribs::PrevCamera` is already written every frame** by `SceneRenderer`, so the camera half costs nothing | T0101.5 |
| **Sub-pixel jitter** | Not applied. `TemporalAntiAliasing::GetJitterOffset()` supplies it and `buildView` is where it goes — `CameraSystem.hpp` **already documents that seam** by name | T0081 |
| **History buffers** | `FrameTargets::declarePingPong` already exists and is exactly the right shape | T0046 |

**These same three inputs serve every temporal upscaler as well** — see 111.3.
Building them once opens TAA, DLSS, DirectSR, FSR2 and MetalFX Temporal together,
which is the single most leveraged piece of work in this area.

**The risk that decides whether this looks good or bad is motion vectors, and two
cases are not the easy one:**

- **Skinned meshes need *previous joint matrices*, not just previous transforms.**
  Get it wrong and animated characters ghost — the most visible object in most
  games.
- **Particles.** **D15** makes them GPU-driven and cosmetic, so they have no
  CPU-side previous position. Unless the simulation writes motion vectors itself
  they will smear. A real cost against D15, recorded on T0106.

**The quality judgement is deliberately deferred.** Everything renders black
until T0079, and antialiasing cannot be evaluated on a black screen. What is
decided here is the *direction and the hooks*.

#### 111.3 — upscaling: adopt the abstraction, not a backend

The ticket assumed `SuperResolution` was a single upscaler shipping as a
DiligentFX post-process. **Both halves are wrong.**

It lives in **DiligentCore** (`DiligentCore/Graphics/SuperResolution/`), and it is
not one upscaler — it is a **unified abstraction over four**, behind
`ISuperResolutionFactory` / `ISuperResolution`, which **discovers the available
implementations at factory-creation time based on the render device type**:

| Backend | Type | Graphics API |
|---|---|---|
| NVIDIA DLSS | Temporal | D3D11, D3D12, Vulkan |
| Microsoft DirectSR | Temporal | D3D12 |
| AMD FSR | Spatial | All |
| Apple MetalFX | Spatial + Temporal | Metal |

**The decision is to adopt `ISuperResolutionFactory` as the engine's upscaling
interface and let backend availability be a build-and-platform matter, not an
engine design constraint.** The engine asks what is available and uses the best
one; it never hardcodes a technique.

That is the correct shape for a reason worth stating plainly: **this is an engine
for several games on several machines, and what one developer's build happens to
compile is not an engine capability decision.** A player with an NVIDIA GPU should
get DLSS; the engine's job is to have asked.

**What that costs: nothing beyond TAA's prerequisites.** Temporal upscalers need
exactly what TAA needs — depth (have it), motion vectors, and jitter — plus
optional exposure, reactive and ignore-history masks. Spatial FSR needs only the
low-resolution colour texture. So the same three prerequisites open every path.

**Current build availability, measured rather than read off the CMake:**

```
DILIGENT_DLSS_SUPPORTED  = FALSE
DILIGENT_DSR_SUPPORTED   = FALSE
DILIGENT_FSR_SUPPORTED   = TRUE
MINGW_BUILD              = TRUE      (both targets)
```

`MINGW_BUILD = TRUE` is why the two temporal backends are off: DLSS is gated on
`PLATFORM_WIN32 AND NOT MINGW_BUILD` plus Windows SDK >= 10.0.26100, and DirectSR
additionally needs D3D12. **This is a property of the current toolchain, not of
the engine**, and it is the same root cause that rules out D3D11/D3D12 — zig
targets Windows through the MinGW ABI and DiligentCore gates those on ATL.

**So it is recorded as a revisit trigger rather than a conclusion.** An MSVC-based
Windows build would unlock DLSS, DirectSR and D3D11/12 together. That is a build-
harness decision with real costs of its own (it ends the single-toolchain,
hermetic cross-compile that the harness is built around), and it belongs to
whoever owns that question — but the price of the current choice is now written
down where it can be weighed, instead of being discovered later as "the engine
does not support DLSS".

`Diligent-SuperResolution` is **built but not linked** by `hp_engine` today,
compiled because `DiligentCore/Graphics/CMakeLists.txt` adds the subdirectory. So
keeping the door open costs nothing at all.

**FSR spatial is the guaranteed floor** — all APIs, all platforms, colour texture
only, and **no antialiasing whatsoever**. It is a resolution lever, not a quality
one, which is precisely why TAA is decided separately above rather than being
assumed to come with the upscaler.

#### 111.4 — render scale, and the sizing rule

**Accepted.** The world renders at a scale factor of the output size and is
composited up; 1.0 is the default and the identity case.

**Most of the machinery already exists**, which the ticket did not know:

- **`FrameTargetDesc::scale` is already a field**, applied in `FrameTargets`'s
  create and resize (`width * desc.scale`, clamped to at least one pixel). It was
  added for half-resolution bloom buffers and is exactly what render scale needs.
- The scene already renders **offscreen** (T0028.4); `SceneView` and
  `FrameTargets` own their size rather than the swap chain's.
- `SceneRenderLayer` takes its viewport from the **resolved camera**, not the pass
  size (T0027.3).

So this ticket's fear — that the world layer would assume swap-chain size — did
not come true.

**The sizing rule:**

> World and post-process targets size from **output x renderScale**. UI, HUD and
> editor panels are **always native**. The upscale happens once, between the two.

**And that rule collides with T0027.5, which is the finding this subtask exists to
surface.** `RenderStack` composites **every layer into one colour target**
(single-target compositing, decided on 27.5) — verified, not inferred:
`RenderStack::render` takes a single `ITextureView* colour` for all layers. Under
render scale that target has one size, so a HUD layer drawn into it renders at
render scale too — **upscaled text and UI, exactly what the rule forbids.**

Three ways out, and this ticket does not pick one because the cost is only payable
once there is UI to look at:

1. **Two-phase stack** — world layers into the scaled target, an upscale/composite
   step, then UI layers into a native-size target. Cleanest, and it is also
   exactly where a tonemap pass belongs (T0096), so the two probably want the same
   seam.
2. **UI layers own their target and blit** — already the documented escape hatch
   in 27.5.
3. **Accept UI at render scale** — cheapest, and wrong for text.

**Whichever is chosen, it is the same seam as tonemapping and as the upscale**,
and T0096 should not invent a second one. Recorded on T0027, T0033 and T0096.

**Scale greater than 1 is SSAA, and it comes free with this rule.** Asked
explicitly during the decision and worth answering here, because a future reader
will ask it too: supersampling is not a separate feature to add later — it *is*
render scale above 1.0. `FrameTargets` already computes `width * desc.scale`, so
2.0 renders at double resolution and the same composite step downsamples it.
Ordered-grid SSAA, one float.

Nothing in the MSAA decision above forecloses it, and this is the distinction
that matters: MSAA needs sample counts threaded through every format declaration
and every pipeline state, whereas SSAA needs only a **bigger target**. Removing
sample counts costs SSAA nothing. It also composes with TAA rather than competing
— supersample spatially, accumulate temporally — and it antialiases everything,
because it genuinely shades more samples.

Two notes for whoever implements the composite: the **downsample filter is a real
choice** (box is the common default, tent or Lanczos better; naive bilinear from
2x discards part of what was paid for), and the cost is quadratic — 2x scale is
4x the pixels and roughly 4x the shading, which is why it is a quality tier and
not a default. Which tier maps to which scale is **T0078's**.

The HUD collision above **inverts** in this direction and stops being a problem:
at scale below 1 the UI is upscaled and blurry, but at scale above 1 it is merely
supersampled along with everything else — wasteful, not wrong.

One interaction to carry forward: **T0093's per-pixel visibility uses screen-space
dither patterns** (Bayer/blue-noise). Dithering at render scale then upscaling
changes the pattern's appearance, and TAA resolving a dither pattern is a
different thing again — that combination needs checking when T0093 lands.

#### 111.5 — what is deliberately *not* decided

- **Whether TAA actually looks good here**, and its stability/sharpening
  parameters. Unanswerable against a black screen; waits for T0079.
- **Which quality tier maps to which AA and scale combination** — T0078's
  settings work.
- **Actual TAA or upscaler integration.** This ticket names prerequisites and
  builds none of them.
- **Which of the three UI-at-native options is taken.** T0096 or T0033, whichever
  reaches the seam first.
- **Whether to add an MSVC Windows build to unlock DLSS/DirectSR/D3D12.** Named
  as a real trade-off with a real price; not this ticket's to settle.

### Original framing, kept for the record

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
