# T0167 — A model from Sketchfab, rendered and measured: the first asset chosen by somebody other than us

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 463 |
| **Created** | 2026-08-08 |
| **Blocked by** | [0166-tangent-frames-and-real-assets.md](../completed/0166-tangent-frames-and-real-assets.md) — **not a soft ordering.** A production model has several things wrong at once; run it against a known-clean frame or the wrong cause gets debugged |
| **Refs** | [0166-tangent-frames-and-real-assets.md](../completed/0166-tangent-frames-and-real-assets.md) — supplies the controlled cases this ticket deliberately is not; [../completed/0165-right-handed-engine.md](../completed/0165-right-handed-engine.md) — **this is the observation D33's amendment was argued from and never made**; [../completed/0157-rock-cube-sample.md](../completed/0157-rock-cube-sample.md) — the authoring path a real asset re-walks; [../completed/0142-slang-shader-language.md](../completed/0142-slang-shader-language.md), [../completed/0143-extended-material-features.md](../completed/0143-extended-material-features.md) — the features a real material will actually exercise; [0087-environment-lighting.md](0087-environment-lighting.md) — a real PBR asset will look wrong without IBL, and that is **not** a defect this ticket may report; [../../documentation/11-material-format.md](../../documentation/11-material-format.md); **D13**, **D33** |

## The asset

**`aston_martin.glb`** — supplied by the owner 2026-08-08, currently at `~/Downloads/aston_martin.glb` (**not** in the repository; see 167.7).

