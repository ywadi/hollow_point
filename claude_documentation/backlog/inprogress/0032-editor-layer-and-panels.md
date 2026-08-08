# T0032 — Editor layer and panel framework

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 6 — Editor |
| **Order** | 600 |
| **Created** | 2026-08-02 |
| **Refs** | **T0174** — the material inspector, shader hot reload and the two-reflection-systems question **moved there** (32.7/32.8/32.9 → 174.1/174.2/174.3); this ticket builds the panel collection they register into, and they must not build a second one. [0033-viewport-panel.md](../open/0033-viewport-panel.md) — the viewport becomes a panel here, because a dockspace with nothing docked in it is not a dockspace; [0172-editor-camera-controls.md](0172-editor-camera-controls.md) — the camera reads input for the whole window today and must read the viewport panel's rect once one exists; [0035-hierarchy-and-inspector.md](../open/0035-hierarchy-and-inspector.md) — panels that register into this collection; [../completed/0085-layers-and-masks.md](../completed/0085-layers-and-masks.md) — 85.6's mask editor widget waits on T0174's inspector, not on this shell; [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) **D6** (ImGui: upstream docking branch via `DILIGENT_DEAR_IMGUI_PATH`, and ImGui ships in the runtime too), **D12** (the editor is a module host) |

## Inherited from T0061 (2026-08-08, T0171) — two vendored visualisations are yours

**Moved here rather than kept on the debug-draw ticket**, because neither is a
debug-draw primitive and keeping them there made that ticket look half-vendored
when it is not.

- **`CoordinateGridRenderer`** (`DiligentFX/Components/interface/CoordinateGridRenderer.hpp`)
  is a complete, depth-aware, full-screen ray-marched infinite grid with flags for
  three planes and three axes. It is **the editor grid**, and this engine never
  constructs it. `HnPostProcessTask.cpp:181-425` is the worked example of driving it.
- **`BoundBoxRenderer`** draws **one** box per `Prepare`+`Render` pair, with colour
  and a dash pattern — which is wrong for the hundred bounds a culling
  visualisation wants, and exactly right for **one selection outline**.
  `HnRenderBoundBoxTask.cpp:141-209` shows the sequence.

Both are 🔧 *switch off* rows in
[`../../documentation/12-vendored-capabilities.md`](../../documentation/12-vendored-capabilities.md)
with this ticket named as owner. See [0061-debug-draw.md](../open/0061-debug-draw.md).

## Why

The editor is added as **one layer** pushed onto the LayerStack, which is what
keeps the engine library ignorant that an editor exists. Inside it, UI is
organised as panels behind an `IEditorPanel` interface so the editor layer does
not itself become a dumping ground.

## Done when

- [ ] `EditorLayer : ILayer` lives in `apps/editor`, not in the engine
- [ ] `IEditorPanel` with an update entry point; the layer owns and iterates them
- [ ] An ImGui dockspace hosts panels, and docking actually works
- [ ] Dock layout persists across editor restarts
- [ ] `grep -ri editor engine/` returns nothing
- [ ] The viewport is a **panel** in the dockspace, not the whole window
- [ ] The zero plane and the world axes are visible, and occlude correctly
      against geometry

## Subtasks

- [ ] 32.1 `EditorLayer` in `apps/editor`
- [ ] 32.2 ImGui context, Diligent renderer backend and platform wiring
- [ ] 32.3 Dockspace over the main viewport
- [ ] 32.4 `IEditorPanel` and the panel collection
- [ ] 32.5 Persist ImGui layout (`imgui.ini`) into the project or user config
- [ ] 32.6 Verify the engine has no editor references
- [ ] 32.10 The **grid and world axes**, from `CoordinateGridRenderer` — a
      configuration job, not a feature (see the inherited section above)

## Descoped

**32.7, 32.8 and 32.9 moved to [T0174](../open/0174-material-inspector-and-shader-hot-reload.md)**
on 2026-08-08, whole and with their notes. They are major functionality — shader
hot reload, a reflection-driven material inspector, and unifying two reflection
systems in one presentation path — and they were here only because two closed
tickets needed *an* editor to put them in and this was the only editor ticket.
The owner's framing for this ticket is the shell: *"I want docks so the editor
turns out awesome and clean... We don't want to add any major functionality
yet."* The long "inspector's reflection question" section went with them, along
with T0160's answer to it.

## Notes / findings

**ImGui docking is already proven working** — that was the whole point of the
earlier probe app: `ImGui 1.92.9b, docking ON`, verified on both OpenGL and
Vulkan, and a screenshot showing a window docked into a dockspace. Link
`Diligent-Imgui`, not raw ImGui; it carries `ImGuiDiligentRenderer` and the
per-platform impls.

