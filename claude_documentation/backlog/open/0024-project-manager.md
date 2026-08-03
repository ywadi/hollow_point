# T0024 — ProjectManager

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 3 — Data model |
| **Order** | 290 |
| **Created** | 2026-08-02 |

## Why

A project is the coordination of an open scene plus a loaded asset pool, so this
sits on top of T0021-T0023 and genuinely cannot be built before them.

It is also the seam Phase 8 export depends on: the runtime uses the
ProjectManager and the render layer and nothing else, which is what keeps the
exported game free of editor code.

## Done when

- [ ] New / Open / Save / Close, each coordinating scene and asset managers
- [ ] A folder is recognised as a project by an `.hpproj` file
- [ ] Close resets engine state fully, so opening another project is clean
- [ ] Opening a project restores its scene and assets
- [ ] Tests for the full new → save → close → open cycle

## Subtasks

- [ ] 24.1 `.hpproj` format via T0020 — name, version, startup scene, settings
- [ ] 24.2 `New` — create the folder structure and an empty scene
- [ ] 24.3 `Open` — validate, load assets from metafiles, load the scene
- [ ] 24.4 `Save` — scene plus project file, metafiles already written on import
- [ ] 24.5 `Close` — tear down in the right order and reset state
- [ ] 24.6 Define the on-disk project layout and document it
- [ ] 24.7 Tests

## Notes / findings

**`Close` is where state leaks show up.** Anything cached outside the scene and
asset managers — GPU resources, editor selection, undo history — has to be reset,
or opening a second project inherits fragments of the first. Cheap to get right
now, painful to debug later.

The **active scene lives in the SceneManager/ProjectManager, not in EditorState**
(T0034). EditorState vanishes when the editor is stripped at export, and the
runtime still has to know which scene to load. This is a real trap in Laura's
account and worth stating explicitly.

Project *settings* (render quality, startup scene) belong here rather than in the
editor, for the same reason.
