# Does DiligentEngine already do this? The render-layer capability matrix

**This document is binding (D40).** It answers one question from one place: for
each capability the render layer might want, what does the vendored tree already
implement, how is it reached — and where the answer is "it does not", **whose gap
is that**.

**Before building anything in the render layer, the first written note on the
ticket is what Diligent already does about it.** Not a habit; a required
artefact, before the code. **D35 extends here**: a render feature gets a row in
this table *before* it is built, exactly as a shader technique gets one in
[`13-shader-capability-matrix.md`](13-shader-capability-matrix.md) and a format
feature gets one in [`14-asset-import-matrix.md`](14-asset-import-matrix.md).

**The check has failed six times.** Spec-gloss, glTF's `TANGENT.w`, the
cotangent-frame determinant, Draco, the normal-map green channel and
order-independent transparency were all vendored with the switch off, all found
by a person reading, **none by a test**. D40 is the decision that followed; this
is the artefact that makes it checkable.

> **Surveyed 2026-08-06, re-surveyed and corrected 2026-08-08 (T0171)** against
> the pinned submodule. The 2026-08-06 pass got three paths wrong and one
> capability backwards; every row below was re-read from source. **If the pin
> moves, treat counts as approximate and paths as the durable part** — and note
> that the 2026-08-08 pass found a **21,700-line subsystem that did not exist at
> the previous pin**, so a pin bump is a reason to re-run this, not to trust it.

---

## How to use it

- **Before building**: find the row. If the state is 🔌 or 🔧, the ticket is
  *wiring and settings*, and a ticket proposing to build it is wrong by
  construction.
- **The check is not "does Diligent do this"** but "does Diligent already do
  this *better than the thing we are about to write*" — T0166's lesson, where
  correct upstream code sat beside a wrong local copy.
- **⬆️ is a real answer and it must stay reachable.** D40 rejects adopting by
  default just as firmly as reimplementing by default. Where ⬆️ is chosen, the
  reason is written in the row's ticket.
- **When something lands or is decided, flip the row and date it.** The value is
  entirely in being current.

### Legend — the state names who owns the gap

| State | Meaning | What it costs us |
|---|---|---|
| ✅ **wired** | reachable from `hp::` today | nothing |
| 🔌 **construct and drive** | upstream ships it complete; we build the seam and surface the settings | plumbing, not implementation |
| 🔧 **switch off** | implemented behind a flag or a binding we do not supply | a flag, plus whatever the flag then demands |
| 📄 **sample source** | it is in `DiligentSamples/`, so **copy-and-adapt with all the maintenance that implies** — not a library to link | an order of magnitude more than 🔌 |
| ⬆️ **upstream absent** | Diligent does not implement it | ours to build, vendor, or decline — **with the argument written down** |
| 🚫 **declined** | decided against, with the reason | nothing, but it is *recorded* |

---

## The two seams, because almost every mis-scoped ticket confused them

**This engine has exactly two extension seams, and a capability belongs to one of
them or to neither.** Getting this wrong is what produced nine tickets each
proposing its own hook.

| | **The surface seam** | **The frame seam** |
|---|---|---|
| **What it is** | `IHpMaterial` — Slang generics, a game overrides methods | a **pass**: `IRenderLayer` inserted into `RenderStack`, in C++ |
| **Granularity** | per fragment | per frame |
| **Fits** | anything about a **surface** — shading, parallax, triplanar, the light loop, screen reads | anything about a **frame** — an extra pass, a full-screen effect, a second camera's view |
| **Default** | **the default implementation *is* the standard path**; overriding is opt-in | the engine's own layers |
| **Status** | **built** (T0141, T0142, T0145, T0146, T0147, T0159, T0160, T0161) | **interface built** (T0027, T0046) — **and never once crossed by a real app**: see the ⬆️ row below |
| **Owner** | closed tickets; extended by whoever adds a contract field | **T0094** |