**ImGui is not optional anyway** — `DiligentFX` links `Diligent-Imgui` PUBLIC and
its post-process components call `ImGui::` for their settings panels (D6).

Decide where `imgui.ini` lives. Per-project keeps layouts with the work; per-user
avoids churning the project on every window drag. Per-user is the usual choice
and probably right.


### Architecture decision (2026-08-03) — the editor is a module host (D12)

The editor loads the gameplay module, exactly as the runtime does. That is not
an incidental capability — it is how the editor knows about game-defined types
at all, and it is the model this project is following deliberately (Godot loads
extensions into the editor for the same reason).

Consequences this ticket did not previously account for:

- The editor links the **shared** engine library and hosts modules through the
  same loader the runtime uses. The build-id check (T0104) lives in that shared
  loader, or the editor will cheerfully load a stale module and the failure will
  present as a broken panel
- In-editor hot reload becomes first-class rather than a runtime-only concern
  (T0048), because the editor is where reload actually gets used
- Panels that display game-defined types depend on the module being loaded, so
  "no module loaded" and "module failed to load" are real editor states that
  need designing, not error paths to bolt on

---

## What landed (2026-08-08) — the shell

### 32.2 first, because D40 says so: almost all of it was already here

The first written note is what Diligent already provides, and on this ticket the
answer is *nearly everything*, including two things nobody had checked.

| Vendored | Where | Used? |
|---|---|---|
| Dear ImGui **1.92.9b, docking branch** | `third_party/imgui`, already wired in via `DILIGENT_DEAR_IMGUI_PATH` (**D6**) | **Yes** |
| `ImGuiImplSDL3` — context, platform backend, `HandleSDLEvent` | `DiligentTools/Imgui` | **Yes**, engine-side |
| `ImGuiDiligentRenderer` — the RHI renderer backend | same | **Yes**, through the above |
| `CoordinateGridRenderer` — grid **and** axes, depth-aware | `DiligentFX/Components` | **32.10, not yet** — see below |

**Which ImGui the editor links, since the question was asked explicitly:**
`third_party/imgui`, the **docking** branch at 1.92.9b — `IMGUI_HAS_DOCK` is
defined in its `imgui.h`. Diligent's bundled copy
(`DiligentTools/ThirdParty/imgui`, 1.92.1, no `IMGUI_HAS_DOCK`) is **not**
compiled by this build and never was: `CMakeLists.txt:124` forces
`DILIGENT_DEAR_IMGUI_PATH` at our copy, which is D6 executed. So sorting it out
was not part of 32.2 — it had already been sorted, before this ticket started.

**One thing was genuinely missing, and it fails as four undefined symbols rather
than as a build error you can read.** `Diligent-Imgui` compiles its own
`ImGuiImplSDL3.cpp` wrapper, but the ImGui backend that wrapper calls into —
`imgui_impl_sdl3.cpp` — is added to the build **on Win32 only**
(`DiligentTools/Imgui/CMakeLists.txt` appends `imgui_impl_win32.cpp` and nothing
for SDL). The object file exists and links against nothing. `hp_engine` compiles
that one source now, with the reason at the call site.

### The architecture question 32.2 actually turned on: who owns the ImGui context

**Not the editor, and the constraint is a link constraint rather than a
preference.** `ImGuiImplSDL3` needs SDL3 symbols and the `SDL_Window*`. SDL3 is
linked **statically and PRIVATE** into `libhp_engine`, so a second binary linking
SDL3 to reach that backend gets its own copy of SDL's globals — its own event
queue, its own video subsystem — and every call it makes about *this* window goes
to a library that has never heard of it. That is not untidiness; it does not
work.

So the split is:

- **the engine hosts the context** — `RenderConfig::ui` creates
  `ImGuiImplSDL3`, opens the frame at late update, and submits the draw data
  between the present blit and `Present`. Nothing about it names an editor
  (32.6), and D6 already established ImGui ships in the runtime too;
- **the editor draws** — it links `Diligent-Imgui` for the API, and
  `EditorLayer::onAttach` points this binary's copy at the engine's context with
  `SetCurrentContext` + `SetAllocatorFunctions`, which is Dear ImGui's own
  documented arrangement for a context shared across module boundaries. Sound
  here because both copies compile from the same source with the same
  `IMGUI_USER_CONFIG` — linking the same CMake target is what makes that true by
  construction. `IMGUI_CHECKVERSION()` runs immediately after the bind, because
  that is the failure this arrangement risks.

