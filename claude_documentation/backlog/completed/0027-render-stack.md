# T0027 — RenderStack: composited visual layers

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 400 |
| **Created** | 2026-08-02 |
| **Refs** | [../completed/0113-device-loss.md](../completed/0113-device-loss.md), [../completed/0028-scene-draw-submission.md](../completed/0028-scene-draw-submission.md), [../open/0045-culling-and-render-queues.md](../open/0045-culling-and-render-queues.md), [../open/0079-lighting-system.md](../open/0079-lighting-system.md), [../open/0069-game-ui.md](../open/0069-game-ui.md), [../open/0094-gameplay-extensible-rendering.md](../open/0094-gameplay-extensible-rendering.md) |

## Why

The final image is not one render — it is an ordered set of visual layers
composited in order: 3D world at the bottom, then HUD, then debug overlays, then
UI. Each may use a different camera and projection, and each decides whether it
clears or draws over what is beneath.

This is the same concept as Unity URP's camera stacking and Godot's `CanvasLayer`.
Building it into the renderer from the start is far cheaper than discovering
later that the HUD needs its own projection and depth behaviour.

**Naming:** this stacks *visual output* and is distinct from `LayerStack`
(T0017), which stacks *systems*. The render layer is one system layer that owns
this `RenderStack`.

## Done when

- [x] `IRenderLayer` with a render entry point and explicit ordering — stable sort, so equal orders keep insertion order
- [x] `RenderStack` composites layers in order into one target — verified rendering on a real device, both backends (a *software* device; see the correction below)
- [x] Per-layer clear behaviour and camera/projection — clear and per-layer **depth** verified in pixels; **camera/projection is resolved per layer** from its own view slot (27.3), with T0081 owning the resolve and T0130 the vocabulary
- [x] A world layer and a UI/HUD layer both render, correctly stacked — measured in pixels, both backends; see the evidence block below
- [x] Layers can be enabled/disabled at runtime without reordering — a three-layer stack with one disabled renders exactly two, in order
- [~] **Gameplay code can implement and insert its own layer** (T0094) — the API allows it, D22 measured that the calls link with no Diligent library, and `SceneRenderLayer` is now a worked example of the interface; **no layer has been written in an actual gameplay module**, so this stays proven in principle and not in fact
- [x] Each layer emits its own profiling zone (T0019) — `HP_PROFILE_ZONE_NAMED(layer->name())` in `RenderStack::render`, one per enabled layer

## Subtasks

- [x] 27.1 `IRenderLayer` — order, enabled flag, clear policy, per-layer depth. Camera deliberately excluded; see notes
- [x] 27.2 `RenderStack` owning an ordered list — **non-owning**, see notes
- [x] 27.3 World layer drawing the scene (T0028) — `hp::SceneRenderLayer`, sharing `SceneRenderer` with `SceneView` rather than growing a second resolve
- [x] 27.4 A HUD/UI layer proving 2D-over-3D works, including depth handling — `configureAsHud`; the depth-less pipeline is built and exercised, and the overlay is asserted in pixels
- [x] 27.5 Decide compositing — **single target**, decided and recorded below
- [x] 27.6 Per-layer profiling zones — `HP_PROFILE_ZONE_NAMED(layer->name())`

## Unblocked 2026-08-05 — T0028 closed

T0028 built the thing this stack had nothing to composite: `hp::SceneView`
renders a scene into an offscreen colour and depth pair and publishes it. Two
things it leaves for this ticket:

- **The scene is drawn without going through `RenderStack`.** `SceneView` binds
  its targets and submits directly, because a stack with one layer would have
  been ceremony. Turning that into an `IRenderLayer` is this ticket's job, and
  the seam is already the right shape — `SceneRenderer::render` takes the draw
  list, the resolved view and a context, and binds no targets itself.
- **A format-converting blit may belong here.** T0028's dev present path is a
  plain `CopyTexture` and cannot show a frame on Vulkan, whose surface is BGRA
  against the scene target's RGBA. T0033 makes that moot by sampling the texture
  in a shader; if this stack grows a fullscreen-triangle composite first, it
  solves the same problem and T0033 should use it rather than either being
  written twice. See T0033's notes.

