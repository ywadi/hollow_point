# T0042 — Runtime application

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Simple |
| **Phase** | 8 — Runtime & export |
| **Created** | 2026-08-02 |

## Why

The second consumer of the engine library, and the proof that the editor/engine
separation (T0013) actually held. It uses only the ProjectManager and the render
layer — no editor code, no panels.

Because it never changes between games, it is compiled once and simply copied at
export time (T0043).

## Done when

- [ ] `apps/runtime` builds for both targets and links only engine + Diligent
- [ ] It opens a project folder, loads the startup scene and renders it
- [ ] One full-window render layer, no editor UI
- [ ] It contains no editor symbols — checkable
- [ ] It runs the same project the editor was editing, identically

## Subtasks

- [ ] 42.1 `apps/runtime` with its own `CreateApplication` and `readme.md` (G8)
- [ ] 42.2 A minimal layer: no panels, one full-window viewport
- [ ] 42.3 Listen for the frame-rendered event and present full-window
- [ ] 42.4 Locate and open the project relative to the executable
- [ ] 42.5 Prefer cooked binary assets, fall back to YAML (T0020)
- [ ] 42.6 Verify no editor code is linked in

## Notes / findings

**This is the real test of the architecture.** If the runtime turns out to need
something that lives in the editor, that thing was in the wrong place — the
canonical example being the active scene, which is why it lives in the scene
manager and not EditorState (T0034).

Window sizing differs from the editor: the editor renders into a panel-sized
offscreen target, the runtime into a window-sized one. Same code path from T0028,
different dimensions — a good check that the abstraction is right.

The runtime should ship with profiling compiled out entirely (T0031).
