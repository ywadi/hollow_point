# T0041 — ozz-animation runtime and import

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Phase** | 7 — Content pipeline |
| **Created** | 2026-08-02 |

## Why

ozz-animation is vendored and builds for both targets, but nothing uses it and —
more importantly — there is currently **no way to produce the `.ozz` runtime files
it consumes**, because its importers are disabled (D8). Skeletal animation needs
both halves.

## Done when

- [ ] Skeleton and animation data can be produced from source assets
- [ ] The runtime samples an animation and produces skinning matrices
- [ ] A skinned mesh animates on screen
- [ ] Sampling is off the main thread or cheap enough to justify not being
- [ ] Blending between two animations works

## Subtasks

- [ ] 41.1 Resolve the importer problem — see notes, this is the real decision
- [ ] 41.2 Skeleton + animation as engine assets with GUIDs (T0023)
- [ ] 41.3 Runtime sampling via `ozz::animation::SamplingJob`
- [ ] 41.4 Local-to-model transforms and skinning matrices to the renderer
- [ ] 41.5 An animation component on entities
- [ ] 41.6 Blending two clips
- [ ] 41.7 Parallelise sampling across instances via the job system (T0026)

## Notes / findings

**The importer problem, carried over from T0005.** `ozz_build_tools`,
`ozz_build_fbx` and `ozz_build_gltf` are all OFF, disabled together because the
samples need GLFW/OpenGL and the FBX pipeline needs the proprietary FBX SDK. But
`ozz_build_gltf` does **not** need the FBX SDK — that cut was too broad.

Three options, in rough order of preference:
1. Re-enable `ozz_build_tools` + `ozz_build_gltf` for **host builds only**, and
   run ozz's own glTF converter as an offline pipeline step. Fits the existing
   host-tool pattern (T0038) and uses ozz's supported path.
2. Convert from our own glTF import into ozz's runtime structures directly,
   using `ozz::animation::offline::RawSkeleton`/`RawAnimation` builders — no
   importer needed at all, more code but fewer moving parts.
3. Commit pre-converted `.ozz` files. Simplest, but makes source assets and
   runtime assets diverge, which is exactly what the metafile system exists to
   avoid.

Option 1 or 2. Decide, record it in the decision log, and note that either way
the FBX SDK is never required — T0038 already gets FBX in via ufbx.
