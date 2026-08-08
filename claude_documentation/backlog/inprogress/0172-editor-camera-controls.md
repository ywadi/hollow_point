# T0172 — Editor camera: orbit, fly, and opening a model to look at

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Simple |
| **Phase** | 6 — Editor |
| **Order** | 620 |
| **Created** | 2026-08-08 |
| **Splits from** | **T0063**, with **T0173** (viewport picking) as the other half |
| **Refs** | T0068 (input contexts); T0034 (EditorState, for persistence); T0173 — picking needs the view this ticket produces, and must not build a second camera; **T0165** — the engine is right-handed and the camera looks down its own **−Z**. An editor camera's forward and its orbit maths both follow from that. Nothing here may reintroduce a mirror (a negative camera-parent scale is `WindingConvention.hpp`'s item 5, still unhandled). **D40** — Diligent vendors the camera; we add the seam |

## Why

**The editor has no camera control at all.** No orbit, no fly, no trackball —
`grep` across `apps/editor/src/` finds nothing. It opens on whatever the rockcube
module built, from one fixed vantage, and you cannot move.

The cost is not hypothetical and it is not small. **The owner spent a full day
reading offscreen PNG dumps** because that was the only way to see the renderer's
output from a second angle, and the rock cube's black top face (T0158, T0167) was
found **by eye** rather than by any assertion. Every rendering change in this
repository is currently reviewed either by writing a gpu test that captures a
frame, or by not reviewing it.

**Opening an arbitrary model matters as much as the camera**, for the same
reason: today the editor shows only what a compiled-in gameplay module built, so
looking at the Aston Martin — or any asset an importer just produced (T0169) —
means writing a gpu test.

This is separated from picking (**T0173**) on **dependency risk**: a camera
writes a view matrix and touches no draw path, so it carries zero refactor risk
while T0170 is in flight. Picking touches the draw path and does not. T0063
records the full argument.

## Done when

- [x] The editor viewport can be **flown** — WASD plus mouse-look — and
      **orbited** — drag to rotate, wheel to zoom
- [x] `hp_editor <path-to-model>` opens an arbitrary glTF/GLB and frames it
- [x] The camera obeys **T0165**: forward is the camera's own −Z, and the
      view matrix's determinant is positive (no mirror)
- [x] Nothing here rebuilds what Diligent vendors — the written note says what
      was reused and what the seam is (**D40**)
- [ ] Camera position persists per project across editor restarts

## Subtasks

- [x] 172.0 **Write down what Diligent already provides** before writing a line
      — `FirstPersonCamera`, `TrackballCamera`, `InputControllerBase`
      (`DiligentSamples/SampleBase/`). **D40 is binding.** The owner's own
      reference does the whole thing in ~20 lines:
      `/media/ywadi/second/dillegent_tests/AstonMartinScene/src/AstonMartinScene.cpp`
      — read it, never edit it, it is the owner's
- [x] 172.1 Editor camera state, held outside the scene — it lives on the
      editor's `SceneLayer`, is not an entity, and is not serialized. It does
      *drive* the scene's camera entity each frame; see the notes for why that
      is the right shape until T0033 and T0024 exist, and for the obligation
      it leaves on whatever saves a scene
- [~] 172.2 Orbit / pan / zoom — **built, but not through the input context.**
      The camera reads raw events, so the bindings are not rebindable and do
      not participate in context consumption. **T0068 cannot express them
      yet**: `bindAxis2D` composes an axis from four `KeyCode`s, so mouse
      motion and the wheel cannot reach an `InputMap` at all, and
      `hp::KeyCode` has no modifier entries, so Alt cannot be bound either.
      **T0133** unblocks this
- [x] 172.3 Fly mode with configurable speed
- [x] 172.4 Open a model from the command line (or a drop target — whichever is
      smaller), imported through the VFS like everything else (**D13**)
- [x] 172.5 Frame the opened model automatically, from its bounds
- [ ] 172.7 Persist camera per project in EditorState (T0034)

## Notes / findings

**The editor camera should not end up in a saved scene file.** T0063's argument
stands: if the editor camera is an entity the scene serializes, it appears in the
hierarchy and ships with the game — the same reasoning that keeps the active
scene *out* of EditorState (T0034), applied in the opposite direction.