**Everything else is a *setting*, not a seam.** Which tone-mapping operator,
which shadow filter, bloom threshold, cascade count — a game developer chooses
these out of data (T0078), and exposing them means reflected, serialized engine
data, never a hook. **If a ticket proposes a seam for something a struct field
would express, it is wrong.**

---

## Inventory

### Shading and lighting

| Capability | What upstream has, and where | State | Who owns the gap |
|---|---|---|---|
| **PBR shading, punctual lights** | `PBR_Renderer` / `PBR_Shading.fxh`; the engine seeds its own Slang pixel shader beside it | ✅ wired | — |
| **IBL — irradiance + prefiltered specular** | `PBR_Renderer::{EnableIBL, PrecomputeCubemaps, SetIBLResourceViews, CreateIrradianceCube, CreatePrefilteredEnvMap}`; **all `PBR_Renderer`'s, not `GLTF_PBR_Renderer`'s** | ✅ wired (T0170.5) | — |
| **Sheen, clearcoat, anisotropy, iridescence, transmission** | `PBR_Shading.fxh`, per-layer IBL (`GetSheenIBL`, `GetClearcoatIBL`) | ✅ wired (T0143 + T0170.5) | — |
| **Skybox as a visible background** | `DiligentFX/Components/interface/EnvMapRenderer.hpp` — **complete, and this engine never constructs it.** `grep EnvMapRenderer engine/` → nothing. The environment is integrated into cubemaps and never drawn | 🔧 switch off | **T0088** |
| **Tone mapping — 12 operators** | `Shaders/PostProcess/ToneMapping/public/ToneMapping.fxh:87`; `TONE_MAPPING_MODE_*` 0–11 in `ToneMappingStructures.fxh:11-22` (`NONE`, Exp, Reinhard, Reinhard-mod, Uncharted2, Filmic ALU, Logarithmic, Adaptive-log, AgX, AgX Custom, PBR Neutral, Commerce). UI in `Components::ToneMappingUpdateUI` | 🔌 construct and drive | **T0096** |
| **Cascaded shadow *shading*** | `Shaders/Common/public/Shadows.fxh` — `FindCascade:65`, `FilterShadowMap:219` **incl. cross-cascade blend :238**, `SampleFilterableShadowMap:350`; four modes at `BasicStructures.fxh:19-22` | 🔌 construct and drive | **T0086** |
| **Cascade textures and placement** | `DiligentFX/Components/interface/ShadowMapManager.hpp` — `DistributeCascades:165`, `SnapCascades:108`, `StabilizeExtents:112`, `ConvertToFilterable:169`, `GetCascadeDSV:88`. **Directional only** (`pLightDir:106`) | 🔌 construct and drive | **T0086** |
| **Point / spot shadows** | nothing. No cube, no dual-paraboloid, anywhere in DiligentFX | ⬆️ upstream absent | **T0086.8** — build or decline |
| **The `thickness` / `attenuation` / `ior` volume family, *shaded*** | nothing. `VolumeThickness` reaches a **debug view only** (`PBR_Shading.fxh:984`); upstream shades transmission as `diffuse *= 1 - Transmission` and stops, exactly as we do | ⬆️ upstream absent | unowned — see *Open gaps* |

### Post-processing

