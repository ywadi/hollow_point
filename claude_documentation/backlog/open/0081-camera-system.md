# T0081 — Camera system

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Simple |
| **Phase** | 4 — Render layer |
| **Order** | 420 |
| **Created** | 2026-08-03 |

## Why

T0021 gives a camera component and T0028 requires one to render, but nothing
manages cameras: which is active, what happens with several, how gameplay
switches between them, or how the render stack's layers get their own cameras
(T0027 — a HUD layer needs an orthographic camera, not the world's).

## Done when

- [ ] An active camera per render layer, resolved each frame
- [ ] Multiple cameras coexist; priority decides which is active
- [ ] Perspective and orthographic both supported
- [ ] Switching cameras from gameplay is one call
- [ ] Viewport rect per camera, so split-screen or picture-in-picture is possible
- [ ] An object culling mask, so a camera can render only some object layers
- [ ] A camera with no valid target degrades visibly rather than rendering nothing
- [ ] The editor camera (T0063) is explicitly outside this system

## Subtasks

- [ ] 81.1 Camera component: projection, FOV/size, near/far, priority, layer mask
- [ ] 81.2 Active camera resolution per render layer
- [ ] 81.3 Projection and view matrix computation from the transform
- [ ] 81.4 Viewport rect support
- [ ] 81.5 **Object culling mask** (T0085) — which object layers this camera
      renders. Distinct from RenderStack layers, see notes
- [ ] 81.6 Frustum extraction shared with culling (T0045)
- [ ] 81.7 Screen-to-world and world-to-screen helpers — gameplay and UI both need them
- [ ] 81.8 Debug draw of camera frustums (T0061)

## Notes / findings

**Frustum extraction belongs here and is consumed by culling** (T0045) and by LOD
selection (T0040). Computing it in three places is how they drift and produce
objects culled in one system but not another.

**Near and far planes deserve more care than they get.** Too wide a range destroys
depth precision and causes z-fighting; the usual cause is a near plane set to
0.001 out of caution. Default to sensible values and surface the consequence in
the inspector.

The editor camera is deliberately **not** part of this system — it is editor
state, not scene state (T0063), and putting it here would save it into scenes and
ship it with the game.

**Two different things are called "layers", and this ticket touches both.**
`RenderStack` layers (T0027) are *compositing passes* — world, then HUD, then UI.
An **object culling mask** (T0085) selects *which objects* a camera renders, and is
a bitmask tested during culling. A weapon-viewmodel camera uses both: it is its own
render layer composited over the world, *and* it culls to only the `Viewmodel`
object layer.
