# Will this model import? The asset-import capability matrix

**This document answers one question from one place: what happens to each
feature a glTF file can carry, and — where the answer is "it is lost" — whose
gap that is and what closing it costs.**

It was written on 2026-08-08 (T0168) after four gaps were found in one evening,
none by a test, all by someone reading — and **every one was a case where
DiligentEngine already had the answer and this engine was not asking for it**:
spec-gloss (their loader *and* their shader; our hardcoded workflow), `TANGENT.w`
(a default we inherited by passing null), the tangent-frame determinant (correct
upstream, hand-rolled wrong here), and Draco (opt-in behind a CMake target
nobody supplied). `12-vendored-capabilities.md` carries that discipline for the
render layer; `13-shader-capability-matrix.md` carries it for shader techniques;
this is the same rule applied to the loader, which is the boundary both of them
stopped at.

**D35 extends to import** (see the decision log): a format feature gets a row
here *before* anything relies on it — before a converter targets it (T0038),
before an import pipeline maps it (T0169), before a test asset assumes it.
An empty cell found in advance is a plan; the same cell found by rendering is a
day of silent debugging, and both of those sentences have been measured here.

---

## How to use it

- **"Will this model import?"** — look up what the file carries, row by row.
  Anything not ✅ says exactly what will be wrong and why.
- **Before building format support**, read the row's Diligent column first.
  The check is not only "does Diligent do this" but "does Diligent already do
  this *better than the thing we are about to write*" (T0166's lesson).
- **When a capability lands or a decision is taken**, flip the row and date it.
  The document's value is entirely in being maintained.

Legend — the state names who owns the gap:

| State | Meaning | What it costs |
|---|---|---|
| ✅ **supported** | reaches `hp::` and renders | nothing |
| 🔌 **loader-only** | Diligent parses it, we drop it on the floor | usually a mapping, sometimes one line |
| 🔧 **switch off** | Diligent implements it behind a flag or a target we do not provide | a build change |
| ⬆️ **upstream absent** | Diligent does not implement it | ours to build, vendor, or decline |
| 🚫 **declined** | decided against, with the reason | nothing, but it is *written down* |

## The pipeline, so the states name a seam

```
file → tinygltf (parse) → Diligent GLTF::Model (convert + GPU buffers)
     → hp::MeshAsset (AssetImport.cpp) → SceneRenderer::drawModel (flags, bind, draw)
     → HpSurface.slang (shade)
```

A feature can die at any arrow. tinygltf parses almost everything (unknown
extensions survive as raw JSON maps); Diligent's `ModelBuilder` consumes what it
knows and ignores the rest; our draw path raises shader permutation flags from
what the model carries; the shader reads what its permutation compiled in.
"Loader-only" almost always means the third or fourth arrow.

---

## Core glTF 2.0

