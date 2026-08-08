# T0168 — Asset import coverage: what does DiligentEngine already do, and are we asking it to?

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 461 |
| **Created** | 2026-08-08 |
| **Blocked by** | nothing |
| **Refs** | [../completed/0166-tangent-frames-and-real-assets.md](../completed/0166-tangent-frames-and-real-assets.md) — **closed early into this ticket**; it shrank once the check below was applied, and carries 166.5, 166.6 and 166.7 here; [../open/0167-sketchfab-asset-validation.md](../open/0167-sketchfab-asset-validation.md) — the empirical half; this ticket is the paper half and they answer the same question from opposite ends; [../open/0038-fbx-to-gltf-converter.md](../open/0038-fbx-to-gltf-converter.md) — a converter's output is an import, and every row here constrains it; [../../documentation/12-vendored-capabilities.md](../../documentation/12-vendored-capabilities.md) — **the document whose discipline this extends**; [../../documentation/13-shader-capability-matrix.md](../../documentation/13-shader-capability-matrix.md) — the model for the table; [../../documentation/11-material-format.md](../../documentation/11-material-format.md), [../../documentation/05-verification-status.md](../../documentation/05-verification-status.md), [../../documentation/07-design-gaps.md](../../documentation/07-design-gaps.md) — the three documents that between them say almost nothing about import; **D13**, **D35** |

## Why

**The owner's requirement is that asset import works, or the engine does not get used**, and the evidence of one evening is that we do not know what it supports. Three gaps were found in a few hours — none by a test, all by someone reading — and **every one of them was a case where DiligentEngine already had the answer and this engine was not asking for it**:

| Found | Diligent | HollowPoint | Whose gap |
|---|---|---|---|
| `KHR_materials_pbrSpecularGlossiness` | loader ✅ (`GLTFLoader.cpp:1510`) **and** shader ✅ (`RenderPBR.psh:159`) | hardcodes `PBR_WORKFLOW_METALL_ROUGH` (`SceneRenderer.cpp:329`) | **ours** |
| glTF `TANGENT.w` handedness | **parameterised** — `ModelCreateInfo::VertexAttributes`, *"if null is provided, default vertex attributes will be used"* (`GLTFLoader.hpp:850-857`) | passes null (`AssetImport.cpp:365`), inherits `VT_FLOAT32, 3` | **ours** |
| Tangent frame determinant sign | correct — divides by the **signed** `d` (`ShaderUtilities.fxh:51-55`) | hand-rolled Schüler with a positive `invMax` | **ours**, and a defect rather than an absence |
| `KHR_draco_mesh_compression` | full support, gated `if (TARGET draco OR TARGET draco_static)` | never supplies the target — the built `libDiligent-AssetLoader.a` carries **zero** draco symbols, and there is no draco submodule | **ours (build)** |

**`CLAUDE.md` opens with the check that would have caught all four** — *"Does DiligentEngine already do this? Check before building anything in the render layer. The check has failed twice here."* It is now four. `12-vendored-capabilities.md` exists for the same reason. **The discipline exists and was applied to the render layer, never to the loader.**

**The same asymmetry appears twice.** **D35** requires a technique to get a capability-matrix row *before* it is built — applied to shaders, where T0143, T0160 and T0161 all landed with their gaps known in advance, and never applied to import, where we still find out one asset at a time. Two rules, both stopping at the same boundary.

And nothing answers the question today. `05-verification-status.md` says exactly one thing about glTF — *"A glTF mesh loads through the VFS and rasterises"*. `07-design-gaps.md` mentions assets only as editor operations and memory budgets. There is no third place.

## The shape this must take

**A table, not prose**, modelled on `13-shader-capability-matrix.md`, with one row per glTF feature and extension and — the column that makes it this ticket rather than a wish-list — **who owns the gap**:

| State | Meaning | What it costs |
|---|---|---|
| ✅ **supported** | reaches `hp::` and renders | nothing |
| 🔌 **loader-only** | Diligent parses it, we drop it on the floor | usually a mapping, sometimes one line |
| 🔧 **switch off** | Diligent implements it behind a flag or a target we do not provide | a build change |
| ⬆️ **upstream absent** | Diligent does not implement it | ours to build, vendor, or decline |
| 🚫 **declined** | decided against, with the reason | nothing, but it must be *written down* |

**On tonight's evidence most rows will be 🔌 or 🔧, and that is the finding**, not a preamble to one.

## Done when

