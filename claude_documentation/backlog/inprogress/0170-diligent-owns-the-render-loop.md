# T0170 — DiligentEngine owns the render loop; the engine owns the shader

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 459 |
| **Created** | 2026-08-08 |
| **Blocked by** | nothing |
| **Refs** | **D26** — **this ticket amends it**; [0045-culling-and-render-queues.md](../open/0045-culling-and-render-queues.md) — **superseded in large part**: OIT replaces the back-to-front sort, and upstream's bucketing replaces the queue split; what survives is frustum culling and batching; [0087-environment-lighting.md](../open/0087-environment-lighting.md) — **probably subsumed**: `PBR_Renderer` ships IBL, so "configure or supersede" answers itself; [../completed/0147-engine-intermediates-for-shaders.md](../completed/0147-engine-intermediates-for-shaders.md) / **D37** — the 10.9b snapshot survives, but via `RenderInfo::AlphaModes` rather than our own split; [../completed/0161-game-resource-model.md](../completed/0161-game-resource-model.md) — moves onto `CreateCustomSignature`; [../completed/0145-lighting-stage-own-the-light-loop.md](../completed/0145-lighting-stage-own-the-light-loop.md) / **D30** — records that the lighting hook was to be offered upstream and never was; that debt is now on the critical path; [../completed/0141-custom-shader-materials.md](../completed/0141-custom-shader-materials.md) — the finding D26 rests on, **still true and narrower than what was built on it**; [../completed/0142-slang-shader-language.md](../completed/0142-slang-shader-language.md), [../completed/0166-tangent-frames-and-real-assets.md](../completed/0166-tangent-frames-and-real-assets.md), [0168-asset-import-coverage.md](../completed/0168-asset-import-coverage.md); **D12**, **D27**, **D35** |

## Why

**Six times in three days, a capability was already in the tree with the switch off** — spec-gloss, `TANGENT.w`, the tangent frame, Draco, the normal-map green channel, and now **order-independent transparency**. Each was found by a person reading, none by a test, and the last one had been sitting behind `OITLayerCount = 0` while this project prepared to hand-write a back-to-front sort that is strictly worse.

**The cause is structural, not carelessness.** **D26** says the engine owns the surface stage, decided on T0141's finding that *DiligentFX's lighting is a reusable public library but its material shader is not hookable*. **That finding is still true.** What went wrong is the conclusion drawn from it: a constraint on **the shader** was allowed to swallow **the submission loop** — scene traversal, alpha bucketing, PSO caching, depth policy, transparency. Every capability lost was invisible from inside our own loop. *You cannot see the OIT you are not calling.*

**Measured 2026-08-08, and two of three answers say the loop was never blocked:**

| Question | Answer |
|---|---|
| Can we interrupt between opaque and blend for D37's snapshot? | **Yes.** `GLTF_PBR_Renderer::RenderInfo::AlphaModes` is a flag mask (`GLTF_PBR_Renderer.hpp:98`). Render opaque\|mask, snapshot, render blend |
| Can we attach per-module resource signatures? | **Yes, by design.** `virtual void CreateCustomSignature(PipelineResourceSignatureDescX&&)` (`PBR_Renderer.hpp:906`), and upstream's own precedent is subclassing — `USD_Renderer final : public PBR_Renderer` |
| Can we supply our own material evaluation? | **No.** `GetPSMainSource` returns `{OutputStruct, Footer}` → two generated includes, the PS output struct and code appended at the *end* of main (`PBR_Renderer.cpp:1915-1935`). `USD_Renderer` uses it to emit a G-buffer. **Shading stays inside `RenderPBR.psh`** |

So the honest scope: **we must own the shader; we must not own the loop.**

## The decision this ticket takes

**D26 is amended**: the engine owns the **surface shader**, not the **submission**. `SceneRenderer` becomes a subclass of `GLTF_PBR_Renderer` rather than a reimplementation of it.

What that buys immediately, none of it written here: **OIT**, opaque→mask→blend bucketing, the PSO cache, upstream's depth policy for blended draws, **IBL**, and every future upstream feature arriving by submodule bump instead of being discovered six months late.

