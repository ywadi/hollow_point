# T0023 — AssetManager, asset pool and metafiles

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
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

### Architecture review (2026-08-03) — three things this ticket underspecifies

**1. Sub-asset identity.** A glTF file is not one asset — it is N meshes, M
materials and K textures in a container. "Each import writes a metafile with
source path and GUID" implies one GUID per file, but scenes need to reference
*the second mesh in* `characters.glb`. The metafile format must assign stable
GUIDs to **sub-assets** (stable across re-import, ideally keyed by name rather
than index so a re-exported file does not shuffle identities). Unity's
fileID/guid split exists for exactly this reason. Deciding this after scenes
reference assets means a migration.

**2. GPU-backed import needs the render device — a real Phase 3→4 ordering
wrinkle.** Diligent's `GLTFLoader`/`TextureLoader` create GPU resources and
take an `IRenderDevice`, which does not exist until T0025 (Phase 4). Either
accept that the GPU half of import only completes once T0025 lands, or split
import into CPU parse + deferred GPU upload from the start — the same
two-stage shape T0058's async loading needs anyway, so the split is not wasted
work. Do not let Phase 3 "complete" with an import path that was never run.

**3. Builtin default assets need well-known GUIDs.** The default material
(T0028), error material (T0060), and primitive meshes for greyboxing
(cube/sphere/plane — scene authoring needs them before any real asset is
imported) are engine-provided assets that exist in every project. They need
*fixed, reserved* GUIDs so scenes referencing them work across projects and
exports — a reserved GUID range is one line of policy now and a data migration
later.


### Architecture decision (2026-08-03) — all reads go through the VFS (D13)

**This ticket must not call `std::filesystem` or `fopen` on asset paths.**
Content is addressed through the virtual filesystem in T0103, which mounts loose
directories during development and archives when shipping. Both are mounts, so
dev and shipped builds run the same code path.

The ordering is the whole point: **T0103 lands before this ticket hardens.** If
the asset manager is written against the real filesystem first — which is the
obvious way to write it — then packs, patches and DLC each become a rewrite of
every read site rather than a mount. That is why T0103 was created.

The division of responsibility is deliberate and worth keeping clean: the VFS
owns *where bytes come from*, this ticket owns *what the bytes mean* — GUIDs,
metafiles, reference counting, and the lifetime rules in T0058. Resist letting
asset identity leak into path handling; a GUID must not be a path, and a mount
point must not be an asset concept.
