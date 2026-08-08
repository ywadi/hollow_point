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

- [x] 168.1 **Build the table by reading**, not by rendering. `GLTFLoader.cpp`, `GLTFLoader.hpp`, `tiny_gltf.h`'s `#ifdef`s, `AssetImport.cpp`, `SceneRenderer.cpp`'s material mapping, and `hp::Material`. Cheap, and it is the whole point — the four gaps above cost hours to find one at a time and would have cost one afternoon together. **Done 2026-08-08: [`14-asset-import-matrix.md`](../../documentation/14-asset-import-matrix.md)** — and the reading corrected the ticket's own premise twice; see Notes
- [x] 168.2 **Close the 🔌 rows that are one mapping wide.** Spec-gloss is the known one: Diligent has the workflow and the shader, `hp::Material` has no fields and `SceneRenderer.cpp:329` no branch. **Judge it on merit** — Khronos archived spec-gloss in favour of metallic-roughness, and adopting a deprecated workflow is a real cost — but judge it, do not inherit the omission. **Done 2026-08-08, four rows: spec-gloss (judged, adopted for imports only — the judgement is in the matrix row), unlit, texture-transform, default scene. `importedMaterialFlags` + a runtime `Workflow` branch in `HpSurface.slang`; every closure asserted in `import_coverage_test.cpp` with values that fail on the prior code**
- [x] 168.3 **Draco.** `if (TARGET draco OR TARGET draco_static)` is opt-in and nobody opted in. It is the default compression on a Sketchfab download, so *"this engine cannot open my model"* is the failure it produces. Weigh the submodule and build-time cost against `03-build-harness.md`'s constraints — an added submodule is not free and the pin bump costs a cold CI build — and **record the decision either way**. **Done 2026-08-08: vendored and proven — the decision, its costs and the end-to-end measurement are in Notes**
- [x] 168.4 **The vertex attribute array.** Carried from T0166.4 if it was not landed there. `DefaultVertexAttributes` is a default; we pass null and inherit it. `TANGENT.w` is the known casualty; check whether anything else is. **Done 2026-08-08, as a decision: declined, recorded in the matrix with the reopening route.** The sweep found exactly two casualties: `TANGENT.w` (declined — widening needs an upstream patch at `PBR_Renderer.cpp:1660`, T0166 already measured that, and the engine reads the same fact from the UV determinant per fragment, where 168.5's frame fix needs it anyway) and `TEXCOORD_2+` (dropped, upstream has two UV sets — a matrix row, unowned). Everything else in the default array converts faithfully, including normalized u8/u16 attributes
- [x] 168.5 **The remainder of T0166**: the normal-map green channel (166.5), whether to vendor Khronos's `glTF-Sample-Assets` (166.6, ~2 GB — a build-harness call), and rendering `third_party/meshoptimizer/demo/pirate.glb` (166.7). **Done 2026-08-08, all three — 166.5 as code, 166.6 as a recorded decline, 166.7 as a measured negative; the substance is in Notes**
- [x] 168.6 **Warn on an unsupported required extension.** Neither `tinygltf` nor Diligent consults `extensionsRequired` — verified, zero hits in `AssetLoader/`. **Not spec-mandated**: the glTF spec places its `MUST`s on the asset, never on the client, and a hard refusal would reject assets that render acceptably. But the alternative is a material silently resolving to defaults, which is D35's class. A log line naming the extension is the cheap middle. **Done 2026-08-08: `AssetImport.cpp` reads the field through the load callbacks' `pSrcModel` (tinygltf declarations only — the implementation stays in Diligent's TU) against `kEndToEndExtensions`, once per import. Asserted both directions: a fake required extension warns by name, a supported one is silent**
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

### What building the table corrected (168.1, 2026-08-08)

The reading changed four things this ticket and T0167 believed:

1. **Spec-gloss dies one stage later than the ticket says.** `SceneRenderer.cpp:329`'s hardcode is the *authored*-`.hpmat` path (`toGltfMaterial`). An **imported** model is drawn with the loader's own `GLTF::Material`, so `Workflow = SPEC_GLOSS`, `SpecularFactor` and both textures (diffuse aliased into the base-colour slot, SG into phys-desc — `GLTFLoader.hpp:112-113`) all reach the constant buffer — and die in `HpSurface.slang`, which calls `GetSurfaceReflectanceMR` unconditionally and never reads `Workflow`. Upstream's `RenderPBR.psh:151-173` branches on the same buffer field at runtime — no permutation — and `GetSurfaceReflectance(Workflow, …)` already exists in `PBR_Shading.fxh`, which our shader already includes. So the close is a shader branch, not a loader change. **Prediction correction for T0167**: the car will render *textured* (diffuse lands in the right slot), with wrong roughness/metallic, not as a grey default.
2. **`KHR_materials_unlit` and `KHR_texture_transform` fail the same way** — loader-complete, attribs written, and `drawModel` never raises the permutation bit (`kPsoFlagUnshaded` / `ENABLE_TEXCOORD_TRANSFORM`) that only the authored path sets. Both are one condition wide. Upstream raises `ENABLE_TEXCOORD_TRANSFORM` unconditionally for model draws (`GLTF_PBR_Renderer.cpp:621`).
3. **`extensionsRequired` is reachable without touching vendored code.** Every `ModelCreateInfo` load callback (`NodeLoadCallback`, `MeshLoadCallback`, `MaterialLoadCallback`…) receives `pSrcModel`, the `tinygltf::Model*`, which carries the field tinygltf faithfully parses. `Model::Extensions` is `extensionsUsed` only — close, but the wrong field.
4. **Diligent's animation support is not loader-only — it is evaluate-capable.** `ComputeTransforms` takes `(AnimationIndex, Time)` and updates joints; the engine passes the default `-1`. T0041/T0049 start from a running mechanism, not from `Model::Animations` as raw data. Noted on their Refs by this ticket.

Smaller findings, all in the matrix: primitive `mode` is never read by the builder (POINTS/LINES render as triangle soup, upstream-absent); sparse accessors ignored (dense base read); multi-scene files draw `Scenes[0]` while `DefaultSceneId` sits loaded and unread — one line; `occlusionTexture.strength` never loaded (`OcclusionFactor` stays 1; the authored path *does* write it); sampler wrap modes are loaded into `Model::TextureSamplers` and dropped by the render contract — upstream's renderer has the same immutable-sampler shape, so that one is a real build, not a mapping; KTX2 (`KHR_texture_basisu`) is an explicit upstream throw (`KTXLoader.cpp:291`).

### 168.2/168.6 landed, and what the closures measured (2026-08-08)

The spec-gloss judgement, in full since the matrix row compresses it: **adopted, for imported models only.** For — both upstream halves exist and are maintained; the branch is runtime buffer data (upstream's own design), so the permutation space is untouched; Sketchfab still exports SG by the million and the first real asset this engine was handed requires it; the owner's rule is that import works. Against — Khronos archived the workflow; adopting it into the *authoring* surface would invite new assets onto a dead format. The line drawn: `.hpmat` keeps no workflow field (the `11-material-format.md` "absent on purpose" decision stands untouched), so the engine opens the corpus that exists without growing it. The channel hooks keep their MR meaning; an imported model can bind no custom module, so no game shader ever sees SG values through an MR-named hook.

Measured closures (`import_coverage_test.cpp`, RTX 2080, both targets):
- unlit under zero lamps: (186, 0, 0) against the lit twin's (0, 0, 0) — before, both were black
- SG `specularFactor` 0 vs 1: (2, 82, 196) vs (0.80, 0.80, 0.80) — before, `SpecularFactor` was unread and the files rendered identically
- `KHR_texture_transform` 0.5 offset: (255, 0) → (0, 255) — before, both red
- two-scene file with `"scene": 1`: green (the default scene), not scene 0's red

**Two more upstream fidelity gaps found while closing** (matrix updated): the loader never reads `glossinessFactor` and `RenderPBR.psh:163` hardcodes it to 1.0 — a factor-only SG material shades at roughness 0, per-pixel glossiness from the texture alpha works; and `occlusionTexture.strength` is never loaded (already in the matrix from 168.1).

gpu baseline: 67 → **68** (this file), assertions 1580 → 1621.

### 168.5: one construction, both fixes, and what each carried piece became (2026-08-08)

**166.5 — the green channel, fixed and pinned.** The engine's surface stage no longer calls DiligentFX's `PerturbNormal`: its `b = cross(t, n)` is a fixed chirality, which both inverted the green channel on every plain chart (its `b` points *down* a glTF image; the spec says +Y is up) and shaded a mirrored shell's `v` response backwards (T0166's 0 vs 130.2). The normal map now applies through **`HpTangentFrameGrad`** — the signed-determinant solve as an engine function, exported to the shader contract with `HpTangentFrame`, shared by parallax, normal mapping (base and clearcoat) and game modules — and `rock_pom.slang`'s copy of the construction became a call, closing the 13-matrix's duplication row. The artist answer is written in `11-material-format.md`: **author NormalGL**.

The lit case (`tangent_frame_test.cpp`) pins mirror *and* absolute direction, with every direction taken from a **point light at a plain world position** — N·L from first principles, no quaternion convention anywhere, which is where T0166's directional control went wrong. Measured, RTX 2080, both targets:

| map | above-lamp A / B | notes |
|---|---|---|
| green-up | **227.7 / 172.3** | both bright — both shells agree, toward the lamp |
| green-down | 0 / 0 | both dark; and **172.1 / 227.5** once the lamp moves below — the symmetric control that "map broke shading" cannot pass |
| +u, right-lamp | **195.0 / 0** | opposite — `u` is what the mirror mirrors |
| −u, right-lamp | 0 / **195.0** | opposite the other way |

Ripples through existing suites, all inside their bounds and each explained: the `channel_shadingnormal` debug view's green moved (186,**188**,252 → 186,**183**,252 — the fix's own signature), rockcube luminance variation 26.5 → 28.4 (its NormalGL map now applies un-inverted), the glass pane 1 LSB, and the cooked archive +173 KiB (the new helper's source in every cooked permutation).

**166.7 — `pirate.glb`, re-scoped by its own container.** It is **gltfpack 0.14 output**, not representative artist DCC export: `extensionsRequired` carries `KHR_mesh_quantization` **and** `EXT_meshopt_compression`, both upstream-absent at every layer. So it is not an answer to "does a real model come out the right way round" (that is `aston_martin.glb`, T0167's) — it is the ready-made case for the ⬆️ rows and the natural test of 168.6, and that is the test it got: tinygltf refuses it at parse (`byteLength 64704 > binary size 23036`) **before any load callback runs**, which is the measurement that forced 168.6's warning out of `NodeLoadCallback` and into a pre-parse of the raw JSON chunk. The committed case asserts both extensions are named in warnings; whether it loads is deliberately not asserted (it does not).

**166.6 — vendoring `glTF-Sample-Assets`: recommendation recorded, decision left to the owner** (a build-harness call, and the owner's asset boundary applies — nothing was fetched). Recommendation: **not the ~2 GB set**, and not the two named models either — `NormalTangentTest` and `NormalTangentMirrorTest` exercise the *vertex-tangent* path (`TANGENT` + `w`), which this engine deliberately does not use for normal mapping (168.4's declined row; the derivative frame reads the same fact per fragment), so they would test a path that does not exist here. The two-shell asset covers the question they ask, from first principles, with the mirror asserted on the emitted bytes. Worth revisiting only for a broad conformance sweep of *material* models (`KHR_materials_*` fixtures), which is a different, larger decision and T0167-shaped empirical work.

### 168.3: Draco vendored, and the decision's ledger (2026-08-08)

**Decided for, and done.** The weighing: *for* — rule 2 (a Sketchfab-default compression the engine cannot open is "this engine cannot open my model"), the loader wiring already complete upstream behind one CMake target, and the vendoring pattern already established in this repo for exactly this shape (`FETCHCONTENT_SOURCE_DIR_ENTT`/`ABSEIL-CPP` — draco is the third name on that list, at exactly the 1.5.7 ref Diligent's own FetchContent declares, so no version judgement was even needed). *Against* — one more submodule (~40 MB checkout), ~1–2 min of build per cold tree, and the pin-bump cold CI build (16–18 min, once; the documented intended trade for the coarse cache key). `DRACO_TESTS`/`DRACO_TOOLS` off, decoder-and-encoder library only.

**Measured**: both targets build clean; `libhp_engine.so` carries the decoder (148 `draco::Decoder*` symbols where T0166 measured zero); offline configure holds (no `_deps/*-src`, the same assertion CI makes); and the end-to-end case in `import_coverage_test.cpp` **synthesises** its asset with draco's own encoder — no binary fixture — decodes it through the engine's import (82 bytes → 4 points / 2 faces), renders it full-frame at (186, 0, 0), and asserts the required-extension warning stays silent now that `KHR_draco_mesh_compression` sits in `kEndToEndExtensions`. The encoder links into the test buckets only (`HP_TESTS_HAVE_DRACO`), not into anything that ships.

### The measurement that started it, for anyone checking the claims

`aston_martin.glb` (T0167's asset, Sketchfab, CC-BY-4.0): `KHR_materials_pbrSpecularGlossiness` in **`extensionsRequired`**; `TANGENT` present on all 31 primitives with **`w = −1` on 1,316 vertices**; 594 of 642,553 triangles with negative UV signed area. Full numbers in T0167's Notes.
