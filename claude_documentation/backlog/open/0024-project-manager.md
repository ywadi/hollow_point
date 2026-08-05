# T0024 — ProjectManager

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 3 — Data model |
| **Order** | 290 |
| **Created** | 2026-08-02 |
| **Refs** | [../completed/0023-asset-manager.md](../completed/0023-asset-manager.md), [../completed/0022-scene-serialization.md](../completed/0022-scene-serialization.md) (owns the document; this ticket owns the file), [../../documentation/10-scene-file-format.md](../../documentation/10-scene-file-format.md) |

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

### From T0023 (2026-08-05) — load-on-project-open lands here

T0023 closed with "reopening a project reloads assets from metafiles and
reconnects scenes" **moved to this ticket**. The mechanism is built and proven;
what does not exist is a *project* to open.

What is ready to use:

- `hp::importAsset(device, context, pool, virtualPath)` dispatches on extension,
  resolves the GUID through the asset's `.hpmeta`, **writes that metafile when
  absent**, loads through the VFS and stores in the pool.
- A second import of the same file returns the **same GUID**, which is exactly
  what makes a scene's references reconnect across a reopen. Verified on device.
- A failed load still yields a valid GUID, so a scene referencing a broken asset
  resolves to a placeholder rather than silently detaching every entity.

What this ticket has to add is the *sweep*: walk the project's asset directories
through the VFS, import each, and do it in an order that does not stall the
editor on a large project. T0026's job system is the obvious lever and does not
exist yet either.


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

### From T0022 (closed 2026-08-05) — the document exists; the file does not

`saveSceneToString` / `loadSceneFromString` and `cookScene` /
`loadSceneFromCooked` are built and tested, and the format is written down in
[`../../documentation/10-scene-file-format.md`](../../documentation/10-scene-file-format.md).
T0022 deliberately stopped at string-in/string-out: **this ticket owns the
virtual path, the `.hpscene` extension, and reading and writing them through the
VFS.** Call that API rather than growing a second serializer, and put the cook
beside the YAML with `hashSource` of the YAML as its staleness key — the cook
layer already refuses a stale one, but only if something passes it the hash.