## Done when

- [ ] **The Aston Martin renders as it does in Blender, Godot and Unity** — interior visible through the glass from every angle, environment-lit, correct colour. Judged by eye **and** by a gpu case that fails on today's code
- [ ] The engine's draw path is **`GLTF_PBR_Renderer::Render`**, not a hand-written walk
- [ ] A game's Slang material still overrides `IHpMaterial` — **or the exact line that prevents it is named**, with the route to fixing it
- [ ] **D26 is amended in the decision log**, with the measurement above and what was rejected
- [ ] `13-shader-capability-matrix.md` and the render layer's own boundary document say **what Diligent owns, what we own, and why** — so D26's scope cannot silently expand again

## Subtasks

- [ ] 170.1 **Settle the shader question first — it is the only real unknown.** Shader sources resolve through `CreateCompoundShaderSourceFactory({DiligentFXShaderSourceStreamFactory, pMemorySourceFactory})` — a **chain**. A factory that shadows the surface-evaluation include could substitute our material code while keeping everything else. DiligentFX's factory is listed *first*, so this is not a supported extension point today. **Determine whether it can be made one — by ordering, by an upstream PR, or not at all.** D30 already records an upstream hook offer we owe and never made
- [ ] 170.2 **Subclass, do not fork.** `class HpRenderer : public GLTF_PBR_Renderer`, following `USD_Renderer` as the worked precedent. Override `CreateCustomSignature` for T0161
- [x] 170.3 **OIT — evaluated and rejected; it is not reachable from here.** — `OITLayerCount`, `CreateOITResources`, `CreateClearOITLayersSRB`, `SetOITResources`, and the `OITLayers` pass. Read how **Hydrogent** sequences it (`HnRenderRprimsTask`, `HnEndOITPassTask`) and copy that sequence
- [ ] 170.4 **D37's snapshot via `RenderInfo::AlphaModes`**, deleting the hand-rolled 10.9a/c split
- [x] 170.5 **Turn IBL on** and give the car an environment. This is what makes it look like the DCC preview — its paint colour is authored into the *specular* map and there is currently nothing to reflect
- [ ] 170.6 **Delete what is now upstream's.** Measure the line count before and after; a refactor that adds code has gone wrong
- [ ] 170.7 **Amend D26**, and write the boundary document
- [ ] 170.8 **Re-scope T0045 and T0087** on their own tickets — say what survives and what this absorbed

## Not in scope

- **The RHI.** Device, swapchain and context are already Diligent's and unaffected.
- **Import.** T0168/T0169 landed; this is the draw path only.
- **Gameplay-facing API.** `hp::SceneRenderer`'s public surface should not move — this is an implementation change (**D12**: gameplay is in lockstep, so a header change costs a rebuild of every module).

## Notes / findings

### 170.5 — the environment is on, and the car is the colour it is in Blender

**What Diligent does about it, first** (the rule this ticket sets): all of it.
`EnableIBL`, `PSO_FLAG_USE_IBL`, `CreateIrradianceCube`,
`CreatePrefilteredEnvMap`, `PrecomputeCubemaps`, `SetIBLResourceViews` and
`SetInternalShaderParameters` are **`PBR_Renderer`'s**, not
`GLTF_PBR_Renderer`'s — so `SurfacePipeline`, which has been a `PBR_Renderer`
subclass since T0141.10, could call every one of them without any structural
change at all. The BRDF LUT is precomputed by the base constructor. Nothing was
written here that upstream had.

**Measured, on the RTX 2080, `aston_martin_test`, same camera and same lamps:**

| | before | after |
|---|---|---|
| mean luma of covered pixels, `front_quarter` | **10.4** | **112.9** |
| luma spread | 20.2 | 38.8 |
| coverage | 41.0% | 39.1% |
| magenta share | 0 | 0 |

