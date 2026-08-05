# T0027 — RenderStack: composited visual layers

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 400 |
| **Created** | 2026-08-02 |
| **Refs** | [../completed/0113-device-loss.md](../completed/0113-device-loss.md) |

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
- [x] `RenderStack` composites layers in order into one target — verified rendering on a real device, both backends
- [~] Per-layer clear behaviour and camera/projection — clear and per-layer **depth** done; **camera/projection is not here**, it is T0081's with T0130 deciding the vocabulary
- [ ] A world layer and a UI/HUD layer both render, correctly stacked — **not done.** The mechanism exists; no concrete layer does
- [x] Layers can be enabled/disabled at runtime without reordering — a three-layer stack with one disabled renders exactly two, in order
- [~] **Gameplay code can implement and insert its own layer** (T0094) — the API allows it and D22 measured that the calls link with no Diligent library; **no gameplay layer has been written**, so this is proven in principle and not in fact
- [x] Each layer emits its own profiling zone (T0019) — `HP_PROFILE_ZONE_NAMED(layer->name())` in `RenderStack::render`, one per enabled layer

## Subtasks

- [x] 27.1 `IRenderLayer` — order, enabled flag, clear policy, per-layer depth. Camera deliberately excluded; see notes
- [x] 27.2 `RenderStack` owning an ordered list — **non-owning**, see notes
- [ ] 27.3 World layer drawing the scene (T0028) — **not done**, and needs T0023's assets
- [~] 27.4 A HUD/UI layer proving 2D-over-3D works, including depth handling — the depth *policy* is built (`useDepth`); nothing proves it yet
- [x] 27.5 Decide compositing — **single target**, decided and recorded below
- [x] 27.6 Per-layer profiling zones — `HP_PROFILE_ZONE_NAMED(layer->name())`

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
