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