| | |
|---|---|
| Title / author | *Aston Martin*, Francesco Coldesina ([topfrank2013](https://sketchfab.com/topfrank2013)) |
| Source | https://sketchfab.com/3d-models/aston-martin-1633d1972aa84b7891dc50cd6e83cd18 |
| Licence | **CC-BY-4.0** — redistributable **with attribution**, which 167.7 must carry |
| Generator | `Sketchfab-12.65.0`, glTF 2.0 binary |
| Size | 82.3 MiB (JSON chunk 0.03 MiB, BIN chunk 82.23 MiB) |

Measured by parsing the container directly, not inferred:

- **1 scene, 33 nodes, 31 meshes, 31 primitives, 642,553 triangles**, every primitive `mode: 4` (TRIANGLES) and indexed
- **Attributes, all 31/31**: `POSITION`, `NORMAL`, **`TANGENT`**, `TEXCOORD_0`. No second UV set, no vertex colours, no skinning, no animations, no cameras
- **One material for the entire car** — `alphaMode: BLEND`, `doubleSided: true`
- **4 embedded PNGs**: 16.54 MiB diffuse, 21.40 MiB specular-glossiness, 4.32 MiB normal, 6.17 MiB occlusion. One sampler, trilinear, `REPEAT`
- **Root node `Sketchfab_model` carries a Z-up → Y-up rotation** (maps `y → −z`, `z → +y`). **Determinant +1 — a rotation, not a mirror.** No node in the file carries a negative scale or a mirrored matrix

## Why

**The engine has never been shown an asset it did not help make.** The owner is supplying one from Sketchfab — chosen for what it is, not for what it tests — and that is the point: it carries whatever a real artist's export carries, including the things nobody here would think to write. This one carried three inside ten minutes of reading, before a single frame was rendered.

**It is also the observation D33's amendment was argued from.** T0165 made the engine right-handed on the reasoning that *a Blender artist's model arrives as authored*. That claim is currently **derived, not observed**. One model settles it.

## Done when

- [ ] A Sketchfab model renders in the editor, and the result is judged **both by eye and by number** — a screenshot alongside measurements, not one standing in for the other
- [ ] Orientation, scale and handedness are confirmed against the source: **what the artist authored is what appears**, with the axis and units stated
- [ ] Every defect found is either fixed, ticketed, or written down as accepted — with which of the three, and why
- [ ] What the asset **did not** exercise is listed, because that is the next asset's brief

## Subtasks

- [ ] 167.1 **Take the asset as it comes.** No re-export, no axis fix-up, no re-bake, no hand-edited glTF. The moment it is adjusted to suit the engine it stops being the test — and "we had to fix the asset first" is itself the finding
- [ ] 167.2 **Import and report what the loader saw**: meshes, primitives, materials, attributes present and absent (`TANGENT` especially), texture count and colour spaces, extensions, units and bounds. Much of this is already logged; what is missing is reading it deliberately
- [ ] 167.3 **Render and look**, in the editor, from several angles, with the light moved. Capture frames. **The rock cube's black top face is unexplained and this is where a second data point comes from**
- [ ] 167.4 **Measure, do not only look.** A gpu case that renders it offscreen and asserts something falsifiable — coverage, the magenta and checkerboard guards, luminance response to a moved light, and orientation against the source. **A screenshot alone has misled this project twice**: a magenta checkerboard was read as a working render because the summary statistics passed
- [ ] 167.5 **Test the handedness claim explicitly.** Front is front, left is left, up is up, one unit is one unit — against the source in Blender, which is available in this session over MCP
- [ ] 167.6 **Separate defects from absences.** A real PBR asset will look flat and dark without IBL, and that is T0087's, not a bug. So will anything expecting shadows (T0086). **Say which bucket each finding is in** — a missing system reported as a defect is how a clean engine gets "fixed" into a broken one
- [ ] 167.7 **Decide whether the asset joins the repository**, and on what terms. **CC-BY-4.0 permits it and 82.3 MiB argues against it** — 48 MiB of that is four embedded PNGs, two of them over 16 MiB. A decimated or texture-reduced copy is a different asset and stops being this test. Whatever is decided, **the attribution travels with any copy that lands**: *Aston Martin, Francesco Coldesina, CC-BY-4.0*, with the Sketchfab URL. If it cannot be committed, the measurement still counts and the ticket says so
- [ ] 167.8 **State plainly how far it gets without spec-gloss**, and stop there. `KHR_materials_pbrSpecularGlossiness` is in `extensionsRequired` and this engine implements metallic-roughness only, by a decision already recorded in `11-material-format.md`. Nothing in our loader stack refuses on that basis, so it will load and render **wrongly rather than not at all** — say what that looks like. **Whether to adopt it is a separate ticket** — Khronos archived the workflow, Sketchfab still exports it by the million, and that trade is not this ticket's to make. Open the ticket, do not take the decision
- [ ] 167.9b **Decide whether the engine should warn on an unsupported required extension**, since neither `tinygltf` nor Diligent looks at the field. This is a genuinely open call rather than an obvious yes: it is not spec-mandated, and a hard refusal would reject assets that render acceptably. A **log line naming the extension** is the cheap middle, and the argument for it is that the alternative failure is silent — a material that quietly resolves to defaults, which is exactly the class D35 exists to prevent
- [ ] 167.9 **Test `TANGENT.w` against real data.** 1,316 vertices in this file carry `w = −1` and the loader discards every one of them (Diligent's vertex path is `float3`). That is the first evidence in this project of the sign actually differing, and it is [T0166](../completed/0166-tangent-frames-and-real-assets.md).4's missing input — **report it there whichever way T0166 resolves**

## Not in scope

- **Fixing whatever it finds**, beyond what is small and obviously right. The value is the list; a large finding earns its own ticket rather than dragging this one open.
- **A performance claim.** One model on one machine measures nothing about the renderer's speed. T0045 and T0050 own that.
- **Anything about how good it looks.** Missing IBL, missing shadows and untuned exposure are systems that do not exist yet, and 167.6 exists to stop them being reported as regressions.

## Notes / findings

### What T0168 closed under this ticket's subtasks (2026-08-08) — read before starting

[T0168](../completed/0168-asset-import-coverage.md) landed the paper half and closed several of this ticket's questions ahead of it. The prediction list this ticket tests is **`14-asset-import-matrix.md` § "What reading could not settle"** — where the car disagrees, the car wins and the matrix records the correction (168.8's standing instruction).

- **167.8 is overtaken in its premise**: the engine now implements spec-gloss **for imported models** (runtime `Workflow` branch in `HpSurface.slang`, upstream's own design; authoring stays MR by the standing `11-material-format.md` decision). So the question is no longer "how far does it get without spec-gloss" but "does the genuine SG path survive a real asset" — the synthetic test covered factors and the diffuse slot alias; **the car is the first asset to exercise the SG *texture* in the phys-desc slot**. One caveat to watch: upstream never reads `glossinessFactor` (hardcoded 1.0) — irrelevant here, the car carries an SG texture, but it is the matrix row to check against.
- **167.9b is decided and built** (T0168.6): the import warns, once per model, naming each required extension outside the supported list — via a pre-parse of the raw JSON chunk, *because* a file that fails tinygltf's validation never reaches the loader callbacks (measured on `pirate.glb`). The car requires only spec-gloss, which is supported, so **the expected observation is no warning at all**.
- **167.9 (`TANGENT.w`) is resolved as a recorded decline** (T0168.4, matrix row): the vertex path stays `float3`, and handedness is read per fragment from the UV determinant, where the corrected frame (T0168.5) needs it anyway. The car's 1,316 `w = −1` vertices are therefore expected to render **correctly without the attribute** — mirrored shells follow the chart through `HpTangentFrameGrad`, pinned by the two-shell lit case. If the car's mirrored UVs shade wrong anyway, that is a real finding *against* the derivative-frame decision and belongs here.
- The normal map's green channel is now applied per glTF's convention (green up). Sketchfab exports GL-convention maps, so the car's normal map should read correctly; inverted-looking relief would again be a finding, not a tuning knob.

## Notes / findings (measured)

### What the file contains, measured 2026-08-08 before any render

Three things were found by parsing the container, and the first is the headline.

#### 1. It **requires** an extension this engine deliberately does not implement

`KHR_materials_pbrSpecularGlossiness` appears in **`extensionsRequired`**, not merely `extensionsUsed`.

**What the spec actually says, checked against the source rather than remembered.** All three normative sentences put the obligation on the *asset*, and **none of them puts one on the consumer**:

> All extensions used in a glTF asset **MUST** be listed in the top-level `extensionsUsed` array object […]
>
> All glTF extensions required to load and/or render an asset **MUST** be listed in the top-level `extensionsRequired` array […]
>
> `extensionsRequired` is a subset of `extensionsUsed`. All values in `extensionsRequired` **MUST** also exist in `extensionsUsed`.
>
> — glTF 2.0, *Specifying Extensions* ([`Specification.adoc:2663–2689`](https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/Specification.adoc))

**There is no sentence requiring a client to fail, refuse or decline to render.** Searched for one; the spec's only nearby client-side disposition points the other way — a client "can load glTF 2.x assets while **gracefully ignoring any new features it does not understand**" (§ versioning). Treating a required extension as fatal is an **ecosystem convention** — the glTF Validator flags it, several loaders throw — not a normative requirement. An earlier revision of this ticket asserted the opposite; it was wrong.

**And our stack does not check it at all**, which is the fact that decides what actually happens here. `tinygltf` parses `extensionsRequired` into the model and only ever re-serialises it (`tiny_gltf.h:6064`, `8112`) — there is no refusal path. Diligent's `GLTFLoader` never reads the field: zero hits across the whole `AssetLoader/` tree. So **the asset will load**, and what the spec's "required to load and/or render" is really telling us is that the result will be *wrong* rather than *absent*.

That makes the expected symptom a quiet one, which is worse than a refusal and is the thing to watch for.

The support picture is layered, and the gap is ours:

| Layer | Supports spec-gloss? |
|---|---|
| Diligent's loader | **Yes** — `GLTFLoader.cpp:1510-1516` sets `PBR_WORKFLOW_SPEC_GLOSS` and loads the texture |
| DiligentFX's shader | **Yes** — `SpecularFactor` is read under that workflow (`RenderPBR.psh:159`) |
| **HollowPoint** | **No.** `SceneRenderer.cpp:329-330` hardcodes `material.unlit ? PBR_WORKFLOW_UNLIT : PBR_WORKFLOW_METALL_ROUGH`, and `hp::Material` has no `diffuseTexture`, no `specularGlossinessTexture` and no workflow field at all |

**This was a decision, not an oversight**, and it is already written down — `11-material-format.md:456-458` lists `SpecularFactor` as *"read only under `PBR_WORKFLOW_SPECULAR_GLOSSINESS`, which this engine does not use. Absent on purpose, not forgotten."*

So the likely first observation is **not** a rendering bug: it is the car importing with its diffuse and specular-glossiness textures unmapped and rendering as a default surface. **Do not "fix" that by widening the material in passing.** Whether the engine adopts the deprecated spec-gloss workflow — Khronos archived it in favour of metallic-roughness, and Sketchfab still exports it by the million — is a real decision with a real cost, and it earns its own ticket. What this ticket owes is the finding, stated precisely, plus how far the asset gets without it.

#### 2. It carries mirrored UVs — genuinely, but **weakly**, and this is a correction

Per-triangle UV signed area across all 642,553 triangles:

| | |
|---|---|
| Negative (mirrored) | **594 — 0.1%**, spread over 24 of 31 primitives, at most 1.5% within any one |
| Positive | 641,959 — 99.9% |

And the handedness the loader throws away is present in the file: **`TANGENT.w` is `−1` on 1,316 vertices** and `+1` on 576,433.

**This confirms the mechanism occurs in production output, and it is not the stress case.** [T0166](../completed/0166-tangent-frames-and-real-assets.md)'s framing — relief reading inside-out across half a symmetric model — needs a large contiguous mirrored shell, and this asset does not have one; a scattered ≤1.5% is as consistent with seam and near-degenerate triangles as with deliberate mirroring. **This asset is corroboration, not a substitute for T0166.3's controlled two-shell case.** Anyone reading "a real asset has mirrored UVs" as "T0166.3 is covered" would be drawing the wrong conclusion from a true sentence.

The 1,316 `w = −1` vertices are, separately, a real population to test **166.4** against — the first evidence in this project of the discarded sign actually differing.

#### 3. The whole car is one blended, double-sided material

`alphaMode: BLEND` and `doubleSided: true` on the single material means all 642K triangles go down [T0147](../completed/0147-engine-intermediates-for-shaders.md)'s **blend** pass and nothing culls. That is Sketchfab's exporter default rather than an authoring intent, and it is exactly the kind of thing a synthetic asset would never have produced. Worth observing rather than correcting — 167.1 says take it as it comes.

### The one thing that is now a direct experiment

The root node's Z-up → Y-up rotation (determinant **+1**, verified — a rotation, not a mirror) is **167.5's test made concrete**: if the engine composes node transforms correctly under D33 as amended, the car stands upright and faces the way the artist left it. If the handedness sweep missed something, this is the asset that shows it, because the correction is in the file rather than in our code.

### Two traps that have already cost this project time, and both apply here

- **A failed shader renders magenta (~127, 0, 127) and passes coverage assertions.** Every asserted frame needs the magenta guard, and the checkerboard guard for a missing material. Both were read as working renders in a single session.
- **The gpu suite keeps its own asset import list, separate from a module's.** A file added to one and not the other imports in the editor and is silently absent in the test, or the reverse.

### Why this is second rather than first

Not caution — sequencing. A production model that renders wrong says *something* is wrong, not *which* thing, and T0166 has one confirmed defect (the tangent frame's dropped determinant sign) plus several unchecked conventions in flight. Landing this first means the first real asset renders wrong for two reasons at once, and the trap is that the visible one gets blamed. T0157 lost a day to exactly that shape.