The car goes from near-black to its authored cyan. That is the whole of the
ticket's "correct paint colour": this asset is spec-gloss, its paint lives in
the **specular** map and its diffuse is navy-black, so with nothing to reflect
it was correctly rendering as almost nothing. Coverage falls 1.9 points because
the near-black body used to differ from the clear colour by more than the
`isClear` tolerance in places where it now does not — the silhouette is
unchanged.

**The seam T0087 reserved in `HpSurface.slang` was an `#error`, and it is
filled.** The punctual loop had to be *mirrored* (D30, because
`ApplyPunctualLight` takes the whole `SurfaceShadingInfo`); the image-based
getters take a bare `SurfaceReflectanceInfo`, every field of which
`HpShadedSurface` already carried, so `GetIBLSamplingInfo`,
`GetLambertianIBL`, `GetSpecularIBL_GGX` and `GetSpecularIBL_Charlie` are
**called, not mirrored**. There is no second copy of upstream's maths and no
drift guard needed.

**Sampled before `lighting()`, on purpose.** `HpShadedSurface` gains
`DiffuseIBL`, `SpecularIBL`, `SheenIBL` and `ClearcoatIBL`, filled by the
engine before the material's hook runs. Sampling them *inside* the default
`lighting()` would have made "override the light loop" silently mean "lose
every reflection", which is exactly the class of failure `HpShadedSurface`
exists to prevent. `HpIBLWeight(Surface)` names the `IBLScale * Occlusion`
product once so a custom resolve reuses it instead of re-deriving it.

**Two things had to be built, and both were forced rather than chosen:**

- **The preintegrated Charlie BRDF LUT is now embedded**, beside the sheen
  albedo-scaling LUT T0143 embedded for the same reason. Upstream loads it
  inside its `EnableSheen` block but only `if (EnableIBL)` — so turning IBL on
  with sheen already on is precisely what made a second embedded LUT
  necessary. Without it `g_PreintegratedCharlie` binds **null** for every sheen
  permutation. 25 KiB, from the pinned submodule, same `hp_embed_binary` path.
- **A default environment**, because `EnableIBL` with unbound cubemaps is a
  null descriptor rather than a fallback. It is a **procedural sky** — a
  256×128 equirectangular map, cool zenith, bright horizon band, dark floor,
  one sun disc — generated at `create()` and integrated by
  `PrecomputeCubemaps`. **Not** the vendored `papermill.ktx`, and that is a
  decision worth arguing with: papermill is 16 MiB, so embedding it puts 16 MiB
  of one particular sky into every shipped game binary, and loading it from
  disk makes the engine's default lighting depend on an asset the VFS has to
  find (D13). The generated sky costs 512 KiB of startup scratch and no bytes
  at rest. **A real HDR environment is the game's to supply** and that seam is
  T0087's remaining scope.

**One test assertion was replaced, not re-baselined.** `aston_martin_test`
checked that swinging the key light 137° moved the frame's mean luma by ≥5%.
With an environment the mean barely moves — 112.891 against 112.517, ratio
1.003 — because most of the light now comes from a sky that did not move. The
frame *did* answer: a specular highlight crossed the bonnet and a flank went
from lit to shaded. So the instrument was replaced by a **mean per-pixel
|Δluma|**, which measures the thing the check was always about and is strictly
stronger: **5.06** where a mean-of-means saw 0.3%. Lowering the old threshold
would have kept a check that can no longer fail for the right reason. A second
assertion pins the outcome directly — mean luma > 40, against the 10.4 it was.

### 170.3 — OIT is not reachable from `GLTF_PBR_Renderer`, and the switch in the header is a trap

**Evaluated and rejected, with the line.** `PBR_Renderer::CreateInfo::OITLayerCount`
(`PBR_Renderer.hpp:252`) reads like a switch and is not one from this entry
point. Populating the layer buffer requires PSOs keyed
`RenderPassType::OITLayers`, and **`GLTF_PBR_Renderer.cpp:638` hardcodes
`RenderPassType::Main`** in the only `PSOKey` it builds — the only
`RenderPassType` reference in that whole file — with no `RenderInfo` field to
override it. The population pass lives in `HnRenderPass.cpp`,
`HnBeginOITPassTask` and `HnEndOITPassTask`, i.e. **behind the Hydrogent/USD
entry point only**. Reaching it from here means patching vendored source, which
**D26** forbids and **D40** restates.

