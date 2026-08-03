# T0036 — Assets panel

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 6 — Editor |
| **Order** | 660 |
| **Created** | 2026-08-02 |

## Why

Without this there is no way to import an asset or assign a mesh to an entity, so
the inspector has nothing meaningful to show. It is the UI over the AssetManager
built in T0023.

## Done when

- [ ] A browsable list of project assets, grouped or filterable by type
- [ ] Selecting an asset shows its details — GUID, source path, type, stats
- [ ] Assets can be imported from the panel
- [ ] A mesh or material can be assigned to the selected entity
- [ ] Missing source assets are visibly flagged, not silently absent

## Subtasks

- [ ] 36.1 Two-pane layout: asset list, details for the selection
- [ ] 36.2 Asset tiles with type indication
- [ ] 36.3 Import button wrapping `AssetManager::ImportAsset`
- [ ] 36.4 Details pane, including the metafile's recorded source path
- [ ] 36.5 Assignment into the selected entity's components
- [ ] 36.6 Flag missing/moved assets clearly

## Notes / findings

Drag-and-drop from this panel into the inspector is the natural interaction and
ImGui supports it well, but do the button-based path first — drag-and-drop is
fiddly and not required for the panel to be useful.

Thumbnail previews for textures and meshes are a genuine quality-of-life win and
a genuine time sink (mesh thumbnails need offscreen renders). Explicitly out of
scope here; revisit once the render stack can render to arbitrary targets --
that capability is **T0120** (2026-08-03), filed because this line named it
without a ticket to revisit into.

This panel is where the FBX→glTF converter (T0038) will eventually surface, so
leave room in the import flow for a conversion step rather than assuming import
is always a direct load.
