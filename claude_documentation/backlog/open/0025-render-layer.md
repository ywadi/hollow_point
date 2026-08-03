# T0025 — Render layer and device lifecycle

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 380 |
| **Created** | 2026-08-02 |

## Why

Rendering becomes its own system layer on the LayerStack (T0017), which is what
lets the runtime reuse it in Phase 8 with no editor present.

This owns the Diligent device, contexts and swap chain, and their lifetime
relative to everything that allocates GPU resources.

## Done when

- [ ] `RenderLayer : ILayer` owning device, immediate context and swap chain
- [ ] Backend selectable at runtime (Vulkan default, OpenGL fallback)
- [ ] Window resize resizes the swap chain without artefacts or leaks
- [ ] Clean shutdown with no validation-layer complaints
- [ ] Runs on both Linux and Windows targets

## Subtasks

- [ ] 25.1 Device/context/swapchain creation via Diligent's engine factories
- [ ] 25.2 Backend selection and command-line override
- [ ] 25.3 Resize handling, driven by the window resize event (T0018)
- [ ] 25.4 Shutdown ordering — GPU resources released before the device
- [ ] 25.5 Profiling zones using the T0019 macros while writing, not after
- [ ] 25.6 Enable Vulkan validation layers in debug builds

## Notes / findings

Both engines are **shared libraries** (`GraphicsEngineVk_64r.dll`,
`GraphicsEngineOpenGL_64r.dll`) and must sit beside the executable on Windows —
`cmake/dist.cmake` already stages them correctly.

Diligent loads the backend through a factory; on Linux the `.so` is found via
RUNPATH, which the harness already sets (and deliberately excludes the sysroot
stubs from — G6).

Turn validation on in debug from the very first frame. Every hour spent without
it is an hour of bugs deferred to a worse time.