| Capability | What upstream has, and where | State | Who owns the gap |
|---|---|---|---|
| **Bloom** (6 settings) | `DiligentFX/PostProcess/Bloom/` | 🔌 construct and drive | **T0148** |
| **Depth of Field** (7) | `DiligentFX/PostProcess/DepthOfField/` | 🔌 | **T0148** |
| **SSAO** (11) | `DiligentFX/PostProcess/ScreenSpaceAmbientOcclusion/` | 🔌 | **T0148** |
| **SSR** (16) | `DiligentFX/PostProcess/ScreenSpaceReflection/` | 🔌 | **T0148** |
| **TAA** (6) | `DiligentFX/PostProcess/TemporalAntiAliasing/` | 🔌 | **T0148**, gated by T0111 |
| **`PostFXContext`** — shared inputs the five components need | `DiligentFX/PostProcess/Common/interface/PostFXContext.hpp`; 12 named slots at `:190-206` | 🔌 | infrastructure for **T0148** |
| **`GBuffer`** | `DiligentFX/Components/interface/GBuffer.hpp` — **not** `PostProcess/Common/`, which this table said until 2026-08-08. `Bind` is a plain `SetRenderTargets` (`GBuffer.cpp:166`) | 🔌 | **T0148** |
| **Atmospheric scattering / sun shafts** | `DiligentFX/PostProcess/EpipolarLightScattering/`. **41 fields**, 6 technique enum families (`EpipolarLightScatteringStructures.fxh:43-90`). **One directional light** (`FrameAttribs::pLightAttribs`, `.hpp:73`) | 🔌 construct and drive | **T0088**; the sun-shaft half of **T0091** |
| **Froxel volumetrics** — beams from *point and spot* lights | nothing | ⬆️ upstream absent | **T0091** — the expensive half, to be argued |
| **`DepthRangeCalculator`** | `DiligentFX/Components/interface/DepthRangeCalculator.hpp` — **not** `PostProcess/Common/`. **Compute-only, no PS fallback** | 🔧 blocked | **T0150** |

### Submission, passes and resources

