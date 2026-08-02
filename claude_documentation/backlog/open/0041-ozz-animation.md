# T0041 — ozz-animation runtime and import

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 7 — Content pipeline |
| **Created** | 2026-08-02 |

## Why

> **Skeletal animation is core to the games this engine is for** (stated
> 2026-08-03). This is not an optional subsystem — it is a primary feature, and
> the phase plan is ordered accordingly.


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

- [x] 41.1 Importer problem RESOLVED — ozz offline builders, no importer needed (notes)
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

**RESOLVED (2026-08-03) — option 2.** Inspecting `ozz-animation/include/ozz/animation/offline/`
confirms it ships `raw_skeleton.h`, `raw_animation.h`, `skeleton_builder.h`,
`animation_builder.h`, `animation_optimizer.h` and `additive_animation_builder.h`.
We can therefore build ozz runtime data **directly from our own glTF import**,
with no ozz importer and no FBX SDK. `ozz_build_gltf` and `ozz_build_tools` stay
OFF, and D8 needs no revision.

That also keeps one import path rather than two: FBX → glTF (T0038) → our
importer → ozz runtime structures.

Original options, kept for the record:
1. Re-enable `ozz_build_tools` + `ozz_build_gltf` for **host builds only**, and
   run ozz's own glTF converter as an offline pipeline step. Fits the existing
   host-tool pattern (T0038) and uses ozz's supported path.
2. Convert from our own glTF import into ozz's runtime structures directly,
   using `ozz::animation::offline::RawSkeleton`/`RawAnimation` builders — no
   importer needed at all, more code but fewer moving parts.
3. Commit pre-converted `.ozz` files. Simplest, but makes source assets and
   runtime assets diverge, which is exactly what the metafile system exists to
   avoid.

Superseded by the resolution above. The FBX SDK is never required either way.