Two further requirements, recorded so the next evaluation starts further along:
`OITLayerCount > 0` is silently forced back to 0 unless
`Features.ComputeShaders` is enabled (`PBR_Renderer.cpp:513-526`), and
`UpdateOITLayers.psh` needs `Features.PixelUAVWritesAndAtomics`, which DiligentFX
**does not check** — Tutorial29_OIT enables both explicitly.

**What replaces it is better and needs no new machinery**: the owner's two-pass
alpha-mode reclassification (their `AstonMartinScene::Render`). Pass 1 renders
every `BLEND` material as `MASK`, so its opaque texels draw with depth write and
depth test and occlude each other **by z-buffer — order-independent, no sort**;
its sub-cutoff texels are `discard`ed and leave no depth. Pass 2 restores
`BLEND` and resubmits: the opaque texels now fail the depth test against what
pass 1 wrote, so only the glass blends, over a finished image.

**Three things it does not do, said plainly rather than claimed away:**

- It **depends on the material's own `AlphaCutoff` separating the two
  populations.** On this asset it does — 97.8% of the diffuse map is alpha 255
  and the transparent texels cluster at 76 and 96 (≈0.30/0.38) against a 0.5
  cutoff. An asset whose glass sat at 0.6 would render its windows solid. The
  cutoff must be read from the material, never hardcoded.
- It **does not sort transparent surfaces against each other.** Two overlapping
  glass panes stay order-dependent. Much smaller than the whole car; not
  "transparency solved".
- The sample **mutates `Model::Materials[i].Attribs.AlphaMode` between passes**,
  which in the engine is loaded asset state touched every frame. Whether the
  material constant buffer must be re-uploaded between passes, or the PSO key
  alone carries the alpha mode, is the thing to check first: if the shader reads
  `AlphaMode`/`AlphaCutoff` out of the material cbuffer, a stale buffer means
  pass 1 silently does not alpha-test and the whole mechanism is a no-op that
  looks like it worked.

### 170.5's second half — the environment is a setting, not a constant

Turning a default sky on changed **12 of the 70 gpu cases**, and every one was
the same thing: a case that measures one lamp against one material and asserts
exact channel values, now reading a second light source it never asked for.
`lit.g == lit.b` fails under a blue-tinted sky; `dark.r < 20` fails because
nothing is black any more; a cel-shading case that pins the palette to ≤4
colours fails because an ambient term is continuous.

**None of those assertions was wrong, and none was re-baselined.**
`SceneRenderer::setEnvironmentIntensity` (forwarded by `SceneView` and
`SceneRenderLayer`) scales `IBLScale`, so `0` gives exactly the pre-environment
image at no per-draw cost and no pipeline rebuild. The affected cases turn it
off and keep testing what they were written to test — which also decouples them
from the sky's exact colour, so tuning the default sky later cannot break
thirteen unrelated files.

One case *was* re-baselined, deliberately, because it pins a number rather than
an image: the base signature's descriptor budget (T0161.7) goes **19 → 23
sampled images** — `g_PreintegratedGGX`, `g_IrradianceMap`,
`g_PrefilteredEnvMap`, `g_PreintegratedCharlie` — and **19 immutable samplers,
unchanged**, because all four share `g_LinearClampSampler` and upstream dedupes
immutable samplers by name (`PBR_Renderer.cpp:1262-1266`); the one entry they
want was already there from T0143's sheen LUT. That last number was written as
20 first and the suite said 19, which is the argument for pinning it rather than
reasoning about it.

### 170.2 — `GLTF_PBR_Renderer` **cannot** be the base class, and its `Render` cannot be the draw path

Both measured in source, and they change this ticket's mechanism rather than
its goal. Recorded here because the ticket, the board row and D26's amendment
all currently say otherwise.