| Capability | What upstream has, and where | State | Who owns the gap |
|---|---|---|---|
| **Automatic barriers** | `RESOURCE_STATE_TRANSITION_MODE_TRANSITION`, `DeviceContext.h:257`. **Explicitly not thread-safe** — `:254-261`, repeated at 16 further call sites | ✅ wired | — |
| **Frustum maths** | `DiligentCore/Common/interface/AdvancedMath.hpp` — `ViewFrustum:78`, `ViewFrustumExt:113`, `ExtractViewFrustumPlanesFromMatrix:128`, `GetBoxVisibility:489/518/576`, 3-state `BoxVisibility:303` | 🔌 construct and drive | **T0045** |
| **Scene graph, traversal, render queues, sorting** | **nothing, by design** — Diligent is a graphics abstraction. D40 names this as ours | ⬆️ upstream absent | **T0045** |
| **Order-independent transparency** | `PBR_Renderer::CreateInfo::OITLayerCount`, `CreateOITResources`, the `OITLayers` pass — **and `GLTF_PBR_Renderer.cpp:638` hardcodes `RenderPassType::Main`** in its only `PSOKey`, with no `RenderInfo` override. The population pass lives behind Hydrogent | 🚫 declined | evaluated and rejected on **T0170.3**; reachable only by patching vendored source, which D26/D40 forbid |
| **Per-PSO resource signatures** | **no hook.** `PBR_Renderer.cpp:2055-2058` copies one global `m_ResourceSignatures` into every PSO; the OIT append at `:2059-2063` is the shape a generalising upstream PR would take. `PBR_Renderer` has two virtuals — `~PBR_Renderer:464` and `CreateCustomSignature:906` — and the second feeds the *global* list | ⬆️ upstream absent | **the upstream PR we owe** — same PR as D30's lighting hook. Blocks shader-cache seeding (T0170.2) |
| **`GLTF_PBR_Renderer` as a base class** | **not usable.** Its ctor does not forward `PBR_Renderer`'s `InitSignature` flag (`GLTF_PBR_Renderer.cpp:107-111`), so deriving calls `CreateCustomSignature` through the wrong vtable and silently drops the engine's signature. Its `Render()` builds PSO caches from a default `GraphicsPipelineDesc` with `COMPARISON_FUNC_LESS` (`:113-129`) — **draws nothing under reverse-Z** (T0130). Upstream's own `USD_Renderer` derives from `PBR_Renderer` and passes `false, // InitSignature` (`USD_Renderer.cpp:215`) | 🚫 declined | recorded on **T0170.2**; `SurfacePipeline` already follows the correct precedent |
| **Render-pass / framebuffer objects** | `IRenderPass`, `IFramebuffer` (`RenderPass.h:504`, `Framebuffer.h:102`) — a **Vulkan wrapper and nothing more**: *"Render pass has no methods"* (`RenderPass.h:503`). Subpass dependencies are hand-authored, never derived. **DiligentFX uses them zero times**; one tutorial and two serialisation paths are the only consumers in the whole tree | 🚫 not needed | nobody — `SetRenderTargets` is what everything real uses |
| **Pass orchestration** | **none that derives anything.** The most complex frame in the tree, Hydrogent's **22-task** USD renderer, is a **hand-ordered `std::vector`** (`HnTaskManager.cpp:517-522`) with the read/write matrix written by hand **in a doc comment** (`HnTaskManager.hpp:92-155`). Pass "culling" is a per-task boolean (`HnTask::IsActive`). **Not built here** — Hydrogent needs `DILIGENT_USD_PATH`, never set | ⬆️ upstream absent | **T0047 — answered "no", D41.** `RenderStack` is the answer at our scale |
| **Render-target pooling / transient allocation / aliasing** | **none anywhere.** `PostFXContext`, `GBuffer`, `HnFrameRenderTargets` and each of the five effects all use the same policy: one persistent texture per named slot, recreated when the size changes (`PostFXContext.cpp:246`, `HnBeginFrameTask.cpp:193-198`). `IRenderStateCache` caches **shaders and PSOs only**. `STATE_TRANSITION_FLAG_ALIASING` is sparse-resources-only | ⬆️ upstream absent | **T0047 — declined, D41.** Revisit triggers are on the ticket |
| **A game-inserted pass** | not upstream's problem — but **ours is built and unused**: `RenderStack`/`IRenderLayer` exist and **no shipping app instantiates one.** Editor and runtime go through `SceneView`; `RenderStack` appears only in `tests/`. `ModuleServices` (`ModuleHost.hpp:62-81`) hands a module scene, assets, device and context — **no `RenderStack*`** | ⬆️ ours, unbuilt | **T0094** |
| **Compute pipelines** | standard API (`CreateComputePipelineState`, `DispatchCompute`); the engine requests none | ⬆️ ours, unbuilt | **T0150** |
| **PSO / shader cache** | `IRenderStateCache` (`RenderStateCache.h:187-302`) — real, and the engine **passes null** (`SceneRenderer.cpp:2139`, which records it as a live decision) | 🔧 switch off | unowned — see *Open gaps* |

### Content and tools

| Capability | What upstream has, and where | State | Who owns the gap |
|---|---|---|---|
| **Terrain** — concentric ring mesh, 16-bit heightmap, 5-layer splat, cascade integration | `DiligentSamples/Samples/Atmosphere/src/Terrain/`. Complete and working, **and it is `Samples/`** | 📄 sample source | **T0155** |
| **Editor coordinate grid** — infinite ray-marched grid + axes, depth-aware | `DiligentFX/Components/interface/CoordinateGridRenderer.hpp`; feature flags for three planes and three axes | 🔧 switch off | **T0032/T0033** (editor), *not* T0061 |
| **A single highlighted bounding box** | `BoundBoxRenderer.hpp` — **one box per `Prepare`+`Render` pair** (`:139`, `:143`); colour, dash pattern, reverse-depth option | 🔧 switch off | **T0061**, for the selection-outline case only |
| **Motion-vector field visualisation** | `VectorFieldRenderer.hpp` — draws a line grid **from a 2D vector-field texture**; not a general arrow API | 🔧 switch off | **T0061**, if motion vectors ever exist (T0111) |
| **Immediate-mode debug draw** — batched `Line` / `Sphere` / `Capsule` / `Frustum` / `Text` | **nothing.** No line renderer, no text renderer, nothing batched, anywhere in DiligentCore, DiligentFX or DiligentTools | ⬆️ upstream absent | **T0061** — and this is most of that ticket |
| **glTF import, Draco, texture load** | `DiligentTools/AssetLoader`, `TextureLoader` | ✅ wired | see [`14-asset-import-matrix.md`](14-asset-import-matrix.md) |

