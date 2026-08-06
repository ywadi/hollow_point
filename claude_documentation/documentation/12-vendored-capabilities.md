# What DiligentEngine already gives us

**Read this before building anything in the render layer.** The whole reason this
engine vendors DiligentEngine is to avoid writing what already works (D24, D26).
That saving is only real if somebody checks — and the check has failed twice in
this project's history, both times with a ticket planning to build something that
was already sitting in `third_party/`. `entt::meta` is the worked example: T0053
listed three mechanisms for reflection and none of them was the full reflection
system already in the tree.

This document is the inventory. It is **capability-level, with paths** — it
deliberately does not copy the settings tables, because those go stale and the
source does not. Follow the path and read the struct.

> **Surveyed 2026-08-06** against the pinned submodule. If the submodule pin has
> moved since, treat counts as approximate and the paths as the durable part.
> The full settings-level survey this was distilled from is reproducible: it read
> the six `DiligentFX/PostProcess/` components, `ShadowMapManager`, the
> Atmosphere and Shadows samples, and `DiligentFX/Components/`.

---

## The distinction that matters most: *exists* is not *reachable*

Every entry below separates **what exists** from **how it is switched on**, and
that separation is not pedantry. This engine has already shipped a bug of exactly
this shape: `PBR_Renderer::CreateInfo::TextureAttribIndices` defaults to all
`-1`, meaning "this renderer does not use that texture", and `SceneRenderer`
never set it. Three separate code paths silently did nothing — no macro emitted,
no texture bound, no per-texture attributes written — in a release build, with no
log line. It was found only because a shader failed to compile.

**A capability that exists but is masked off looks exactly like a capability that
is absent.** When this document says something exists, go and check it is
actually reachable before you rely on it.

A live instance of the same pattern: the Atmosphere sample's **inline tone-mapping
UI is a stale copy** exposing 8 of the 12 operators. The reusable
`Components::ToneMappingUpdateUI` panel has all 12. Copying the sample's UI would
silently lose AgX, AgX Custom, PBR Neutral and Commerce.

---

## Inventory

| Capability | Exists | Where | How it is reached | Owner |
|---|---|---|---|---|
| **Atmospheric scattering** — light shafts, sun disc, aerial perspective | ~40 struct fields, 6 technique enums. The largest single surface surveyed | `DiligentFX/PostProcess/EpipolarLightScattering/` | A component to construct and wire into a frame | **T0088**, evaluated by T0091 |
| **Tone mapping** | **12 operators** — Exp, Reinhard, Reinhard-mod, Uncharted2, Filmic ALU, Logarithmic, AgX, AgX Custom, PBR Neutral, Commerce and variants | `DiligentFX/Shaders/PostProcess/ToneMapping/public/ToneMapping.fxh`; UI in `Components::ToneMappingUpdateUI` | Shader macro `TONE_MAPPING_MODE` + `ToneMappingAttribs` | **T0096** |
| **Bloom** | 6 settings | `DiligentFX/PostProcess/Bloom/` | Component | **T0148** |
| **Depth of Field** | 7 settings | `DiligentFX/PostProcess/DepthOfField/` | Component | **T0148** |
| **SSAO** | 11 settings | `DiligentFX/PostProcess/ScreenSpaceAmbientOcclusion/` | Component | **T0148** |
| **SSR** | 16 settings | `DiligentFX/PostProcess/ScreenSpaceReflection/` | Component | **T0148** |
| **TAA** | 6 settings | `DiligentFX/PostProcess/TemporalAntiAliasing/` | Component | **T0148** |
| **Shadows** — cascades, 4 filter modes, bias, stabilisation | ~20 settings; PCF, VSM, EVSM2, EVSM4 | `DiligentCore/.../ShadowMapManager`, `BasicStructures.fxh`, `DiligentSamples/Samples/Shadows/` | `ShadowMapManager` + `SHADOW_MODE_*` macro | **T0086** |
| **Terrain** — ring mesh, heightmap, 5-layer splat, cascade integration | Complete working implementation | `DiligentSamples/Samples/Atmosphere/src/Terrain/` | **Sample source — copy and adapt, not a library to link** | **T0155** |
| **Environment / IBL / skybox** | `EnvMapRenderer` + `PBR_Renderer`'s IBL bake | `DiligentFX/Components/`, `DiligentFX/PBR/` | Component + `CreateInfo` flag | **T0087** |
| **`GBuffer`, `PostFXContext`** | shared infrastructure the six post components need | `DiligentFX/PostProcess/Common/` | Constructed and threaded through | infrastructure for **T0148** |
| **Debug gizmos** — coordinate grid, bounding box, vector field | 3 complete renderers, unreferenced | `DiligentFX/Components/` | Component | **none — deliberately deferred**, editor era |
| **`DepthRangeCalculator`** | auto near/far extraction | `DiligentFX/PostProcess/Common/` | Component, **compute-only, no PS fallback** | blocked on **T0150** |

---

## The compute gate is much narrower than assumed

This engine has **no compute pipeline at all** (T0150). The natural assumption is
that the post-process chain is therefore blocked. **Measured, it is not** — every
`*_Compute*.fx` file in the six components was grepped for `numthreads`, and they
are full-screen-triangle *pixel* shaders despite the filenames.

| Needs compute | Does not |
|---|---|
| `EpipolarLightScattering` LUT precompute — 6 passes, **one-time**, only for LUT/multi-scattering modes | Bloom, DoF, SSAO, SSR, TAA |
| `EpipolarLightScattering` `RefineSampleLocations` — **per-frame**, only in epipolar-sampling technique | Tone mapping, shadows, terrain |
| `DepthRangeCalculator` — always | `PostFXContext` shared infrastructure |

**The consequence worth acting on:** a reduced scattering configuration —
`LIGHT_SCTR_TECHNIQUE_BRUTE_FORCE` + `SINGLE_SCTR_MODE_INTEGRATION` +
`MULTIPLE_SCTR_MODE_NONE` — needs **zero** compute and could ship before T0150, at
a real quality and performance cost. Full epipolar sampling and LUT-based
scattering need T0150 first.

## Vulkan

**Nothing surveyed is D3D-only.** Every component compiles from the same
HLSL-syntax `.fx`/`.fxh` sources through Diligent's cross-compiler. The single
D3D12-specific branch found in scope is Shadows-sample scaffolding
(`GPUDescriptorHeapSize`), irrelevant under D29.

---

## Keeping this honest

- **Add a row when you find something**, and name the owning ticket — or write
  "none" loudly, because that is the row that pays for this document.
- **A pin bump can invalidate the counts.** The paths are the durable part.
- **Do not treat this as permission to skip reading the source.** It tells you
  something exists and roughly where; it does not tell you the current signature,
  and the settings tables were deliberately left out for that reason.
- **`Samples/` is not a library.** Two entries above — terrain and the shadow
  sample's scaffolding — live in `DiligentSamples/`, which means copy-and-adapt
  with all the maintenance that implies, not a dependency edge. That distinction
  changes the cost of adopting something by an order of magnitude, so it is
  stated per row rather than assumed.
