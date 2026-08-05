# T0081 — Camera system

| | |
|---|---|
| **Status** | ⏸ BLOCKED |
| **Priority** | Medium |
| **Complexity** | Simple |
| **Phase** | 4 — Render layer |
| **Order** | 420 |
| **Created** | 2026-08-03 |
| **Refs** | T0100, [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md), T0111, T0120, [../completed/0130-camera-lens-model.md](../completed/0130-camera-lens-model.md), **blocked on** [../inprogress/0028-scene-draw-submission.md](../inprogress/0028-scene-draw-submission.md) and [../open/0061-debug-draw.md](../open/0061-debug-draw.md), [../open/0045-culling-and-render-queues.md](../open/0045-culling-and-render-queues.md), [../open/0085-layers-and-masks.md](../open/0085-layers-and-masks.md) |

## Why

T0021 gives a camera component and T0028 requires one to render, but nothing
manages cameras: which is active, what happens with several, how gameplay
switches between them, or how the render stack's layers get their own cameras
(T0027 — a HUD layer needs an orthographic camera, not the world's).

## Done when

- [~] An active camera per render layer, resolved each frame
- [x] Multiple cameras coexist; priority decides which is active
- [x] Perspective and orthographic both supported
- [x] Switching cameras from gameplay is one call
- [x] Viewport rect per camera, so split-screen or picture-in-picture is possible
- [~] An object culling mask, so a camera can render only some object layers
- [~] A camera with no valid target degrades visibly rather than rendering nothing
- [x] The editor camera (T0063) is explicitly outside this system

## Subtasks

- [x] 81.1 Camera component: projection, FOV/size, near/far, priority, layer mask
- [~] 81.2 Active camera resolution per render layer
- [x] 81.3 Projection and view matrix computation from the transform
- [x] 81.4 Viewport rect support
- [~] 81.5 **Object culling mask** (T0085) — which object layers this camera
      renders. Distinct from RenderStack layers, see notes
- [x] 81.6 Frustum extraction shared with culling (T0045)
- [x] 81.7 Screen-to-world and world-to-screen helpers — gameplay and UI both need them
- [ ] 81.8 Debug draw of camera frustums — BLOCKED on T0061 (T0061)
- [x] 81.9 Post-resolve view offset seam -- shake, recoil, blends hook here
      (see the 2026-08-03 amendment)
- [x] 81.10 Decide the aspect-ratio policy and implement it -- **this ticket
      owns it**, because this is where projection is computed from the viewport
      rect (see the 2026-08-03 amendment)

## Notes / findings

## Blocked on — 2026-08-05

Eight of ten subtasks are done and the remaining work is **not this ticket's to
do**. Both blockers are tickets that do not exist yet:

| Blocker | What it unblocks here |
|---|---|
| [T0028](../inprogress/0028-scene-draw-submission.md) — scene draw submission | 81.2's "resolved each frame". `resolveCamera` and `buildView` are built and tested; nothing calls them in a frame because nothing draws. `RenderStack` deliberately does not depend on `Scene`, so the resolve belongs where draws are submitted. |
| [T0061](../open/0061-debug-draw.md) — debug draw service | 81.8 entirely. `extractFrustum` already returns the six planes; only the ability to draw lines is missing. |

Two further items are stored-but-unconsumed by design, and are *not* blockers on
this ticket — they are obligations on the tickets that consume them:

- `Camera::cullingMask` is read by nothing until [T0045](../open/0045-culling-and-render-queues.md), against object layers from [T0085](../open/0085-layers-and-masks.md).
- Exposure is applied by nothing until T0096.

Both obligations are recorded on those tickets, not only here.

## Progress — 2026-08-05

Eight of ten subtasks are done. **This ticket is not finished**: 81.8 is blocked
on a debug-draw system that does not exist, and the per-frame wiring belongs to
T0028. What follows is what was built and what was deliberately not.

### Built

`hp/CameraSystem.hpp` and `engine/src/CameraSystem.cpp`, with the lens itself
still in `hp/Camera.hpp` (T0130). The split is the one the ticket asked for:
T0130 decides what a camera *describes*, this decides which one is *active* and
what matrices follow.

**Nothing here owns state.** `resolveCamera` is a query over the scene returning
a value, not a cached "active camera" pointer. A cached handle would have to be
invalidated when the entity is destroyed, the scene reloads, or a module
unloads — a query cannot dangle.

- **81.1** — `Camera` gained `priority`, `enabled`, `viewport`, `cullingMask`,
  `viewSlot`, `aspectPolicy`, `referenceAspect`; all reflected except the two
  compound types, so the inspector shows them.
- **81.2** — `resolveCamera(scene, viewSlot)`: highest priority among enabled
  cameras on that slot. **A tie is logged rather than resolved silently**,
  because the fallback is registry order and registry order is not a stable
  guarantee — a scene that renders correctly today would change on an unrelated
  edit.
- **81.3** — view matrix from the entity's `WorldTransform`, so a camera on a
  boom arm or a head socket works with no special case. Tested with a parented
  camera.
- **81.4** — viewport rects are **normalised**, so split-screen halves stay
  halves across a resize with nothing recomputing them.
- **81.6** — Gribb-Hartmann frustum extraction from the view-projection matrix,
  planes normalised so distances are real distances. Derived from the matrix
  rather than the lens, so reverse-Z needs no special case. **The near plane is
  the one that depends on the clip-space convention** and is branched on it.
- **81.7** — `worldToScreen` and `screenToWorldRay`. The first **refuses** a
  point behind the camera rather than projecting it, which is the specific
  failure where a world-space marker appears mirrored behind the player. The
  second builds the ray from 1 towards 0 under reverse-Z, which is the opposite
  of the conventional reading.
- **81.9** — the post-resolve view offset seam. `buildView` takes a matrix
  applied to the camera's world transform *after* resolution, so shake, recoil
  and camera blends never touch the entity — which matters because the audio
  listener and T0100's late update both read it.

### 81.10 — aspect-ratio policy: decided, all three implemented

**Default is free aspect**, chosen by the owner on 2026-08-05 after being
presented with the trade. All three are implemented and it is a **per-camera**
field, not a global setting:

| Policy | On a 21:9 window |
|---|---|
| `FreeAspect` (default) | Sees more world. No bars. |
| `ClampHorizontalFov` | Same horizontal extent as the reference, gains vertical. No bars. |
| `Letterbox` | Identical framing everywhere, bars at the sides. |

Per camera rather than global because a world camera can clamp for fairness
while a HUD's orthographic camera stays free — letterboxing a HUD camera is
never right, and one global setting cannot express that. It is a plain field, so
gameplay assigns it directly (D12) and the next frame picks it up; there is no
invalidation step to forget.

Two details that a one-line implementation would get wrong, both tested:

- **Clamping only applies when the window is *wider* than the reference.** A
  naive `min` would widen the view on a 4:3 monitor — the opposite of the
  policy's purpose.
- **A letterboxed view reports the reference aspect, not the window's.**
  `ResolvedView::aspect` is explicit for this reason; code that reads the window
  aspect instead produces an image stretched by exactly the letterbox ratio.

### Not done

- **81.8 debug draw of camera frustums — blocked.** T0061's debug-draw system
  does not exist. `extractFrustum` gives it everything it needs; only the
  drawing is missing.
- **81.5's mask is stored, not consumed.** Nothing tests visibility against
  `cullingMask` because culling is T0045. It is stored now because adding it
  after cameras are authored costs a component migration.
- **"Resolved each frame" is a mechanism, not yet a wiring.** Nothing calls
  `resolveCamera` during a frame, because nothing draws yet. `RenderStack`
  deliberately does not depend on `Scene` — a compositing pass has no business
  knowing about the ECS — so the resolve belongs to whatever submits draws.
  **That is T0028**, and the obligation is recorded on its ticket.
- **"Degrades visibly" is half-met.** A camera with an unusable lens or an
  off-target viewport yields no view and logs an error, rather than rendering a
  meaningless frame. Showing *something* to the player is a caller decision and
  no caller exists.
- **No pixel has gone through any of this.** Every assertion is arithmetic on a
  scene. The clip-space convention is device-measured (T0130) but no draw has
  used these matrices.

### Evidence

`tests/fast/camera_system_test.cpp`, 22 cases and 96 assertions, green on both
targets. Full suite: fast 123, integration 56, gpu 2 (on an RTX 2080), and
`zig build docs` passes.



### Frame anatomy — phase 8 — late update (T0100, D17)

Camera follow runs in `onLateUpdate` (**phase 8**), never `onUpdate`. In
`onUpdate` it reads this frame's or last frame's target position depending on
which layer was registered first — intermittent jitter that profiles as nothing.
This is a rule, not a convention.

The full order is in [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md); the decision and what it rejected is **D17** in the
decision log. If this ticket needs a phase that does not exist, that is a change
to T0100's document and to D17 — not a new call bolted into `Application::run`.

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
or anywhere -- it is a subsystem in its own right. Whether the engine ships one
needs its own ticket, raised when someone is ready to argue the cost; it is not
a property of any particular game, and it does not belong to this ticket beyond
the seam at 81.9.

**2. Aspect ratio needs a stated policy, and it is a design question first**
(survey item 15). `aspect ratio`, `letterbox`, `ultrawide`, `safe area` -- zero
hits in any runtime context; T0033's three fit modes are *editor viewport*
machinery. The game window has no stated policy: free aspect means 21:9 sees
more world -- which is a real advantage in any game where what you can see is a
mechanic, so it is a fairness question the engine has to have an answer for --
letterboxing trades that for fairness at the cost of bars, clamping horizontal
FOV is the compromise. **81.10 decides and implements it**, where projection is
computed from the viewport rect, and T0069's HUD anchoring picks up the UI half.
The engine-shaped answer is most likely to *offer* the policy as a setting
rather than to pick one: the three differ by a few lines here and by a great
deal to a game that wanted a different one. Until decided, do not bake
"projection always fills the window" into anything.

### Cross-ticket obligations (2026-08-04, T0124 backfill)

- **T0111.2**: the TAA-shaped hook injects sub-pixel jitter at the projection
  this ticket computes (81.3). Leave the seam where projection is assembled —
  the same shape as 81.9's post-resolve view offset — so jitter does not need
  a retrofit through every projection consumer.
- **T0120.1** gives this camera component an optional texture target. Leave
  room in 81.1 rather than assuming every camera contributes to the composited
  swap-chain frame.

### Cross-ticket obligation — T0130 (2026-08-05)

**This ticket resolves *which* camera is active; T0130 decides what a camera
describes.** They are easy to conflate and must not be: active-camera selection,
priority, viewport rect and culling mask are yours, while the lens vocabulary —
vertical FoV versus focal length and sensor size, aspect derivation, projection
convention, exposure ownership, depth-of-field storage — is T0130's.

Two of its outcomes bind this ticket directly:

- **Aspect ratio is derived from the viewport, not stored on the camera.** That
  makes it *this* ticket's job to supply, since the viewport rect lives here. A
  stored aspect goes stale on every resize and yields subtly stretched output
  that nobody notices for weeks.
- **The projection convention (reverse-Z, depth range, handedness) must be
  measured on both backends.** OpenGL's clip-space Z is `[-1, 1]` and Vulkan's is
  `[0, 1]`, both ship here (D16), and Diligent's projection helpers take an
  explicit GL flag. Whichever way T0130 decides, this ticket builds the matrices
  and is where getting it wrong shows up as a mirrored or depth-fighting scene on
  exactly one backend.
