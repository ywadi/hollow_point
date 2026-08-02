# T0027 — RenderStack: composited visual layers

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Phase** | 4 — Render layer |
| **Created** | 2026-08-02 |

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
