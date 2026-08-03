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
- [ ] 81.9 Post-resolve view offset seam -- shake, recoil, blends hook here
      (see the 2026-08-03 amendment)
- [ ] 81.10 Implement whichever aspect-ratio policy T0044 decides (see the
      2026-08-03 amendment)

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

### Amendment (2026-08-03) -- two seams from the design-gap survey

**1. A post-resolve view offset costs a line now and a retrofit later** (survey
item 7). `camera shake`, `cutscene`, `cinematic`, camera blending -- zero hits
anywhere; this ticket resolves an active camera and "switching cameras from
gameplay is one call", an instant cut. If 81.3's view matrix comes *strictly*
from the entity transform, gameplay shake must physically move the camera
entity -- workable but ugly, because it pollutes the transform other systems
read (the audio listener follows it, T0100's late-update reads it). 81.9 adds
the seam: after the active camera is resolved, an additive view offset (and,
naturally at the same point, a blend between two resolved cameras) can be
applied without touching the entity. With that seam, shake, recoil, sway and
camera blends become *gameplay* problems, which is where D14/T0094 philosophy
says they belong. The engine ships the hook, not the shake.

A sequencer/timeline for scripted scenes is deliberately **not** scoped here
or anywhere -- it is a subsystem, and whether this game has scripted scenes at
all is now a question on T0044's list.

**2. Aspect ratio needs a stated policy, and it is a design question first**
(survey item 15). `aspect ratio`, `letterbox`, `ultrawide`, `safe area` -- zero
hits in any runtime context; T0033's three fit modes are *editor viewport*
machinery. The game window has no stated policy: free aspect means 21:9 sees
more world -- a real gameplay advantage in a vision-cone stealth game (T0093) --
letterboxing trades that for fairness at the cost of bars, clamping horizontal
FOV is the compromise. T0044 decides; 81.10 implements it where projection is
computed from the viewport rect, and T0069's HUD anchoring picks up the UI
half. Until decided, do not bake "projection always fills the window" into
anything.