- [ ] Every glTF 2.0 feature and every ratified `KHR_`/`EXT_` extension has a row, a state, and — where the state is not ✅ — **who owns it and what it would cost**
- [ ] A person can answer *"will this model import?"* from one document, without reading the loader
- [ ] Every 🔌 and 🔧 row has either been **closed** or has a named ticket
- [ ] **Draco loads**, or its rejection is recorded with the reason
- [ ] `CLAUDE.md` points at the matrix in the same table that points at the shader one, and **D35's rule is extended to import in words** — a format feature gets a row before someone finds out by rendering

## Subtasks

- [ ] 168.1 **Build the table by reading**, not by rendering. `GLTFLoader.cpp`, `GLTFLoader.hpp`, `tiny_gltf.h`'s `#ifdef`s, `AssetImport.cpp`, `SceneRenderer.cpp`'s material mapping, and `hp::Material`. Cheap, and it is the whole point — the four gaps above cost hours to find one at a time and would have cost one afternoon together
- [ ] 168.2 **Close the 🔌 rows that are one mapping wide.** Spec-gloss is the known one: Diligent has the workflow and the shader, `hp::Material` has no fields and `SceneRenderer.cpp:329` no branch. **Judge it on merit** — Khronos archived spec-gloss in favour of metallic-roughness, and adopting a deprecated workflow is a real cost — but judge it, do not inherit the omission
- [ ] 168.3 **Draco.** `if (TARGET draco OR TARGET draco_static)` is opt-in and nobody opted in. It is the default compression on a Sketchfab download, so *"this engine cannot open my model"* is the failure it produces. Weigh the submodule and build-time cost against `03-build-harness.md`'s constraints — an added submodule is not free and the pin bump costs a cold CI build — and **record the decision either way**
- [ ] 168.4 **The vertex attribute array.** Carried from T0166.4 if it was not landed there. `DefaultVertexAttributes` is a default; we pass null and inherit it. `TANGENT.w` is the known casualty; check whether anything else is
- [ ] 168.5 **The remainder of T0166**: the normal-map green channel (166.5), whether to vendor Khronos's `glTF-Sample-Assets` (166.6, ~2 GB — a build-harness call), and rendering `third_party/meshoptimizer/demo/pirate.glb` (166.7)
- [ ] 168.6 **Warn on an unsupported required extension.** Neither `tinygltf` nor Diligent consults `extensionsRequired` — verified, zero hits in `AssetLoader/`. **Not spec-mandated**: the glTF spec places its `MUST`s on the asset, never on the client, and a hard refusal would reject assets that render acceptably. But the alternative is a material silently resolving to defaults, which is D35's class. A log line naming the extension is the cheap middle
- [ ] 168.7 **Wire it into the rules.** A row in `CLAUDE.md`'s table, and D35 extended to import — **a format feature gets a row before it is relied on**, the same sentence that already works for shader techniques
- [ ] 168.8 **Feed T0167 and be fed by it.** This ticket predicts from reading; T0167 measures one real asset. **Where they disagree, the asset wins and the table is wrong** — record which rows it corrected, because that is the measure of whether reading is enough

## Not in scope

- **Building any format support Diligent lacks.** ⬆️ rows get a state and a cost, not an implementation. `EXT_meshopt_compression`, `KHR_texture_basisu`, `KHR_mesh_quantization` and `KHR_materials_specular` all have zero hits in the loader; each is its own decision.
- **FBX, OBJ, USD.** T0038 owns conversion. This ticket is about what the engine's *one* mesh format actually supports (D13), which is what a converter must target.
- **A texture/image pipeline.** Colour spaces and compressed formats touch T0097 and T0028 and are named in rows, not solved here.

## Notes / findings

### Why this is not "write more documentation"

The four gaps in the table above were each found by a person reading, hours apart, while looking for something else. **Not one was found by a test, and not one would have been found by rendering** — the tangent defect needs a mirrored UV shell, spec-gloss produces a plausible-looking default surface, and Draco fails at load with a message nobody had read. A table built from the loader in an afternoon would have listed all four before any of them cost anything.

That is the argument for doing this by reading and doing it first. It is the same argument `13-shader-capability-matrix.md` won on, and it was written **after** a day of silent debugging rather than before — which is exactly what D35 exists to stop happening again.

### The measurement that started it, for anyone checking the claims

`aston_martin.glb` (T0167's asset, Sketchfab, CC-BY-4.0): `KHR_materials_pbrSpecularGlossiness` in **`extensionsRequired`**; `TANGENT` present on all 31 primitives with **`w = −1` on 1,316 vertices**; 594 of 642,553 triangles with negative UV signed area. Full numbers in T0167's Notes.
