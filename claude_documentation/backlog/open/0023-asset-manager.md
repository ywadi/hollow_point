# T0023 — AssetManager, asset pool and metafiles

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Phase** | 3 — Data model |
| **Created** | 2026-08-02 |

## Why

Scenes reference meshes, materials and textures by GUID (T0021); something has to
resolve those GUIDs to actual loaded data. That is the asset manager, and it sits
above Diligent's existing loaders rather than replacing them —
`GLTFLoader`/`GLTFResourceManager` and `TextureLoader` already do the file
parsing.

Assets can live anywhere on disk, so each imported asset gets a **metafile** in
the project recording its source path and GUID. Without that, reopening a project
cannot reconnect a scene to its assets — and Phase 8 export cannot find what to
copy.

## Done when

- [ ] `ImportAsset` dispatches on extension to the right loader
- [ ] Imported assets land in an asset pool addressable by GUID
- [ ] Each import writes a metafile with source path and GUID
- [ ] Reopening a project reloads assets from metafiles and reconnects scenes
- [ ] Missing or moved source assets fail gracefully and visibly
- [ ] Tests for import, metafile round-trip and a deliberately missing asset

## Subtasks

- [ ] 23.1 Asset pool: GUID → asset, with per-type storage
- [ ] 23.2 `ImportAsset` extension dispatch; glTF and textures first
- [ ] 23.3 Delegate to Diligent's `GLTFLoader` and `TextureLoader` — do not
      reimplement parsing
- [ ] 23.4 Metafile format via T0020, alongside the asset in the project
- [ ] 23.5 Load-on-project-open from metafiles
- [ ] 23.6 Missing-asset handling: placeholder plus a visible error, never a crash
- [ ] 23.7 Tests

## Notes / findings

**Do not copy Laura's "one big shared buffer + version-checked whole-pool GPU
upload".** That is a path-tracer idiom — it uploads the entire scene as flat
buffers for a compute shader. We rasterise, so meshes own their own GPU buffers
and are drawn individually. Take the *pool and metafile* ideas, not the upload
strategy.

The pool is passed by pointer to systems that need it (renderer, editor panels)
so there is a single place assets live.

FBX is deliberately **not** an import path here — T0038 converts FBX to glTF
offline, and glTF is the engine's source of truth. Keeps this dispatch small.

Asset *hot reload* is not in scope but will be wanted; leave the pool's
GUID indirection able to swap an asset's contents without invalidating handles.