| Feature | State | Where it lives / dies | Cost to close · owner |
|---|---|---|---|
| `.gltf` + external `.bin`/images | ✅ | every file read through the VFS callbacks (`AssetImport.cpp`, T0103) — packs and loose files behave identically | — |
| `.glb` binary container | ✅ | tinygltf; the Aston Martin's chunks parse (T0167 notes) | — |
| data-URI buffers and images | ✅ | tinygltf decodes base64 URIs unconditionally | — |
| `POSITION` | ✅ | buffer 0, `VT_FLOAT32×3` | — |
| `NORMAL` | ✅ | keyed per model: `PSO_FLAG_USE_VERTEX_NORMALS` (`drawModel`) | — |
| `TANGENT` xyz | ✅ | buffer 4, `float3`. **Normal mapping does not use it** — the shader builds its frame from screen-space derivatives; the vertex tangent feeds only the anisotropy basis | — |
| `TANGENT.w` (handedness) | 🚫 | **declined 2026-08-08 (T0168.4), recorded here and in the shader contract.** The default attribute array truncates to `float3` and widening resists upstream: `PBR_Renderer.cpp:1660` generates `VSInput` from a hardcoded `float3 Tangent` with a `DEV_CHECK_ERR` against the layout (the *colour* attribute, three lines up, shows the escape — its count is read from the layout — so this is an upstream patch, not a setting). The engine does not need it: the signed determinant of `(ddx(uv), ddy(uv))` announces a mirrored chart per fragment, and the corrected frame (T0166.2, T0168.5) reads it there. `w` is a per-vertex copy of the same fact. **Reopen if** per-vertex handedness is ever needed where derivatives are unavailable (compute, ray hits) — the route is an upstream PR mirroring the colour attribute's pattern | reopening: upstream PR · nobody |
| `TEXCOORD_0` | ✅ | buffer 0; `PSO_FLAG_USE_TEXCOORD0` | — |
| `TEXCOORD_1` | ✅ | buffer 2; per-texture UV-set selection flows (`SetUVSelector`) | — |
| `TEXCOORD_2+` | ⬆️ | not in the attribute array and the PBR shader has two UV sets; extra sets silently dropped | rare in practice; a row before anyone relies on it · unowned |
| `COLOR_0` | ✅ | buffer 3, `float4`, vec3 alpha-filled from the default, u8/u16 normalized converted | — |
| `JOINTS_0` / `WEIGHTS_0` | 🔌 | loaded into buffer 1 and `Model::Skins` is complete (inverse bind matrices, joints); upstream can evaluate the whole chain (`ComputeTransforms` + `PSO_FLAG_USE_JOINTS`). `drawModel` never raises the flag and never binds joint matrices, so a skinned mesh renders its **bind pose** — which is the correct degradation, not a crash | animation runtime · **T0049** (skinning named in its Refs) |
| quantized attribute types (u8/u16/s8/s16, `normalized`) | ✅ | `GLTFVertexDataConverter` converts any accessor component type to the destination, honouring `normalized` — see also `KHR_mesh_quantization` below | — |
| indices u8/u16/u32 | ✅ | converted to `VT_UINT32` | — |
| non-indexed primitives | ✅ | `drawModel` issues a plain draw | — |
| primitive `mode` ≠ TRIANGLES | ⬆️ | **the loader never reads `mode`** (`TinyGltfPrimitiveView` has no accessor for it) — POINTS/LINES/STRIP/FAN data is drawn *as a triangle list*, silently wrong geometry | pre-triangulate on import, or refuse with a log line; a row + decision before the first lines asset · unowned |
| sparse accessors | ⬆️ | tinygltf parses `sparse`; the builder reads only the base bufferView — sparse displacements silently ignored (or garbage on a bufferView-less accessor) | uncommon outside morph pipelines · unowned |
| morph targets / weights | ⬆️ | `Primitive.targets` never read | named gap in `07-design-gaps.md`; T0169 gives it a mapping row | 
| animations | 🔌 | fully loaded (`Model::Animations`) and upstream *evaluates* them — `ComputeTransforms(scene, transforms, root, AnimationIndex, Time)` — the engine always passes the default `-1` | runtime is **T0041/T0049**; the loader half is already done |
| node hierarchy / TRS / matrix | ✅ | `ComputeTransforms`; negative-determinant nodes get the glTF facing rule (D33, T0152.5) | — |
| multiple scenes | ✅ | **closed 2026-08-08 (T0168.2)**: `drawModel` draws `Model::DefaultSceneId`; a two-scene file with a nominated default renders it, asserted in `import_coverage_test.cpp` | — |
| cameras | 🔌 | loaded into `Model::Cameras` (perspective + ortho, all attribs); nothing reads them | `hp::Camera` exists — mapping decision is **T0169**'s table |
| material: factors, alphaMode/cutoff, doubleSided | ✅ | loader → attribs buffer → shader; MASK is a compile-time cutout, BLEND routes to the blend pass (T0147) | — |
| `normalTexture.scale` | ✅ | `NormalScale`, multiplied in `GetMicroNormal` | — |
| `occlusionTexture.strength` | ⬆️ | the loader never reads it — `OcclusionFactor` stays 1.0 (our authored `.hpmat` path *does* write it) | one `MaterialLoadCallback` line · unowned, minor |
| texture assignment + per-texture UV set | ✅ | `UVSelector` per slot | — |
| sampler wrap/filter modes | ⬆️ | the loader builds `Model::TextureSamplers` faithfully, but the *render* contract — upstream's own as well as ours — is one immutable sampler per slot (linear/wrap). CLAMP or MIRROR assets tile wrongly at UV borders | per-material sampler variables; upstream's renderer has the same shape, so this is a real build · unowned |
| images: PNG, JPEG | ✅ | decoded by Diligent's TextureLoader (tinygltf's stb decode is compiled out) | — |
| images: TGA/TIFF/HDR/SGI/DDS/KTX1 payloads | ✅ | same loader, if an asset ever carries them | — |
| sRGB → linear | ✅ | in-shader `SRGB_TO_LINEAR` on the colour slots, the mode DiligentFX documents for UNORM views over sRGB data (T0141.12) | — |
| `extensionsRequired` honoured | ✅ | **closed 2026-08-08 (T0168.6)**: the import warns, once per model, naming each required extension outside `AssetImport.cpp`'s `kEndToEndExtensions` list — reached through the load callbacks' `pSrcModel`, since `Model::Extensions` mirrors the wrong field. Advice, not refusal (T0167.9b's argument): the model still loads and renders. Asserted both ways in `import_coverage_test.cpp` — a fake required extension warns by name, a supported one stays silent | keep `kEndToEndExtensions` in step with this table |

## Extensions

Khronos-ratified `KHR_` and the multi-vendor `EXT_` set. "tinygltf" means the
parser interprets it (raw extension JSON survives regardless); "Diligent" means
`GLTFLoader.cpp` consumes it into the model.

| Extension | tinygltf | Diligent | HollowPoint | State · owner |
|---|---|---|---|---|
| `KHR_materials_pbrSpecularGlossiness` | raw | ✅ full — workflow, factors, diffuse→base-colour slot, SG→phys-desc slot (`GLTFLoader.cpp:1510`, slot aliases `GLTFLoader.hpp:112-113`) | ✅ **closed 2026-08-08 (T0168.2), judged on merit and adopted for imported models only**: `HpSurface.slang` branches on `Workflow` at runtime — upstream's own design, no new permutation — and shades `GetSurfaceReflectance` with the raw spec-gloss texel and `SpecularFactor`. The judgement: both upstream halves exist and are maintained, Sketchfab exports it by the million, and rule 2 says the engine opens the corpus that exists — while **authoring stays metallic-roughness** (`11-material-format.md`'s "absent on purpose" stands; a `.hpmat` has no workflow field, deliberately, so no new asset adopts an archived workflow). Asserted in `import_coverage_test.cpp`: `specularFactor` 0 vs 1 shades (2,82,196) vs (0.8,0.8,0.8) — the two files rendered identically before. **Upstream caveat, ours to know**: the loader never reads `glossinessFactor` and `RenderPBR.psh:163` hardcodes it to 1.0, so a factor-only SG material shades at roughness 0; per-pixel glossiness from the SG texture's alpha works | — |
| `KHR_materials_unlit` | raw | ✅ `Workflow = UNLIT` | ✅ **closed 2026-08-08 (T0168.2)**: `importedMaterialFlags` raises the same `HP_UNSHADED` permutation `Material::unlit` rides. Asserted: full base colour under zero lamps, (186,0,0) against the lit twin's (0,0,0) | — |
| `KHR_texture_transform` | raw | ✅ per-texture scale/rotation/offset | ✅ **closed 2026-08-08 (T0168.2)**: `importedMaterialFlags` raises `ENABLE_TEXCOORD_TRANSFORM` when any texture attrib carries a non-identity transform — per material, mirroring the authored path's `isIdentity` trade rather than upstream's raise-always. Asserted: a 0.5 UV offset flips the read from (255,0) to (0,255) | — |
| `KHR_materials_emissive_strength` | raw | ✅ folded into `EmissiveFactor` at load | flows through | ✅ |
| `KHR_materials_clearcoat` | raw | ✅ | ✅ authorable and imported (T0143); IBL half waits on T0087 | ✅ |
| `KHR_materials_sheen` | raw | ✅ | ✅ (same qualifier) | ✅ |
| `KHR_materials_anisotropy` | raw | ✅ | ✅ — with T0166's caveat: the basis chirality comes from two `cross` calls, untested against a mirrored tangent | ✅ |
| `KHR_materials_iridescence` | raw | ✅ | ✅ — the (248,129,93) split is T0143's flagship measurement | ✅ |
| `KHR_materials_transmission` | raw | ✅ (forces `BLEND`) | ✅ via the blend pass + screen colour (T0147); refraction fidelity waits on T0087 | ✅ |
| `KHR_materials_volume` | raw | ✅ | ✅ data flows; visual no-op until T0087 (assertion waiting to flip, on T0087's Refs) | ✅ |
| `KHR_materials_ior` | raw | 🔌 read **only inside the transmission block** — a standalone `ior` (dielectric F0 ≠ 0.04) is ignored | same | ⬆️ (upstream's own reader is partial) · unowned |
| `KHR_draco_mesh_compression` | ✅ full decode behind `TINYGLTF_ENABLE_DRACO` | wired behind `if (TARGET draco OR draco_static)`, or configure-time `FetchContent` via `DILIGENT_ENABLE_DRACO` — which offline-configure forbids | the built `libDiligent-AssetLoader.a` has zero draco symbols; a draco file throws at load (caught, logged, nothing stored) | 🔧 · **T0168.3 decides** |
| `KHR_lights_punctual` | ✅ parsed | ✅ loaded into `Model::Lights` (type, colour, intensity, range, cones) | dropped at draw — `drawModel` reads only `pMesh` | 🔌 · `hp::Light` exists; mapping is **T0169**'s table |
| `KHR_materials_specular` | — | — | — | ⬆️ zero hits at every layer · unowned |
| `KHR_materials_dispersion` | — | — | — | ⬆️ · unowned |
| `KHR_materials_variants` | — | — | — | ⬆️ (needs T0169's material identity first — a variant is a *mapping*, and today materials have no identity to map) · unowned |
| `KHR_mesh_quantization` | n/a (relaxes accessor type rules) | conversion machinery handles every component type + `normalized` — **believed to work by construction, unverified by any asset** | same | ⬆️→✅? **prediction for T0167/168.8** — needs one quantized asset to flip |
| `KHR_texture_basisu` | — | KTX2 **explicitly refused** (`KTXLoader.cpp:291: "ktx2.0 is not currently supported"`) | — | ⬆️ transcoder is a real vendor decision (basis_universal) · unowned |
| `KHR_xmp_json_ld` | — | — | metadata only; ignoring is conformant and rendering-neutral | 🚫 declined 2026-08-08 — no rendering meaning; revisit only if asset provenance tooling wants it |
| `KHR_animation_pointer` | — | — | — | ⬆️ moot until an animation runtime exists (T0041/T0049) |
| `EXT_meshopt_compression` | — | — | meshoptimizer *is* vendored (T0039) but the decode wiring through tinygltf does not exist at any layer | ⬆️ · unowned |
| `EXT_mesh_gpu_instancing` | — | — | the node renders **one** instance at the node TRS — thousands of authored instances silently absent | ⬆️ · unowned (T0164's per-instance data is the natural consumer) |
| `EXT_texture_webp` | — | — | — | ⬆️ · unowned |

## What reading could not settle — T0167's brief (168.8)

Predictions this table makes that one real asset can correct. **Where the asset
disagrees, the asset wins and the table is wrong** — record the correction here.

1. **Spec-gloss**, revised after T0168.2 closed it: the Aston Martin should now
   render through the genuine SG path — diffuse texture, specular colour and
   per-pixel glossiness all honoured — dark and flat only for what IBL (T0087)
   and shadows (T0086) have not built yet. The synthetic test covers factors
   and the diffuse slot alias; the car is the first asset to exercise the SG
   *texture* in the phys-desc slot.
2. **Quantization**: unverified-by-construction claim above.
3. **Z-up → Y-up root rotation** (det +1): composes through `ComputeTransforms`
   under D33 as amended — the car should stand upright, unmirrored.
4. **One blended double-sided material**: all 642K triangles down the blend
   pass, nothing culled — a load the synthetic assets never produced.

## Verification basis

Rows marked ✅ are **read-verified against the code paths named**, and
render-verified only where the existing suites exercise them (cube/rockcube
assets, T0143's punctual pixel tests, T0103's pack test). This table was built
by reading (T0168.1) — that is its point, and its limit: T0167 is the first
empirical check, and 168.8 records what it corrects.
