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

- [ ] `IRenderLayer` with a render entry point and explicit ordering
- [ ] `RenderStack` composites layers in order into one target
- [ ] Per-layer clear behaviour and camera/projection
- [ ] A world layer and a UI/HUD layer both render, correctly stacked
- [ ] Layers can be enabled/disabled at runtime without reordering
- [ ] **Gameplay code can implement and insert its own layer** (T0094)
- [ ] Each layer emits its own profiling zone (T0019)

## Subtasks

- [ ] 27.1 `IRenderLayer` — order, enabled flag, clear policy, camera
- [ ] 27.2 `RenderStack` owning an ordered list, composited per frame
- [ ] 27.3 World layer drawing the scene (T0028)
- [ ] 27.4 A HUD/UI layer proving 2D-over-3D works, including depth handling
- [ ] 27.5 Decide compositing: draw straight into one target, or per-layer
      targets blended (see notes — this is the real design decision)
- [ ] 27.6 Per-layer profiling zones

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