**1. Subclassing it silently destroys the engine's signature.**
`PBR_Renderer`'s constructor takes `bool InitSignature = true` and calls
`CreateSignature()` — and therefore the **virtual** `CreateCustomSignature` —
from its own body, where the derived vtable does not exist yet.
`SurfacePipeline` handles this the way the flag exists for: it passes
`InitSignature = false` and calls `CreateSignature()` itself
(`SurfacePipeline.cpp:355`). **`GLTF_PBR_Renderer`'s constructor does not
forward that flag** (`GLTF_PBR_Renderer.cpp:107-111`) and offers no way to set
it. Deriving from it would call `CreateCustomSignature` through
`PBR_Renderer`'s vtable, and `g_HeightMap`, `g_SceneColour`, `g_SceneDepth` and
the six-sampler palette would simply not be in the signature — every custom
material shader in the engine stops compiling, with no diagnostic pointing
here.

**Upstream's own precedent says the same thing.** The ticket cites
`USD_Renderer` — and `USD_Renderer` derives from **`PBR_Renderer`**, not from
`GLTF_PBR_Renderer`, and passes `false, // InitSignature`
(`USD_Renderer.cpp:215`) for exactly this reason. `SurfacePipeline` is already
that precedent, followed.

**2. `GLTF_PBR_Renderer::Render` draws nothing under reverse-Z.** Its two PSO
caches are built in its constructor from a default-constructed
`GraphicsPipelineDesc` (`GLTF_PBR_Renderer.cpp:113-129`), whose depth
comparison is `COMPARISON_FUNC_LESS`, and its `CreateInfo` carries no depth
field. The engine clears depth to 0 and puts the near plane at 1 (**T0130**),
so `LESS` admits only fragments below 0 — a black frame, not inverted geometry.
`SceneRenderer.hpp` has documented this since T0134; it is still true.

**3. And it cannot see anything the engine adds.** `Render` reads materials
only from `GLTFModel.Materials`, so a `.hpmat` assignment, the missing-material
fallback, a custom Slang module, the per-object light selection and the
mirrored-node cull rule (**D33**/T0152.5) are all invisible to it. It hardcodes
`RenderPassType::Main`, so it cannot drive an OIT layers pass either.

**What this leaves.** The two things worth having from `GLTF_PBR_Renderer` are
`InitMaterialSRB` and `GetMaterialPSOFlags`, both currently inlined here — and
`InitMaterialSRB` reaches `GetPBRTextureSRV`, the per-slot sRGB view that is
file-static and unreachable today. Neither needs the base class: both are worth
one focused follow-up, not a base-class change that costs the signature.

**The route that does work is 170.1's**, and it is better than subclassing:
pre-seed the protected `m_VertexShaders` / `m_PixelShaders` caches with the
engine's Slang-compiled bytecode, then delegate to `PBR_Renderer::GetPSO` —
which skips compilation for an already-populated entry — with a
`GetPsoCacheAccessor(GraphicsDesc)` built from **the engine's own reverse-Z
pipeline description**. That keeps upstream's PSO construction, blend policy
and depth policy, gets `RenderPassType::OITLayers` compiled from upstream's own
`UpdateOITLayers.psh` for free, and never mentions `GLTF_PBR_Renderer`.

**D26's amendment therefore needs a second correction before it is final**: the
engine keeps the *submission walk* — it must, for reverse-Z, per-object lights,
material assignment and D33 — and hands upstream the *pipeline construction and
per-pass policy*. What the six missed capabilities actually needed was an audit
and adoption, which is what OIT and IBL are, not a transfer of the loop.

### The trap this ticket exists to close, stated so it cannot recur

**Every one of the six missed capabilities was invisible from inside our own loop.** That is the whole mechanism: owning the traversal means upstream's improvements arrive as code you never call, and the only way to notice is to go reading. Six times in three days is the measurement.

### What must not be lost

Five tickets went into giving game developers full shader power — `IHpMaterial`, the vertex hook, the light loop, screen intermediates, declared resources. **170.1 protects that, and it is first for that reason.** If the shader cannot be injected, stop and report: trading away custom materials to get the loop back is an owner decision, not an implementation detail.
