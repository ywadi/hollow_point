# T0170 — DiligentEngine owns the render loop; the engine owns the shader

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 459 |
| **Created** | 2026-08-08 |
| **Blocked by** | nothing |
| **Refs** | **D26** — **this ticket amends it**; [0045-culling-and-render-queues.md](0045-culling-and-render-queues.md) — **superseded in large part**: OIT replaces the back-to-front sort, and upstream's bucketing replaces the queue split; what survives is frustum culling and batching; [0087-environment-lighting.md](0087-environment-lighting.md) — **probably subsumed**: `PBR_Renderer` ships IBL, so "configure or supersede" answers itself; [../completed/0147-engine-intermediates-for-shaders.md](../completed/0147-engine-intermediates-for-shaders.md) / **D37** — the 10.9b snapshot survives, but via `RenderInfo::AlphaModes` rather than our own split; [../completed/0161-game-resource-model.md](../completed/0161-game-resource-model.md) — moves onto `CreateCustomSignature`; [../completed/0145-lighting-stage-own-the-light-loop.md](../completed/0145-lighting-stage-own-the-light-loop.md) / **D30** — records that the lighting hook was to be offered upstream and never was; that debt is now on the critical path; [../completed/0141-custom-shader-materials.md](../completed/0141-custom-shader-materials.md) — the finding D26 rests on, **still true and narrower than what was built on it**; [../completed/0142-slang-shader-language.md](../completed/0142-slang-shader-language.md), [../completed/0166-tangent-frames-and-real-assets.md](../completed/0166-tangent-frames-and-real-assets.md), [0168-asset-import-coverage.md](../completed/0168-asset-import-coverage.md); **D12**, **D27**, **D35** |

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
- [ ] 170.3 **Turn OIT on** — `OITLayerCount`, `CreateOITResources`, `CreateClearOITLayersSRB`, `SetOITResources`, and the `OITLayers` pass. Read how **Hydrogent** sequences it (`HnRenderRprimsTask`, `HnEndOITPassTask`) and copy that sequence
- [ ] 170.4 **D37's snapshot via `RenderInfo::AlphaModes`**, deleting the hand-rolled 10.9a/c split
- [ ] 170.5 **Turn IBL on** and give the car an environment. This is what makes it look like the DCC preview — its paint colour is authored into the *specular* map and there is currently nothing to reflect
- [ ] 170.6 **Delete what is now upstream's.** Measure the line count before and after; a refactor that adds code has gone wrong
- [ ] 170.7 **Amend D26**, and write the boundary document
- [ ] 170.8 **Re-scope T0045 and T0087** on their own tickets — say what survives and what this absorbed

## Not in scope

- **The RHI.** Device, swapchain and context are already Diligent's and unaffected.
- **Import.** T0168/T0169 landed; this is the draw path only.
- **Gameplay-facing API.** `hp::SceneRenderer`'s public surface should not move — this is an implementation change (**D12**: gameplay is in lockstep, so a header change costs a rebuild of every module).

## Notes / findings

### The trap this ticket exists to close, stated so it cannot recur

**Every one of the six missed capabilities was invisible from inside our own loop.** That is the whole mechanism: owning the traversal means upstream's improvements arrive as code you never call, and the only way to notice is to go reading. Six times in three days is the measurement.

### What must not be lost

Five tickets went into giving game developers full shader power — `IHpMaterial`, the vertex hook, the light loop, screen intermediates, declared resources. **170.1 protects that, and it is first for that reason.** If the shader cannot be injected, stop and report: trading away custom materials to get the loop back is an owner decision, not an implementation detail.
