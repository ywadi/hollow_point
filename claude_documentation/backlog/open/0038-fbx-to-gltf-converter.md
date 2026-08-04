# T0038 — FBX → glTF converter (host tool)

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 7 — Content pipeline |
| **Order** | 700 |
| **Created** | 2026-08-02 |
| **Supersedes** | T0009 |

## Why

glTF is the engine's source of truth — Diligent's `GLTFLoader`,
`GLTFResourceManager` and `TinyGltfModelView` already handle it, and
reimplementing mesh/material import for a second format would be waste.

But art pipelines produce FBX, and round-tripping every asset through Blender to
re-export as glTF is exactly the friction worth removing. So: an offline tool
that reads FBX with **ufbx** and writes glTF with **tinygltf**, both already in
the tree.

## Done when

- [ ] `hp-convert model.fbx -o model.gltf` produces a glTF that Diligent loads
- [ ] Meshes, materials, textures and node hierarchy survive the conversion
- [ ] Skinned meshes and skeletons convert (needed by T0041)
- [ ] The tool builds for the **host only** — it never ships in the game
- [ ] Conversion of a real FBX is verified by loading the result and rendering it

## Subtasks

- [ ] 38.1 Host-only tool target — see notes, this keeps ufbx out of the game
- [ ] 38.2 Wire ufbx as a static library from `cmake/ufbx.cmake` (ufbx ships no
      CMakeLists; it is a single `ufbx.c` + `ufbx.h`, and must NOT be modified
      in-place since it is a submodule)
- [ ] 38.3 Map ufbx scene → tinygltf model: meshes, materials, textures, nodes
- [ ] 38.4 Handle coordinate system and unit differences — FBX is commonly
      centimetres and Z-up, glTF is metres and Y-up
- [ ] 38.5 Skinning: joints, weights, inverse bind matrices
- [ ] 38.6 Write `.gltf` and `.glb`
- [ ] 38.7 Verify by loading the output through Diligent and rendering it

## Notes / findings

**Host-only matters.** Like Diligent's `RenderStatePackager`, this runs on the
developer's machine, never on the target. Keeping it host-only means ufbx,
tinygltf's writer and meshoptimizer never enter the shipped binary.

**Axis and unit conversion is where these tools usually go wrong**, and the
failure is subtle: models load but are 100× too large, or lying on their side.
Test with an asset that is visibly asymmetric in all three axes so mistakes are
obvious rather than plausible.

tinygltf's write support is confirmed present in the bundled copy
(`WriteGltfSceneToFile`, `SerializeGltfModel`).

T0039 hooks LOD generation into this same tool, so structure it as a pipeline of
stages rather than one monolithic convert function.

### Note (2026-08-03) -- morph targets are deliberately not in the subtask list

The subtasks convert meshes, materials, skinning and (via T0041) animations.
**Morph targets / blend shapes are not listed, and that is currently an
absence, not a rejection** (design-gap survey item 14): this converter does
not carry them, ozz does not handle them (they are vertex animation, not
joint animation), and the skinned vertex layout being assigned in T0041's
review note does not include morph deltas. Morph-target support touches this
importer, the vertex format and the animation runtime (T0049) *at once*, which
is exactly what makes it worth deciding deliberately: a three-place retrofit if
added late, a small addition if planned. **This ticket and T0049 own that
decision**, and they own it on that cost -- an engine either offers shape keys
or it does not, and a game cannot add them from gameplay code if the vertex
format has no room. Decide it at the start of this ticket and record the answer;
if it is yes, scope it as its own ticket rather than growing it here quietly.