## What 27.3 and 27.4 need, worked out 2026-08-05

Written down rather than re-derived. Both are unblocked; neither is started.

### 27.3 — the world layer

A `hp::SceneRenderLayer : IRenderLayer` in `engine/`, holding a `Scene*`, an
`AssetPool*`, a view slot and a `SceneRenderer`. `onRenderLayer(pass)`:

1. `resolveCamera(scene, viewSlot)` — return early if none, the stack has
   already cleared;
2. `buildView(entity, pass.width, pass.height, clip)`;
3. `SetViewports` from `ResolvedView::viewport*`, **not** from `pass.width/height`;
4. `parseScene` then `SceneRenderer::render`.

**It must not bind targets.** `RenderPassContext` arrives with colour and depth
already bound and cleared, which is why `SceneRenderer::render` was written to
take a context and bind nothing — the seam is already correct.

`hp::SceneView` keeps its direct path; it is what the editor uses today and what
T0033 will consume. This layer is the same submission driven by the stack
instead, so the two must share `SceneRenderer` rather than growing a second copy
of the resolve.

### 27.4 — the HUD layer, with no UI library

**A HUD is a camera on another view slot**, which is what T0028's notes
predicted: "a world layer and a HUD layer each resolve their own slot, which is
how a HUD gets an orthographic camera without the world knowing about it."

So 27.4 is a second `SceneRenderLayer` on **view slot 1** with an orthographic
camera, `useDepth = false` and `clear = LayerClear::None`, ordered above the
world layer. No new shader, no UI dependency.

**Deliberately not a UI library.** This subtask proves *compositing*, and
choosing Dear ImGui or RmlUi here would be this ticket answering
[T0069](../open/0069-game-ui.md)'s question — the same mistake T0134 exists to
prevent. T0069 already surveys the options and leans RmlUi; **D6** records that
ImGui ships in the runtime binary regardless, so "it is already linked" is not an
argument. Leave it there.

`useDepth = false` is the part worth asserting: the header warns that a UI layer
which depth-tests against the world vanishes behind near geometry, and reads as
flickering UI rather than as a depth bug.

### The test

GPU bucket, both backends. Stack a world layer (slot 0, perspective, geometry at
one depth) under a HUD layer (slot 1, orthographic, geometry nearer the camera in
world terms but drawn without depth), render, and **read the pixels back** —
`SceneView::readback` exists for exactly this. Assert the HUD's pixels win where
they overlap.

**Assert pixels, not statistics.** T0028 spent an afternoon on a frame where
every counter said a draw was issued and nothing was on screen; two submitted
layers prove nothing about ordering.

### Built and measured 2026-08-05 — 27.3 and 27.4 close the ticket

`engine/include/hp/SceneRenderLayer.hpp`, `engine/src/SceneRenderLayer.cpp`,
`tests/fast/scene_render_layer_test.cpp`,
`tests/gpu/render_stack_composites_test.cpp`.

**Evidence, both backends** — see the correction below on *what device*:

```
world only: left (0, 0, 0), right (0, 0, 255)
stacked:    left (0, 0, 0), right (0, 0, 0)
[doctest] test cases:  11 |  11 passed | 0 failed   (gpu bucket)
[doctest] assertions: 477 | 477 passed | 0 failed
```

The world layer's camera takes the left half of the target, the HUD's the right.
Alone, the world leaves the right half at the clear colour; stacked, it is
covered. **That is the ordering proof**, and it works because the world layer
clears colour: a HUD that ran first would have been erased and the right half
would read blue in both rows. The left half staying covered proves the HUD did
not erase what was beneath it.

Full suites green on both targets: 196 fast, 89 integration, 11 gpu.

#### Correction: "a real GPU" was wrong — this is llvmpipe

Written down because it was claimed first and is wrong, and because the same
claim sits on T0028 in a stronger form.

The `gpu` bucket here runs on **Mesa's software rasteriser**, on both backends:

```
[info ] render: Vulkan on 'llvmpipe (LLVM 20.1.2, 256 bits)', 256x256, 3 buffers, vsync off
[info ] render: OpenGL on 'llvmpipe (LLVM 20.1.2, 256 bits)', 256x256, 2 buffers, vsync off
```

The machine **does** have an RTX 4070 Laptop GPU and `/dev/dxg` exists, but
`/usr/share/vulkan/icd.d/` carries no NVIDIA ICD — only `lvp_icd.json`
(lavapipe). So Vulkan resolves to software and there is no hardware path to
resolve to.

**What that does and does not weaken.** Lavapipe and llvmpipe are conformant
implementations, so everything this ticket asserts is genuinely validated: the
API usage, the pipeline states, the depth comparison, the viewport handling and
the composite are all real results. What is *not* covered is hardware driver
behaviour — precision, format support, and the vendor-specific quirks that are
exactly why "it works on my machine" is a phrase. **No test here has ever run on
a hardware GPU.**

Worth fixing at the environment level rather than the ticket level, and worth
knowing before anyone reads a green `gpu` bucket as hardware coverage.

**Superseded in part (2026-08-05, T0135): hardware coverage now exists on the OpenGL backend.** Mesa's `d3d12` gallium driver reaches the RTX 4070 with `GALLIUM_DRIVER=d3d12 MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA`, and the whole gpu bucket passes there with **pixel-identical results** to the software run. Vulkan still has no hardware path on this machine — NVIDIA's WSL package ships no Vulkan ICD and Mesa's Dozen is not packaged — so `RenderBackend::Default` remains lavapipe. See [../open/0135-gpu-tests-on-real-hardware.md](../open/0135-gpu-tests-on-real-hardware.md).

#### The engine renders every mesh pure black, and that is not a T0027 bug

**Measured, not assumed** — `(0, 0, 0, 255)` regardless of `baseColorFactor`,
`emissiveFactor` or alpha. `SceneRenderer` runs `PBR_Renderer` with
`MaxLightCount = 0`, `EnableIBL = false` and `EnableEmissive = false`, so the
shading result is zero. Nothing is wrong with this ticket's code; there is simply
no light in the world yet (**T0079**).

It matters twice over:

- **It changed this ticket's test design.** The first version distinguished the
  two layers by material colour — red world, green HUD — and could not, because
  both are black. The clear colour is a better discriminator anyway, for the
  reason above, but it was found by measuring rather than by reasoning.
- **T0028's "a mesh is drawn" evidence is weaker than it reads.** That test
  asserts pixels differ from a blue clear colour, and they differ *because they
  are black*. It is still a true and useful assertion — geometry reached the
  target through the whole VFS-to-raster path — but it would pass identically if
  shading were completely broken. Recorded on T0079, which is what makes it
  assertable.

#### A view slot picks a camera; it does not filter objects

The two layers in the test draw **two separate scenes**, and that is forced
rather than stylistic. `SceneRenderLayer` runs `parseScene` over whatever scene
it holds, so a world layer and a HUD layer sharing one scene each draw the
other's geometry. Per-layer object filtering is `Camera::cullingMask`, which is
stored on the camera and honoured nowhere — **T0045**.

Until that lands, a HUD's content lives in its own `Scene`. That works and is not
absurd — a HUD genuinely is a separate little world — but it is not what the
"a HUD is just a camera on another slot" framing implies, and someone will be
surprised by it.

#### Two API changes this needed, and why they are not incidental

- **`RenderPassContext` now carries `ClipSpace`**, and `RenderStack::render`
  takes one. A layer that builds a projection cannot get it right without the
  device's clip convention, and a gameplay-authored layer (T0094) has no other
  way to obtain it. Required rather than defaulted: a default-constructed
  `ClipSpace` looks plausible and silently mirrors every projection on OpenGL.
  `ClipSpace` moved from `hp/Render.hpp` to `hp/DepthConvention.hpp` so this
  costs no swap-chain include — which is the argument that header already makes
  for itself.