**Two smaller engine additions fell out of it**, both generic and both
documented where they are: `Window::platformWindow()` (SDL's object, distinct
from `nativeHandles()`'s OS handle) and `Window::setPlatformEventSink` — a
pre-translation hook for a vendored backend that speaks `SDL_Event`. The sink
**cannot consume**; whether the UI took an input is answered by asking
`ImGuiIO::WantCaptureMouse`, not by swallowing events the layer stack is supposed
to order.

### Where the UI draws, and why there is only one place it can

`RenderLayer::onRender` clears, blits the scene, **then** submits ImGui, then
presents. Before the blit and the UI is painted over; after `Present` and it
lands in a buffer already handed to the compositor. `NewFrame` is at **late
update**, not render, because layers render in push order and the render layer is
pushed last — a frame opened in `onRender` would open after every panel had
already tried to draw into it.

### The viewport is a panel, which is T0033's substance

A dockspace with nothing docked in it is not a dockspace, so this much of T0033
landed here. Three things followed, each of which was a latent bug while the
viewport was the window:

- **the scene renders at the panel's size**, not the swap chain's — otherwise it
  is resampled by whatever fraction of the window the panel occupies, and every
  pixel measurement taken off it later (picking, gizmos) is taken off the wrong
  grid;
- **the camera takes input only when the viewport is hovered** — this closes the
  gap T0172 recorded, where dragging a slider would have flown the view;
- **the engine's dev present blit is switched off**, or the scene appears twice.

### Layout, and where it lives

Per **user**, not per project: `$XDG_CONFIG_HOME/hollowpoint/editor-layout.ini`
(`%APPDATA%` on Windows), which is what T0032 predicted. A dock layout changes
every time somebody drags a splitter and a per-project file would put that in
everyone's diff. A project override is additive when T0024 exists.

`ImGuiImplDiligent`'s constructor sets `io.IniFilename = nullptr`, so a context
created through it persists nothing until told where to — which is why
`RenderLayer::setUiLayoutPath` exists and stores the string: ImGui keeps the
pointer, not the characters.

### Deliberately off: multi-viewport

`ImGuiConfigFlags_ViewportsEnable` — panels torn off into real OS windows — needs
`UpdatePlatformWindows`/`RenderPlatformWindowsDefault` driven around the present,
and `ImGuiImplDiligent` does not do it. Turning the flag on without that gives a
blank second window, which reads as a renderer bug. Docking *within* the main
window is what was asked for and is what is on. Additive later, at the cost of a
swap chain per window.

### 32.10 — the grid and axes: what Diligent provides, and why it is not built yet

**D40 note first, and it confirms the framing that this is configuration rather
than code.** `DiligentFX/Components/interface/CoordinateGridRenderer.hpp` ships
the zero plane *and* the world axes in one component:

- `FEATURE_FLAG_RENDER_PLANE_XZ` (and `_YZ`, `_XY`) — the plane;
- `FEATURE_FLAG_RENDER_AXIS_X` / `_Y` / `_Z` — the axes;
- `CoordinateGridAttribs` (`Shaders/Common/public/CoordinateGridStructures.fxh`)
  already defaults to **+X red, +Y green, +Z blue** with dimmed negatives and
  per-axis pixel widths;
- `RenderAttributes` takes `pDepthSRV`, so the grid **occludes correctly against
  geometry** rather than floating over it. Passing the real depth buffer is not
  optional.

`HnPostProcessTask.cpp:181-425` is the worked example of driving it.

**It is not built yet, and the reason is a live collision rather than an
oversight.** The grid needs three things that all live inside the engine's scene
pass: the colour RTV, the scene **depth as an SRV**, and `CameraAttribs`. There
were two ways in:

1. a bespoke hook on `SceneView` — which is a fourth one-off insertion point into
   the frame, and is exactly the divergence **T0120** and **D40** exist to stop;
2. a pass in **T0094's `RenderExtensions`** — an `IRenderLayer` inserted at a
   stated `order`, with `RenderPassContext::targets` giving the depth slot **by
   name**. That is the frame seam, it is what the seam is for, and it answers
   "where does it belong relative to the scene pass and the present blit"
   properly: after the scene pass, before the present.

**T0094 was being implemented in `engine/` by another session while this ticket
was being worked**, and its header landed while this was in progress. Building
(1) would have been a second seam obsolete on the day it was written; building
(2) against uncommitted, still-moving API would have collided head-on. So the
grid waits for T0094 to land and then goes in as a pass, which is one ticket of
delay and no rework.

**Two things to check when it is built, and to check rather than infer**: the
grid renders in world **XZ**, and this engine is right-handed with the camera
down its own **−Z** (T0165) — so confirm on screen that the plane and the axis
directions are the ones intended, rather than reasoning about them. The same
document warns that a handedness error here does not look like an error.
