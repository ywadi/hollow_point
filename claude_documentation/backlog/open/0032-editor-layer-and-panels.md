# T0032 — Editor layer and panel framework

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 6 — Editor |
| **Created** | 2026-08-02 |

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

## Subtasks

- [ ] 32.1 `EditorLayer` in `apps/editor`
- [ ] 32.2 ImGui context, Diligent renderer backend and platform wiring
- [ ] 32.3 Dockspace over the main viewport
- [ ] 32.4 `IEditorPanel` and the panel collection
- [ ] 32.5 Persist ImGui layout (`imgui.ini`) into the project or user config
- [ ] 32.6 Verify the engine has no editor references

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