- **`SceneRenderer::create` takes `std::optional<TargetFormat>` for depth.** A
  HUD binds no depth target, so its pipeline state must declare none. A state
  carrying a DSV format with nothing bound is a **render-pass incompatibility**,
  not a slightly wrong image. `SceneRenderLayer` derives this from its own
  `useDepth` so there is one place that decides, and guards the residual case —
  `useDepth` is public data and can be flipped after `create` — with a named
  error rather than leaving it to the validation layers. The gpu test flips it
  deliberately and asserts the layer refuses.

Also: `FrameTargets::readback` now exists and `SceneView::readback` delegates to
it. The staging copy, the flush-and-wait and the row-by-row stride walk were
about to be duplicated for this ticket's test, and two copies of a readback is
how one of them keeps the stride bug the other fixed.

#### Still not done here

- **T0113.5 remains parked** and is no closer: this stack still cannot compile or
  dispatch a compute shader, so the device-loss abort has still never run.
- **`configureAsHud` is configuration, not a widget system.** T0069 still owns
  what draws HUD *content*; nothing here chose a UI library, which was the point.

## Notes / findings

**The stack must accept layers implemented outside the engine** (T0094). Fog of
war accumulation, minimaps, portal views and custom post effects are all
gameplay-owned passes, and the engine deliberately does not implement those
policies. Design `IRenderLayer` as a public extension point from the start —
retrofitting one means revisiting ordering, resource access and hot-reload safety
all at once.


**The compositing decision matters and is hard to change later.** Drawing all
layers into a single target is cheapest and usually right. Giving each layer its
own render target enables per-layer post-processing and effects but costs memory
bandwidth per layer. Start single-target, but keep `IRenderLayer` able to opt into
its own target, and record the choice.

Depth is the classic trap: the HUD must not depth-test against the world. Per-layer
depth policy, not one global depth buffer used by everything.

This is also where DiligentFX post-processing slots in — tonemapping and bloom
apply to the world layer, not to the UI drawn on top of it. Getting that ordering
wrong makes UI look washed out and is a common engine bug.

### Cross-ticket obligation — T0113 (2026-08-04)

**T0113.5 is parked here, and this is the first ticket able to discharge it.**

Device loss is implemented and its policy recorded (D20): the engine detects a
lost device from the backend's message stream, logs a message naming it as a
GPU/driver failure rather than an engine crash, flushes the log and aborts.
**That abort has never run.** Firing it needs a real GPU hang, a GPU hang needs a
deliberately infinite compute shader, and nothing could author one until this
ticket exists.

So when the render stack can compile a compute shader, add the trigger: a
deliberately hanging kernel behind a debug flag, which trips the OS driver
timeout and costs a rough couple of seconds for the whole machine. Decide at the
same time whether it is worth keeping in the tree permanently.

Why it matters here specifically rather than being a curiosity: **D15 makes the
entire particle system compute-driven**, so the team will write accidental GPU
hangs during development on this very ticket's machinery. "Device lost: GPU hang
or driver reset" versus an unlabelled crash is the difference between a shrug
and a lost afternoon — and the message that makes that difference has never been
seen working.
### Design finding (2026-08-05) — gameplay can drive the RHI, so the extension point is real

Measured before writing any API, because it decides the shape of both this
ticket and T0046: a shared library calling `SetRenderTargets`,
`ClearRenderTarget` and `Draw` through `IDeviceContext*` and `ITextureView*`
**links cleanly under `-Wl,--no-undefined` with zero Diligent libraries linked.**
Diligent's interfaces are pure-virtual, so a call through a pointer is virtual
dispatch and needs no symbol.

This is recorded as **D22**, and it turns 27.1's "the stack must accept layers
implemented outside the engine" (T0094) from a design aspiration into something
straightforward: `IRenderLayer` hands a gameplay layer the device context and the
target views, and the layer issues real draw calls. No C API, no command-buffer
abstraction, no reimplementation of the RHI.

The asymmetry that keeps it safe enforces itself through the linker rather than
through review: **gameplay can use anything it is handed a pointer to, and cannot
create a device, swap chain or engine factory**, because those are free functions
in libraries linked PRIVATE. A module that tries fails to link.

