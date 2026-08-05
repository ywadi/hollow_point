# T0137 — The engine cannot present its own frame on its default backend

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 388 |
| **Created** | 2026-08-05 |
| **Blocked by** | Nothing |
| **Refs** | [../completed/0028-scene-draw-submission.md](../completed/0028-scene-draw-submission.md), [../completed/0025-render-layer.md](../completed/0025-render-layer.md), [../completed/0046-frame-render-targets.md](../completed/0046-frame-render-targets.md), T0033 (deletes this path), T0096 (inherits the fullscreen pass), T0120, [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D22, D24 |

## Why

**`hp_editor` with no arguments shows a clear colour and no scene.** The default
backend is Vulkan (`Render.cpp`, Vulkan first with an OpenGL fallback), and the
dev present path is a `CopyTexture`, which requires an exact format match:

```
[warn ] render: present source is 1280x720 fmt 29 and the back buffer is 1280x720 fmt 91;
        skipping the dev blit rather than stretching it
[info ] editor: demo scene ready: a lit quad. Run with --backend=opengl to see it.
```

`fmt 29` is the scene target's `RGBA8_UNORM_SRGB`; `fmt 91` is the Vulkan
surface's `BGRA8_UNORM_SRGB`. The copy is skipped rather than performed, which is
the right call — copying regardless would put a red/blue-swapped image on screen,
and that reads as a shader bug.

**The renderer is not at fault and this is worth stating**, because the symptom
looks like one: the gpu bucket's pixel tests (`scene_draws_mesh`, `lit_surface`)
pass on **both** backends, because they read pixels back from the render target
rather than from the window. Everything up to the last step is correct. Only the
step that puts the frame on screen fails, and only on Vulkan.

`--backend=opengl` is a workaround, not a fix. The engine's default path must
work.

## Done when

- [x] `hp_editor` with **no arguments** shows the scene on Vulkan, on Linux
- [x] The same path works on OpenGL — one code path, not a per-backend branch —
      and the two agree to **0 differing pixels**
- [x] Source and back buffer differing in **format** is handled, not skipped
- [x] Colour is correct, not merely present: no red/blue swap, no double gamma
- [x] A gpu test covers it, and fails if the blit regresses —
      `tests/gpu/present_blit_test.cpp`, both backends
- [x] The fullscreen-pass primitive is written so T0096 can build tonemapping on
      it rather than writing a second one — `RenderLayer::blitTexture`, with
      pipelines cached per destination format so an HDR target needs no new code

## Subtasks

- [x] 137.1 A fullscreen-triangle blit: vertex shader from `SV_VertexID`, pixel
      shader sampling the source
- [x] 137.2 PSO and SRB, created once and reused; no per-frame allocation —
      cached per destination format, since a PSO bakes in its RTV format
- [x] 137.3 Replace `CopyTexture` with it as **the** path, both backends
- [~] 137.4 gpu test — covers the blit **into an offscreen target** on both
      backends, not into the swap chain's back buffer. Reading the back buffer
      after `Present` returns a different buffer, and `Diligent::ScreenCapture`
      is asynchronous; the draw, the pipeline and the sampling are identical
      either way, so what is uncovered is the swap-chain binding alone
- [x] 137.5 Confirm sRGB round-trips rather than double-converting — the
      readback matches the source to three decimal places, which it could not
      if a gamma conversion were applied twice

## Notes / findings

### Verified 2026-08-05 — square, centred, and identical on both backends

```
pixel diff, Vulkan vs OpenGL:  0          (compare -metric AE)
quad bounding box:             468x468 +406+126
centre:                        (640, 360) = exact centre of 1280x720
```

468x468 is the arithmetic answer for a 3x3 quad at z=4 under a 60-degree
vertical FOV, so the projection is right as well as the blit. **Zero differing
pixels between backends** is the result worth keeping: it is the evidence that
one code path really is one code path.

### Two traps, both of which "worked" on Vulkan and lied

Written down because each cost a build and neither announced itself.

**1. The stage-output variable must be named `VSOut` in both shaders.**
Diligent's HLSL-to-GLSL converter names each GLSL varying after the *parameter
variable*, so a VS writing `out BlitVSOutput o` emits `_o_uv` while a PS reading
`in BlitVSOutput i` expects `_i_uv`. OpenGL then fails to link:

```
error: "_i_uv" not declared as an output from the previous stage
```

**Vulkan links it anyway**, because SPIR-V matches varyings by location rather
than by name. So the first version ran perfectly on the default backend and
could not create a pipeline at all on the other. Every DiligentFX post-process
shader names it `VSOut`, and their struct lives in a shared `.fxh` so the two
declarations cannot drift -- ours are duplicated and must be kept textually
identical.

**2. `DefaultVariableType = MUTABLE` silently unbinds the constant buffer.**
Setting it wholesale made `BlitConstants` mutable too, so
`GetStaticVariableByName` returned null. The original code wrapped that in
`if (auto* v = ...)` and skipped it **silently**, leaving `g_Params` at zero --
which makes `v` a constant 0.5, so every output pixel sampled the source's
**middle row** and the frame rendered as vertical bars of whatever lay along
y=360.

That is the tell worth remembering: it does not look like an unbound buffer, it
looks like a stretched image. Diagnosed from the picture rather than the code --
background/quad/background smeared to full height is exactly "one row, repeated".
The fix names `g_Source` as the only MUTABLE variable and leaves the default
STATIC, and the null check is now a hard failure with a message, because a
silent skip is what turned a one-line mistake into two rebuilds.

### Why sampling rather than matching the formats

The obvious cheaper fix is to create the scene target in the swap chain's format
so the copy succeeds. **T0096 invalidates that immediately**: the world will
render into a linear HDR target (`RGBA16F`), which can never match an 8-bit
display surface, so a copy stops being possible the moment the HDR chain lands.
A sampled fullscreen pass is what tonemapping needs anyway — D24 already decided
tonemapping is a pass over an HDR target rather than the in-shader
`PSO_FLAG_ENABLE_TONE_MAPPING`. Building the copy-shaped fix now means building
this one twice.

### sRGB is handled by the hardware, and must not be handled again

Sampling an `_SRGB` texture returns **linear**; writing to an `_SRGB` render
target view converts linear back to sRGB. So a plain sample-and-write round-trips
exactly, and any manual `pow(2.2)` in the shader would be a second conversion —
the classic washed-out or crushed image. There is no gamma maths in this shader
on purpose.

### One path, not a fast path plus a fallback

Keeping `CopyTexture` when the formats happen to match would mean OpenGL never
exercises the shader, so the shader could rot untested while the backend that
needs it is the one nobody runs by default. That is precisely the shape of the
bug T0135 just closed — a path that looks covered and is not. One path, both
backends, both exercised on every run.

### This is the engine's first hand-written shader

Everything shaded so far goes through DiligentFX's `PBR_Renderer`, which brings
its own shader library (D24). `grep` for `.hlsl` in `engine/` returned nothing
before this ticket. Kept file-local inside `Render.cpp` behind `Impl`, matching
how `SceneRenderer` keeps Diligent out of public headers — **T0096 should promote
it** when it needs a second fullscreen pass, rather than a public API being
guessed at here for one caller.

### Relationship to T0033

T0033 deletes the dev present path entirely, replacing it with an ImGui image in
a viewport panel. **That is still the plan and this does not change it.** What
this ticket fixes is that the engine cannot currently show its own output without
an editor, which matters because the editor is a Phase 6 consumer and the engine
is not supposed to depend on one existing to be visible. The fullscreen-pass
primitive outlives the present path that motivated it.

### The gpu test, and a finding it turned up (2026-08-05)

`tests/gpu/present_blit_test.cpp`. Renders an **offset** quad through
`SceneView` — offset because a vertically centred image survives a flip
unchanged and so cannot detect one — reads it back, blits it, and reads that
back.

```
Vulkan   source top 207.064 / bottom 132.667   presented 207.064 / 132.667
OpenGL   source top 132.667 / bottom 207.064   presented 132.667 / 207.064
[doctest] test cases: 16 | 16 passed | 0 failed
[doctest] assertions: 710 | 710 passed | 0 failed
```

**Readback row order is backend-dependent, and this test is the first thing to
notice.** The same scene comes back top-bright on Vulkan and bottom-bright on
OpenGL — the two rows above are exact mirrors — while the *presented window* is
pixel-identical on both. So the difference is in the readback convention, not in
the render or the blit. The existing gpu suites never saw it because they compare
**left and right** halves, which a vertical flip does not disturb.

This test therefore asserts that each band maps to *itself* rather than that a
particular band is bright, which is orientation-agnostic and still fails on a
flip. Whether `FrameTargets::readback` should normalise row order is a real
question and **not** this ticket's — it would change what every existing pixel
test sees. Recorded here so it is a known property rather than a surprise.

### Written to the engine-only test boundary

The first draft built its source with `Diligent::ITexture` directly and would not
compile: no test in this repository includes a Diligent header, and Diligent is
linked PRIVATE (D21/D22). Rewritten to use `SceneView` and `FrameTargets`, which
supply both the image and the readback, so the test stays on `hp/` headers. Worth
knowing before writing the next gpu test that wants a specific input image.

### Verified again after the format-keyed refactor

```
final_vulkan:  468x468 +406+126
final_opengl:  468x468 +406+126
cross-backend pixel difference: 0
zig build test -Dtest=all  ->  238 fast + 89 integration, both targets, 0 failed
```
