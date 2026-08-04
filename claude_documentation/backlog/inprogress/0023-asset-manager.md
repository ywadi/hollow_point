# T0023 — AssetManager, asset pool and metafiles

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 3 — Data model |
| **Order** | 220 |
| **Created** | 2026-08-02 |
| **Refs** | T0103 (Blocks this — D13), T0039, T0096, T0116 |

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
- [~] Imported assets land in an asset pool addressable by GUID
- [~] Each import writes a metafile with source path and GUID
- [ ] Reopening a project reloads assets from metafiles and reconnects scenes
- [~] Missing or moved source assets fail gracefully and visibly
- [~] Tests for import, metafile round-trip and a deliberately missing asset

## Subtasks

- [x] 23.1 Asset pool: GUID → asset, with per-type storage
- [ ] 23.2 `ImportAsset` extension dispatch; glTF and textures first
- [ ] 23.3 Delegate to Diligent's `GLTFLoader` and `TextureLoader` — do not
      reimplement parsing
- [x] 23.4 Metafile format via T0020, alongside the asset in the project
- [ ] 23.5 Load-on-project-open from metafiles
- [~] 23.6 Missing-asset handling: placeholder plus a visible error, never a crash
- [~] 23.7 Tests

## Notes / findings

## Progress — 2026-08-05

**The identity and storage layers are built; import is not.** `hp/Assets.hpp`
covers 23.1 and 23.4. Nothing yet reads a glTF or a texture, because that is
23.2/23.3 delegating to Diligent's loaders, which needs a device.

### 23.1 — the pool is keyed by GUID *and* type name

**Not `entt::type_index`, and this is a T0095 consequence rather than a
preference.** The pool is reached by gameplay modules, and T0095 established
that entt's type index is not stable across the module boundary. If it were used
here, a module and the engine could disagree silently and a lookup would return
nothing for an asset that is definitely loaded — with no diagnostic at all. So a
stored type declares a stable name through `AssetTraits<T>::name`, exactly as
reflected components do, and a type with no specialisation fails to compile
rather than falling back to something unstable.

Keying on (GUID, type) also means two assets sharing a GUID across types cannot
silently return the wrong object. That is not a situation worth designing *for*,
but it is one that must not corrupt a lookup if it happens.

**Ownership is shared**, so gameplay holding an asset across a scene load keeps
it alive, and a pool teardown cannot pull it out from under a caller. Storing
again *replaces* rather than refusing, because that is exactly what a hot reload
is (T0058): same identity, new data, and callers holding the old pointer keep
the old object until they drop it. A test covers that specifically.

### 23.4 — metafiles, and why they keep the GUID

`foo.gltf` is described by `foo.gltf.hpmeta`. **Appended, not substituted**: a
substituted extension would give `mesh.gltf` and `mesh.png` one metafile and
therefore one GUID — two assets with a single identity.

Two decisions that both come down to "never orphan a scene reference":

- **A metafile that disagrees about the type keeps its GUID** and has the type
  corrected. Minting a new GUID would break every scene reference to the asset,
  which is much worse than a wrong type field.
- **A corrupt, absent or future-versioned metafile all mean the same thing**:
  reimport. None of them is fatal, and none produces a half-populated struct —
  which would hand the asset a default GUID and silently break everything
  pointing at it.

The source path is **virtual, not a host path**. A host path bakes one machine's
layout into a project file and breaks for every other person on the team and for
every shipped build.

### Evidence

`tests/integration/assets_test.cpp`, 16 cases. Integration suite 89/89 (515
assertions) on Linux; full both-target run recorded on the commit.

## Not done

- **23.2 and 23.3 — import — are not started.** Extension dispatch and
  delegation to Diligent's `GLTFLoader` and `TextureLoader` need a device, so
  they want a gpu-bucket test alongside them.
- **23.5 load-on-project-open** needs T0024's ProjectManager, which does not
  exist. The pieces it will use — `loadOrCreateAssetMeta` and the pool — do.
- **23.6 is half-done.** A missing or unreadable *metafile* is handled. A missing
  or moved *source asset* is not: there is no placeholder asset and no visible
  error, because there is nothing loading source assets yet.
- **Nothing writes metafiles automatically.** `writeAssetMeta` exists and the
  tests write through the VFS, but no import path calls it.


**T0103 landed the VFS, and this ticket is where its central rule is kept or
quietly broken.** Every asset read goes through `hp::Vfs` — no `std::filesystem`,
no `fopen`, no absolute paths. That is checkable by grep and it is the whole
reason T0103 had to land first: an `AssetManager` written against
`std::filesystem` makes packs, patches and DLC each a rewrite of every read
site instead of a mount.

Concretely:

- `hp::Vfs::read(path)` returns bytes; `readText` returns a string. Paths are
  `/`-separated and relative to the mount tree. An absolute path does not
  resolve, by design.
- **An empty file returns an empty vector, not `nullopt`.** "Not there" and
  "there and says nothing" are different answers and this ticket will care.
- `hp::Vfs::resolvedSource(path)` reports which mount a file actually came from.
  When a patch pack does not take effect, that is the only useful diagnostic.
- The mount policy T0103.3 wrote down is **not wired to anything**. Applying it
  — write directory, then editor loose dirs, then patches, then DLC, then base
  packs — belongs here.


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

### Cross-ticket obligations (2026-08-04, T0124 backfill)

- **T0039.5** decides the cooked-mesh container at the start of Phase 7, and
  its note says that decision "shapes T0023's pool". Keep 23.1's per-type
  storage extensible for a cooked container arriving later — do not bake in
  "an asset is a parsed glTF".
- **T0096.3**'s sRGB policy flows through texture loading: the loader calls in
  23.2/23.3 must carry the per-asset sRGB flag through to the view format
  rather than guessing per call site — a wrong flag silently breaks PBR
  shading in exactly the way T0096 exists to prevent.
- **T0116** builds on this ticket's reserved-GUID builtin primitives (one
  mechanism, not two), and its 116.5 decision — CSG output as source, cooked,
  or load-time-evaluated — may land a change on this import model. Neither
  ticket should discover the other mid-build.
