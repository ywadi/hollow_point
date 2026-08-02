# T0028 — Scene draw submission and the frame-rendered event

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Phase** | 4 — Render layer |
| **Created** | 2026-08-02 |

## Why

"Render the scene" only becomes a well-defined operation once the scene and asset
model exist. This turns entities into draw calls, and publishes the result so the
editor viewport can display it without the renderer knowing the editor exists.

## Done when

- [ ] Entities with transform + mesh are collected and drawn
- [ ] A scene with no camera renders nothing and says so, rather than crashing
- [ ] Entities with no material get a visible default
- [ ] Output goes to an offscreen target, not straight to the swap chain
- [ ] A "new frame rendered" event carries that texture to any listener
- [ ] Assets resolve from the pool by GUID (T0023)

## Subtasks

- [ ] 28.1 Parse step: filter to entities with transform + mesh, require a camera
- [ ] 28.2 Default material for meshes without one
- [ ] 28.3 Resolve mesh/material GUIDs against the asset pool
- [ ] 28.4 Render to an offscreen target sized to the viewport
- [ ] 28.5 Emit the frame-rendered event with the texture handle
- [ ] 28.6 Profiling zones for parse and submit separately

## Notes / findings

**Rendering to an offscreen target rather than the swap chain is what makes the
editor viewport possible at all** — the viewport is an ImGui image of that
texture. The runtime (Phase 8) then just stretches the same texture full-window,
which is why both apps can share this code unchanged.

The frame-rendered event is the *only* thing connecting renderer and viewport.
Resist the temptation to hand the viewport a renderer pointer; that coupling is
what the event system exists to avoid.

Sorting, culling and instancing are deliberately out of scope here — get correct
output first. But leave the parse step's output as an explicit list so a sort or
cull pass can be inserted without restructuring.