Consequences for this ticket's API:

- `IRenderLayer::onRender` takes a context struct carrying `IDeviceContext*` and
  the target views, not an engine-owned wrapper type. Wrapping would buy nothing
  and cost every gameplay author a translation layer.
- **No `RefCntAutoPtr` crosses the boundary.** Refcount manipulation across the
  module edge reintroduces the ownership question D12 exists to avoid. Raw
  interface pointers, engine-owned, valid for the frame and not beyond — and that
  lifetime rule has to be stated in the header, because it is the one thing a
  gameplay author cannot infer from the type.

### Built 2026-08-05 — the mechanism, not yet the demonstration

`engine/include/hp/RenderStack.hpp`, `engine/src/RenderStack.cpp`,
`tests/fast/render_stack_test.cpp`. 89 fast and 56 integration cases green on
both targets; nine new.

**27.5 is decided: single target.** Every layer draws into the same colour
target, in order. Per-layer targets would buy per-layer post-processing and cost
a full-resolution surface plus a composite pass per layer — bandwidth spent
before anything asked for it. The escape hatch is deliberate rather than absent:
a layer needing its own target renders into one it owns and blits, which is what
a portal view or a minimap does anyway.

**Depth is per-layer (`useDepth`), which is the trap the notes named.** A HUD that
depth-tests against the world vanishes behind whatever geometry is near the
camera, and that reads as flickering UI rather than as a depth bug.

**The stack does not own its layers, and that is a hot-reload requirement rather
than a style choice.** A gameplay module's layer lives in that module, and a
module gets unloaded — engine ownership would mean holding a pointer into a
library that no longer exists. The rule is stated where implementers will read
it: whoever adds a layer removes it before destroying it, and a module removes
its layers on unload.

**Camera and projection are deliberately not in `IRenderLayer`**, despite 27.1
listing them. T0081 owns which camera is active and T0130 owns what a camera
describes; putting a camera reference here before either has landed would fix a
vocabulary that T0130 exists to choose. `order` is what expresses the ordering
constraint that matters today, including where tonemapping (T0096) slots in
between world layers and UI.

### Not done, and the honest shape of it

**`render()` has never been observed doing anything.** The fast bucket has no
device, and stubbing an `IDeviceContext` means implementing several dozen pure
virtuals to observe two calls. So what is tested is the stack's own logic —
ordering, insertion stability, double-add refusal, removal, the null-context
guard — and what is untested is every line that touches Diligent:
`SetRenderTargets`, `ClearRenderTarget`, `ClearDepthStencil`, and whether
`enabled` and `useDepth` actually reach the API.

**No concrete layer exists.** The world layer is 27.3 and needs T0028, which
needs T0023's assets. The HUD layer proving 2D-over-3D is 27.4 and needs
something that draws. So "a world layer and a UI layer both render, correctly
stacked" is not met, and the T0094 Done-when — gameplay implementing its own
layer — is proven *in principle* by D22's link measurement and not in fact,
because no gameplay layer has been written.

**The device test both this ticket and T0046 need is still unwritten**, and it is
the single highest-value next step for either. Its shape: a `tests/gpu/` case
creating a window and a `RenderLayer`, skipping cleanly when no device comes up,
then asserting `FrameTargets::create` succeeds for all three roles on both
backends, that `memoryBytes()` is sane, that resize does not leak, and that a
two-layer stack with one disabled renders exactly one. It must skip rather than
fail on a machine without a GPU, because `-Dtest=all` includes the `gpu` bucket
and CI runners have none — getting that wrong turns four green jobs red for a
reason unrelated to the change.

**T0113.5 remains parked here and is now closer.** The device-loss abort has
never run; firing it needs a deliberately hanging compute shader, and that needs
this stack to be able to compile and dispatch one. It cannot yet. Worth doing
once a layer can dispatch compute, because D15 makes particles entirely
compute-driven, so accidental GPU hangs will be written on this very machinery —
and "Device lost: GPU hang or driver reset" versus an unlabelled crash is the
difference between a shrug and a lost afternoon.
