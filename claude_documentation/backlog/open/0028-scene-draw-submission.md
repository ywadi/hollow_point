# T0028 — Scene draw submission and the frame-rendered event

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 410 |
| **Created** | 2026-08-02 |
| **Refs** | T0120 |

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

### Second review pass (2026-08-03) — nothing displays this texture until Phase 6

The two consumers of the frame-rendered event are the viewport panel (T0033,
Phase 6) and the runtime (T0042, Phase 8) — so as written, all of Phase 4 has
no on-screen output. Add a trivial dev-only present path (blit the offscreen
target to the swap chain in the editor app's stub layer) so Phase 4 work is
visually verifiable as it lands. It is ~20 lines, it exercises the same event,
and T0033 simply replaces it.

### Cross-ticket obligations (2026-08-04, T0124 backfill)

- **T0120.2** makes this submission callable per-camera against an arbitrary
  target (portals, security monitors, thumbnails). Keep the parse/submit path
  free of one-implicit-camera, one-viewport-target assumptions — the explicit
  parse-output list in the notes is half of that; camera and target as
  parameters are the other half. Retrofitting them means restructuring
  submission, which is what T0120 was filed to avoid.