That is a statement about **serialization**, not about where the state lives this
week. Until T0033's viewport panel and T0024's project exist, the editor's only
camera *is* the scene's camera, and driving the existing camera entity is
strictly less machinery than inventing an editor-only view path that
`SceneView::render` would then have to be taught about. Whichever way it is done,
the obligation is that saving a scene does not write it.

**T0165, restated because both halves of T0063 need it.** The engine is
right-handed; the camera looks down its own −Z. Diligent's camera classes are
written for a **left-handed** view space (their camera looks down +Z), and the
mirror between the two is absorbed by `hp::projectionMatrix`, which negates the
third row of Diligent's left-handed form. So a seam that converts a Diligent
camera's state into an `hp::Transform` must convert **only the rotation
convention** and must never introduce a det −1 basis. `HandednessConvention.hpp`
is the binding statement.
</content>
</invoke>

---

## What landed (2026-08-08)

### 172.0 first, because D40 says so: what Diligent already provides

Written before a line of camera code, which is the subtask's whole point.

| Vendored | Where | Used? |
|---|---|---|
| `FirstPersonCamera` | `DiligentSamples/SampleBase/{include,src}/FirstPersonCamera.{hpp,cpp}` | **Yes** — drives both fly and orbit |
| `TrackballCamera<T>` | `DiligentSamples/SampleBase/include/TrackballCamera.hpp`, header-only | **No** — reason below, and it is not "we preferred ours" |
| `InputControllerBase` + `MouseState` + `InputKeys` | `DiligentSamples/SampleBase/include/InputController.hpp` | **Yes** — the state the cameras read |
| `InputController{Linux,Win32}` | same, per platform | **Linked, never called** — see the build note |
| `CoordinateGridRenderer` | DiligentFX | Not yet; it is an editor viewport grid and belongs to whoever does the viewport |

**Why `TrackballCamera` was not used, and it is a measurement rather than a
taste.** It is the closer fit for orbit and it is what the owner's reference
uses. But it publishes its result only as `m_PrimaryRotation` — a quaternion
composed *inside* `Update`, in Diligent's **left-handed** camera convention, with
no getter for the yaw and pitch that produced it. Consuming that means converting
a second convention with a second set of signs, in a file where every sign is a
mirror waiting to happen (T0165). `FirstPersonCamera` publishes yaw, pitch and
position as scalars, so the conversion happens **once**. Orbit is then the same
yaw and pitch with the camera placed at `pivot - forward * distance`, which is
what orbit *is* — nothing about the rotation differs between the two modes, which
is exactly why one camera serves both.

### The three places the handedness mirror surfaces, each measured

Diligent's camera space is left-handed (its camera looks down **+Z**); hp's is
right-handed and looks down **−Z** (T0165). `hp::projectionMatrix` absorbs that
for rendering. At the *input* seam it surfaces three times, and all three were
measured against Diligent's own `BasicMath.hpp` matrices rather than reasoned
about:

1. **`hp_yaw = pi − diligent_yaw`, `hp_pitch = diligent_pitch`.** With that,
   hp's forward — the negated third row of the entity's world rotation — equals
   Diligent's `GetWorldAhead()` **exactly**, at every yaw and pitch, and the
   basis determinant stays **+1**. So "W" moves toward what the viewport shows,
   and there is no reflection.
2. **`D` binds to `InputKeys::MoveLeft` and `A` to `MoveRight`.** Diligent's
   `MoveRight` travels along its own camera-right, whose dot product with hp's
   screen-right is **−1 at every yaw**. Reading the crossed binding as a typo is
   the trap.
3. **The horizontal mouse delta is negated**, or the view turns the wrong way.

**Two plausible fixes for (3) are wrong, and one of them was written and then
caught.** `SetHandness(false)` applies `-m_fHandness` to the yaw *and* the pitch
delta, so it fixes the yaw and inverts the pitch; a negative `SetRotationSpeed`
does the same. `SetReferenceAxes(..., IsRightHanded=false)` is worse — it builds a
reference rotation with a negated Z column, determinant **−1**, precisely the
mirror T0165 forbids. The reference axes stay at Diligent's default (identity,
det +1) and the handedness is resolved in the conversion.