---

## `DiligentFX/Radient` — 21,700 lines nobody here knows about

**Found 2026-08-08 by T0171, and it is the reason this document exists.**

`third_party/DiligentEngine/DiligentFX/Radient/` is a **complete alternative
engine** inside the pinned DiligentFX: scene, components, importer, asset cache
and resolver, draw list, frame render targets, SRB cache, light list, a PBR
renderer, and a render pipeline with geometry, skybox and post-process passes.
103 tracked files, **21,746 lines**. It is genuine upstream (authored by
Diligent's maintainer, 2026-07-27), and **it is built by every build of this
project** — `DILIGENT_NO_RADIENT` defaults `OFF` and nothing here sets it.
Nothing in `engine/`, `apps/`, `samples/`, `tests/` or `tools/` references it.

**Verdict: 🚫 not adopted, and the reasons are specific rather than territorial.**

1. **It is an alternative to this engine, not a component of it.** Its object
   model is a C ABI (`IRadientRenderer`, `IRadientBackend`, `RadientScene`,
   `RADIENT_STATUS`) with its own scene, its own importer and its own asset
   cache. Adopting it means replacing `hp::Scene`, the ECS (**D12**, **D23**),
   the VFS (**D13**) and the material contract (**D26**, **D40**) — everything
   D40 lists as *ours*.
2. **Its pipeline has no insertion point.** `RadientRenderPipeline` is four
   concrete members in a fixed order (`RadientRenderPipeline.hpp:62-65`) —
   geometry, skybox, post-process. No registry, no list, no way for a caller to
   add a pass. `RenderStack` is strictly more extensible.
3. **Its post-process pipeline is an empty stub.** `RadientPostProcessPipeline`
   is documented *"tone mapping first, then TAA/SSAO/SSR/Bloom/DoF **as they are
   added**"* and both methods are `(void)` casts returning OK
   (`RadientPostProcessPipeline.cpp:33-49`). So it is not even a reference for
   T0148.
4. **Its PBR renderer would not carry `IHpMaterial`.** The surface seam is the
   reason this engine exists (D26 as amended, D40).

**Two things to do about it anyway**, and both are cheap:

- **Watch it.** If upstream matures Radient, the calculus changes — and this row
  is what makes that a decision rather than a discovery. Re-read on every pin
  bump.
- **Consider `DILIGENT_NO_RADIENT=ON`.** We compile 21,700 lines we never link.
  That is build time on every cold build and on every pin bump, for nothing.
  Not done here because it is a build change and this was a documentation sweep
  — **noted for whoever next touches `CMakeLists.txt`**.

---

## The compute gate is much narrower than assumed

This engine has **no compute pipeline at all** (T0150). The natural assumption is
that the post-process chain is therefore blocked. **Measured, it is not** — every
`*_Compute*.fx` file in the five components was grepped for `numthreads`, and
they are full-screen-triangle *pixel* shaders despite the filenames.

| Needs compute | Does not |
|---|---|
| `EpipolarLightScattering` LUT precompute — 6 passes, **one-time**, only for LUT / multi-scattering modes | Bloom, DoF, SSAO, SSR, TAA |
| `EpipolarLightScattering` `RefineSampleLocations` — **per-frame**, only in the epipolar-sampling technique | Tone mapping, shadows, terrain |
| `DepthRangeCalculator` — always | `PostFXContext`, `GBuffer` |

**The consequence worth acting on:** a reduced scattering configuration —
`LIGHT_SCTR_TECHNIQUE_BRUTE_FORCE` + `SINGLE_SCTR_MODE_INTEGRATION` +
`MULTIPLE_SCTR_MODE_NONE` — needs **zero** compute and could ship before T0150, at
a real quality and performance cost. Full epipolar sampling and LUT-based
scattering need T0150 first.

---

## Two traps that make a present capability look absent

**1. *Exists* is not *reachable*.** `PBR_Renderer::CreateInfo::TextureAttribIndices`
defaults to all `-1`, meaning "this renderer does not use that texture", and
`SceneRenderer` never set it. Three code paths silently did nothing — no macro,
no binding, no per-texture attributes — in a release build, with no log line. It
was found only because a shader failed to compile. **A capability that exists but
is masked off looks exactly like one that is absent.**

**2. A compile-time macro is not a runtime setting.** `ToneMap()` branches on the
`TONE_MAPPING_MODE` **macro** (`ToneMapping.fxh:101-198`); the
`iToneMappingMode` field in `ToneMappingAttribs` is **never read by the shader**.
Upstream's own answer is to key the PSO on it (`EnvMapRenderer.cpp:218`) or emit
it as a macro (`EpipolarLightScattering.cpp:1671`). So "let the game pick an
operator" is a **variant** question (T0151), not a constant-buffer write — and
the same shape applies to `SHADOW_MODE` and terrain's `TEXTURING_MODE`.

A live instance of the first trap: the Atmosphere sample's inline tone-mapping UI
is a **stale copy** exposing 8 of the 12 operators. `Components::ToneMappingUpdateUI`
has all 12. Copying the sample's UI would silently lose AgX, AgX Custom, PBR
Neutral and Commerce.

---

## Open gaps with no owner

**This section is the one that pays for the document.** A capability with no
owning ticket is invisible; naming it here is how it stops being.

| Gap | Why it has no owner | Next step |
|---|---|---|
| **The volume family shades nothing** — `thickness`, `attenuationColour`, `attenuationDistance`, `ior` are authored, packed and debug-visible, and `extended_material_test`'s volume case pins *"changes no shaded pixel"* | T0143 closed; T0087 carried it and is now closed too. **Upstream does not shade it either**, so this is genuinely ours and needs a real decision, not a switch | A game **can already write it** via `IHpMaterial` + T0147's scene-colour read (the shader matrix marks refraction *expressible today*). Whether the **standard** material does is a scope call |
| **`IRenderStateCache` is passed null** | `SceneRenderer.cpp:2139` records it as a live decision, on no ticket | Judge it beside T0151 (variant cost) — it is the PSO cache both would use |
| **Radient is compiled and never linked** | new upstream subsystem, no ticket could have anticipated it | `DILIGENT_NO_RADIENT=ON` in `CMakeLists.txt`, next time that file is touched |

---

## Vulkan

**Nothing surveyed is D3D-only.** Every component compiles from the same
HLSL-syntax `.fx`/`.fxh` sources through Diligent's cross-compiler. The single
D3D12-specific branch found in scope is Shadows-sample scaffolding
(`GPUDescriptorHeapSize`), irrelevant under D29.

---

## Keeping this honest

- **Add a row when you find something**, and name the owning ticket — **or write
  "unowned" loudly**, in the section above, because that is the row that pays for
  this document.
- **A pin bump can invalidate the counts, and can add whole subsystems.** Radient
  was not here at the previous pin. The paths are the durable part; the *set of
  paths* is not.
- **Do not treat this as permission to skip reading the source.** It says
  something exists and roughly where; it does not give you the current signature,
  and the settings tables are deliberately left out for that reason. The
  2026-08-06 pass of this document got three paths wrong precisely because it
  summarised instead of pointing.
- **`Samples/` is not a library.** That distinction changes the cost of adopting
  something by an order of magnitude, so it is a state (📄) rather than a
  footnote.