**The one that was written and caught is the interesting one, because it would
have failed on one target only.** The obvious implementation is to report a
*mirrored cursor position* to the controller. That works on Linux and is silently
void on Windows: `InputControllerWin32::GetMouseState()` **hides** the base method
and re-reads the real cursor with `GetCursorPos`/`ScreenToClient`, so any position
this seam supplies is discarded before either camera sees it. The look direction
would have been inverted on Windows and correct on Linux, with nothing anywhere
saying so — and the editor is not in any suite, so no test would have caught it
either. It was found by reading the Win32 source while checking the link
requirements, not by running anything.

The fix that works on both: `Update` computes `MouseDeltaX = now - m_LastMouseState`,
and `m_LastMouseState` is **protected**, so writing `2 * now - prev` into it makes
the delta come out as exactly `-(now - prev)` regardless of where `now` came from.
Same code, same result, both targets.

### An upstream bug found by measuring: `FirstPersonCamera::SetLookAt` does not invert its own `Update`

It computes `yaw = atan2(V.x, V.z)`; `Update`'s own forward is
`(−sin yaw, 0, cos yaw)` at zero pitch, so the correct inverse is
`atan2(−V.x, V.z)` with `pitch = atan2(V.y, hypot(V.x, V.z))`. Checked over a
13x9 grid of angles: **upstream's version reproduces the input direction at two
points and misses everywhere else**; the signs above round-trip exactly across
the whole grid. `EditorCamera::lookAlong` uses the corrected form and says why
at the call site. Not reported upstream; not used by any sample that round-trips.

### Verified

- **Adopting the rock cube's camera round-trips to the authored value.** The
  scene file has the camera at `position [0, 1.5, 0]`,
  `rotation [-0.145213, 0, 0, 0.9894]`. The editor reads its world matrix, runs
  it through `lookAlong` and back out through `transform()`, and reports
  `at (0.000, 1.500, 0.000) looking (-0.000, -0.287, -0.958)` —
  `sin(-0.29128) = -0.2872`, `-cos(-0.29128) = -0.9579`, i.e. the same pose. That
  exercises the conversion in **both** directions against a number nobody in this
  change chose.
- **Opening the Aston Martin works**, and it is what exposed the clip-plane
  problem below: `opened aston_martin.glb -- 31 mesh(es), 1 material(s);
  radius 566.456 about (0.737, 125.036, -2.589)`, camera placed at
  `(-793.716, 560.273, 1158.659)` looking `(0.539, -0.296, -0.788)` — which is
  the unit vector from that position to that centre, at the framing distance.
- **The determinant is asserted at run time**, not assumed: `isProperRotation`
  runs at startup and logs an error naming T0165 if the basis is ever mirrored.
  This is the check a person *cannot* make by looking — a mirror renders as
  backwards geometry, never as an error, which is how the winding trap was
  misdiagnosed once already.
- `zig build all` clean, `test -Dtest=all` **fast 324 / integration 101** on both
  targets, `test -Dtest=gpu` **71**, `docs` regenerated and gated. Baselines held.

### Two things this change had to fix that were not on the ticket

- **`MeshAsset::boundingSphere()` (new engine API).** Framing needs bounds and
  the engine had none — `grep ComputeBoundingBox engine/` returned nothing. It
  had to go in the **engine** rather than the editor: the bounds come from
  `Diligent::GLTF::Model::ComputeBoundingBox`, defined in `Diligent-AssetLoader`,
  a static library the engine links `PRIVATE` so exactly one copy exists in the
  process. An app computing its own would link a second. It returns four loose
  floats rather than an `hp::float3`, so that `hp/Assets.hpp` does not acquire
  `BasicMath.hpp` (D21: 146,280 lines, ~594 ms per TU) to describe four numbers.
  **This is not T0045's culling bounds** — no world transform, no per-frame
  budget, no hierarchy.
- **The clip planes have to follow the subject.** `hp::Camera` defaults to
  `near 0.1 / far 1000`, which is a sensible game lens and wrong for an asset
  viewer. The Aston Martin is authored in centimetres: radius 566, framing
  distance 1472, **entirely beyond the far plane** — an empty viewport with
  nothing reporting a problem, which is D35's failure class exactly. `openModel`
  now sizes them off the bounds (measured: `near 0.5665, far 8157.0`). Cheap
  because depth is reverse-Z.

### Build note — two vendored `.cpp` files compiled into `hp_editor`

`FirstPersonCamera.cpp` and the platform `InputController*.cpp` are compiled
directly rather than by linking `Diligent-SampleBase`, and the reason is
concrete: that target link-depends **PUBLIC** on `${ENGINE_LIBRARIES}` and
`Diligent-NativeAppBase`, so linking it would pull a second graphics-engine
flavour into a process that already has one through `hp::engine` — the
duplicate-Diligent hazard `engine/CMakeLists.txt` describes and D12 exists to
prevent.

The platform controller is there for **one symbol**: its out-of-line
constructor/destructor, which `EditorInputController` needs because it derives
from the `InputController` typedef — the parameter type
`FirstPersonCamera::Update` takes. None of its event handling is called; the
app's events are SDL3's (T0015). On Linux that costs `XCBKeySyms` (vendored) and
X11/xcb, both already in `third_party/sysroot/`; on Windows it costs nothing
extra. `Diligent-TargetPlatform` is linked for `Diligent::DebugMessageCallback`,
which `LOG_WARNING_MESSAGE` inside `SetReferenceAxes` reaches: the engine builds
with `-fvisibility=hidden` so its copy is not exported. A second copy of a
message sink is contained and is what `Diligent-SampleBase` itself does.

## Not done, and not claimed

- **172.7 — persistence is not implemented.** The camera resets to the framing
  pose every launch. It needs T0034's EditorState and a project to key it on,
  neither of which exists; the "Done when" row stays unticked rather than being
  reworded.
- **172.2's "bound through the input context (T0068)" is not honoured.** The
  camera reads raw events in `SceneLayer::onEvent` rather than going through
  `InputSystem` and an `InputMap`, so the bindings are **not rebindable** and do
  not participate in context consumption. That is a real shortfall against the
  subtask as written: `hp::KeyCode` has no modifier entries, and mouse *buttons*
  reach an `InputMap` but mouse *motion* and the wheel do not — an axis is
  keyboard-composed only (`bindAxis2D` takes four `KeyCode`s). Routing a camera
  through T0068 therefore needs T0133 (pointer input as actions) first. Recorded
  here rather than quietly ticked.
- **Nobody has flown it.** Everything above is measured from logs and from
  numeric round-trips; no human has moved the mouse in this viewport, and a
  camera is judged by using it. Specifically unverified: whether the look and
  strafe directions *feel* right (the signs are measured, the *sensitivity* is
  not), whether the orbit pivot behaves under a long drag, and whether the pan
  scale is comfortable.
- **The viewport is still the whole window.** T0033's panel does not exist, so
  there is no panel-space mapping and the camera takes input whenever the window
  has focus. T0173 needs that mapping and must not invent its own.

## Back-references owed to other tickets — **not yet written**

The backlog rule is that dependencies point both ways, because nobody reads the
ticket they have already closed. These four back-references are owed and were
**not** added, because another session held `claude_documentation/backlog/`
while this landed and the coordination boundary was this ticket plus T0063 and
T0173. Whoever picks this up next should add them; each is one line.

| Ticket | What to add |
|---|---|
| **T0034** (EditorState) | 172.7 is blocked on it. The editor camera has no project to key a saved pose to, so it resets to the framing pose every launch. EditorState should carry a per-project camera pose |
| **T0033** (Viewport panel) | The camera currently takes input whenever the window has focus, because the viewport *is* the window. When the panel exists, the camera must read the panel rect — and **T0173 must use the same mapping**, not invent one |
| **T0133** (Cursor and pointer input as actions) | **T0172.2 could not be honoured because of this.** Routing the camera through T0068's input contexts needs mouse motion and the wheel to be bindable, and they are not: `bindAxis2D` composes an axis from four `KeyCode`s, and `hp::KeyCode` has no modifier entries at all, so Alt cannot be bound either. T0133 unblocks rebindable camera controls |
| **T0064** (Transform gizmos) | The left mouse button is deliberately unbound here and belongs to picking and gizmos |
