# T0134 — How far DiligentFX's PBR renderer goes, and what inherits it

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 445 |
| **Created** | 2026-08-05 |
| **Refs** | [../completed/0028-scene-draw-submission.md](../completed/0028-scene-draw-submission.md), [../completed/0060-material-system.md](../completed/0060-material-system.md), [../completed/0079-lighting-system.md](../completed/0079-lighting-system.md), [../open/0086-shadows.md](../open/0086-shadows.md), [0087-environment-lighting.md](0087-environment-lighting.md), [../open/0096-hdr-pipeline-and-tonemapping.md](../open/0096-hdr-pipeline-and-tonemapping.md), [../completed/0111-anti-aliasing-and-render-scale.md](../completed/0111-anti-aliasing-and-render-scale.md), [../completed/0023-asset-manager.md](../completed/0023-asset-manager.md), [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) (D6, D21, D22, **D24 — this ticket**) |

## Why

T0028 had to turn a `Diligent::GLTF::Model*` into pixels, and could not do that
without picking something. It picked **DiligentFX's `GLTF_PBR_Renderer`, driven
with IBL, lights, tonemapping, shadows and motion vectors masked off**, and
that choice reaches much further than T0028's scope:
T0060 (materials), T0079 (lighting), T0087 (IBL) and T0096 (HDR/tonemapping) all
inherit it.

**This ticket exists so they inherit an argument rather than an accident.** The
survey below was done on 2026-08-05 during T0028 and is recorded here, where the
tickets that must live with it will look, instead of being buried in the notes of
a ticket about draw submission.

The narrow question T0028 answered — "what draws a mesh so the frame path can be
built" — is not the same as the broad one this ticket owns: **how much of
DiligentFX's PBR path the engine actually adopts**, and what it writes itself.

## Done when

- [x] The adoption boundary is written down: which of DiligentFX's PBR features
      the engine uses, and which it replaces — see "The adoption boundary (134.2)", promoted to **D24**
- [x] T0060's material model is reconciled with `PBR_Renderer`'s material
      attribs — **materials map onto them**; `PBRMaterialShaderAttribs` is the
      engine's material vocabulary and T0060 carries the obligation
- [x] T0079 and T0087 know whether they configure DiligentFX's lighting and IBL
      or supersede them — **both configure**, with the exact mechanism named on
      each ticket. `PBRLightAttribs` already specifies T0079's light model
- [x] T0096's HDR/tonemapping decision accounts for what DiligentFX actually
      ships — **and the premise of this clause was wrong**: there is no standalone
      `ToneMapping` component. See "Correction: tonemapping does **not** exist
      twice". The real fork is in-shader versus a pass, and a pass is recommended
- [x] The feature-masked stopgap T0028 leaves behind is either promoted to the
      real path or removed — **promoted**; `kFeatureMask` is now the boundary
      stated in code, and each bit comes off when its ticket lands

## Subtasks

- [x] 134.1 Enumerate what `PBR_Renderer` / `GLTF_PBR_Renderer` actually provide
      — 19 `CreateInfo` toggles, 39 `PSO_FLAGS`, the light and frame layouts, and
      the six `PostProcess` components
- [x] 134.2 Decide the boundary, and record what is rejected and why — **D24**
- [x] 134.3 Reconcile with T0060's material assets
- [x] 134.4 Reconcile with T0079/T0087 lighting and IBL
- [x] 134.5 Reconcile with T0096 tonemapping — the "it has its own" premise was false; corrected and re-answered
- [x] 134.6 Resolve T0028's feature-masked stopgap — promoted, **and it uncovered an unwritten `PBRFrameAttribs::Renderer`**, fixed here

## Notes / findings

### The survey T0028 did, recorded here (2026-08-05)

**Checked before choosing, per the don't-reinvent rule in `CLAUDE.md`.** The
answer was sitting in `third_party/`, which is the failure mode T0053's
`entt::meta` miss is the worked example of.

- `DiligentFX` is `add_library(DiligentFX STATIC ...)` and was **already
  configured in this build** before T0028 — **D7** records that EnTT reaches the
  engine through DiligentFX's own `FetchContent`. The subtree was never new,
  only unlinked.
- `hp_engine` previously linked the two RHI backends, `Diligent-Common`, and
  `Diligent-TextureLoader` / `Diligent-AssetLoader` PRIVATE. Adding FX is a
  **real linkage change**, not a no-op.
- DiligentFX links `Diligent-Imgui` **PUBLIC**. **D6 already accounts for this**
  and explicitly corrects the assumption it would otherwise invalidate: ImGui is
  linked into the *runtime* binary too, not only the editor. So adoption spends a
  decision already made rather than a new one.

**Reverse-Z compatibility — the thing that would have disqualified it, measured
rather than assumed.** T0130 mandates `COMPARISON_FUNC_GREATER_EQUAL`, and a
renderer hardcoding `LESS_EQUAL` would produce distant geometry occluding near
geometry — which reads as a broken mesh import and gets investigated in entirely
the wrong place.

It does not hardcode it. `PBR_Renderer::GetPsoCacheAccessor(const GraphicsPipelineDesc&)`
takes the **caller's** pipeline desc, and every depth write in `PBR_Renderer.cpp`
touches only `DepthEnable` and `DepthWriteEnable` — for the env-map and OIT
passes — and **never `DepthFunc`**. The depth comparison stays the caller's.

**This is true of `PBR_Renderer` and does NOT carry to `GLTF_PBR_Renderer` — see
the correction below.** Left standing because it is correct about the base class,
and because the error was generalising from base to derived without checking.

### Correction, and it is the important one: `GLTF_PBR_Renderer` **cannot** do reverse-Z (2026-08-05)

The reverse-Z finding above is **true of `PBR_Renderer` and false of
`GLTF_PBR_Renderer`**, and the difference decides the design. Recorded in full
because the earlier, wrong conclusion was written down first and acted on.

`PBR_Renderer::GetPsoCacheAccessor(const GraphicsPipelineDesc&)` is public and
does take the caller's desc — that part was verified correctly. But
`GLTF_PBR_Renderer` never lets a caller near it. Its constructor
(`GLTF_PBR_Renderer.cpp:108-130`) builds the desc itself:

```cpp
GraphicsPipelineDesc GraphicsDesc;
GraphicsDesc.NumRenderTargets = CI.NumRenderTargets;
GraphicsDesc.RTVFormats[i]    = CI.RTVFormats[i];
GraphicsDesc.DSVFormat        = CI.DSVFormat;
GraphicsDesc.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
GraphicsDesc.RasterizerDesc.FrontCounterClockwise = CI.FrontCounterClockwise;
m_PbrPSOCache = GetPsoCacheAccessor(GraphicsDesc);   // DepthStencilDesc untouched
```

`DepthStencilDesc` is never assigned, so it takes Diligent's default —
`DepthFunc = COMPARISON_FUNC_LESS` (`DepthStencilState.h:173`). Neither
`GLTF_PBR_Renderer::CreateInfo` nor `PBR_Renderer::CreateInfo` carries **any**
depth-state field; the only depth-adjacent members are the formats. And
`m_PbrPSOCache` is **private** (`GLTF_PBR_Renderer.hpp:285`, under the `private:`
at 269), so a subclass cannot re-seed it either.

**What that produces under T0130's convention is worth stating precisely**, since
it is not the failure the ticket warns about. Reverse-Z clears depth to 0 and maps
the near plane to 1. With `LESS`, a fragment passes only if its depth is *less
than* the stored 0 — so **nothing draws at all**. A black screen, not inverted
geometry. Louder than the classic reverse-Z bug, and quicker to diagnose, but a
hard blocker.

**Decided 2026-08-05: drive `PBR_Renderer` directly** and supply our own
`GraphicsPipelineDesc` with `COMPARISON_FUNC_GREATER_EQUAL`.

This keeps the reuse that actually mattered — the shaders, PSO creation and
caching, SRB management, material attribs — and writes only the model traversal
that `GLTF_PBR_Renderer::Render` performs. Rejected alternatives, with reasons:

- **Patch the vendored Diligent** to expose depth state. Surgical, and genuinely
  worth upstreaming since reverse-Z is standard practice — but it ends the
  unpatched-tree property D6 deliberately preserved, and every upgrade carries
  the patch until it merges.
- **Revisit T0130.** Cheapest today and wrong: its measurement is 135,604
  distinct depth values against 2 over 900–901m with a 0.1m near plane, and the
  whole point of settling the convention early is that changing it later means
  touching every pipeline state in the engine.
- **A minimal hand-written pipeline.** This finding is real evidence for it, but
  it discards the shaders and material handling too, which is far more than the
  traversal.

**The traversal is not pure loss.** It is the seam where per-entity material
overrides (T0060) and per-draw sorting (T0045) have to live anyway, so owning it
was likely on the cards regardless.

### Correction: `PSO_FLAG_UNSHADED` is not the lever (2026-08-05)

Recorded because the wrong version was written down first, and it is the version
a reader would naturally assume from the flag's name.

In `GLTF_PBR_Renderer::Render` (`GLTF_PBR_Renderer.cpp:617-635`) the PSO flags are
accumulated from the material and vertex attributes, then:

```cpp
PSOFlags &= RenderParams.Flags;              // caller's flags are a MASK
if (RenderParams.Wireframe)
    PSOFlags |= PSO_FLAG_UNSHADED;           // the only way UNSHADED is set
```

So the caller's `RenderInfo::Flags` can only **remove** flags, never add one, and
`PSO_FLAG_UNSHADED` is reachable **only as wireframe**. Passing it in
`RenderInfo::Flags` does not enable it — it masks nearly everything else off
instead.

Two related facts for 134.1:

- `PSO_FLAG_DEFAULT` (`PBR_Renderer.hpp:606`) **includes**
  `USE_IBL | USE_LIGHTS | ENABLE_TONE_MAPPING`. The default is the opposite of
  minimal, so anything wanting a plain path must mask deliberately.
- When `PSO_FLAG_UNSHADED` *is* set, `PBR_Renderer.cpp:123-131` reduces the flags
  to `USE_JOINTS | ALL_USER_DEFINED | UNSHADED` and forces alpha to opaque. The
  renderer enforces the stripping itself rather than merely allowing it — useful
  if a genuine unlit pass is ever wanted.

T0028's actual configuration is therefore `CreateInfo::EnableIBL = false` plus a
`RenderInfo::Flags` mask excluding `USE_IBL`, `USE_LIGHTS`, `ENABLE_TONE_MAPPING`,
`ENABLE_SHADOWS` and `COMPUTE_MOTION_VECTORS` — which are exactly the five
features owned by T0079, T0087 and T0096.

### Why no abstraction was built over it, and what forbids one

The obvious instinct is an `IMeshRenderBackend` interface so the renderer can be
swapped. **D22 forbids it**, in terms that apply directly:

> an abstraction over an abstraction buys nothing until there is a concrete
> second target.

An interface with exactly one implementation costs indirection at every call site
and buys nothing until a second renderer exists. Building one speculatively is a
deliberate change to D22, not a free precaution.

**The modularity is structural instead, and it is already enforced.**
`engine/CMakeLists.txt:194` states the rule: Diligent is linked **PRIVATE unless
a public engine header names a Diligent type**, and every engine class is pimpl'd
(`MeshAsset`, `AssetPool`, `FrameTargets`, `RenderStack` all carry
`struct Impl`). So the renderer lives inside one `.cpp` behind an `Impl`, and
replacing it touches that file. That is the swappability, and it costs nothing.

### The counterintuitive finding: writing our own would couple us *more*

Recorded because it reverses the intuition, and the intuition is what a future
reader will arrive with.

`MeshAsset` holds a `Diligent::GLTF::Model*` — T0023's choice. That model's
materials, textures and vertex layout are Diligent's glTF structures. A
hand-written pipeline does not avoid Diligent; it means **the engine** walks
those internals — nodes, primitives, material indices, attribute layout — and
depends on their in-memory shape across upgrades. DiligentFX's renderer is
written against exactly that structure and moves with it.

So "write it ourselves for independence" buys tighter coupling to Diligent's
internals, spread across code this repository maintains.

### Where the real lock-in already is, and it is not the renderer

`engine/include/hp/Assets.hpp:368` — `MeshAsset::model()` returns
`Diligent::GLTF::Model*` **in a public header**. That is the genuine
architectural commitment, and **T0023 made it, not T0028**.

If the renderer is ever seriously changed, that is the seam that hurts — not the
draw call. Worth knowing because it argues against speculative abstraction from
the other direction: the expensive coupling is in the asset representation, and a
renderer interface would not touch it.

### 134.1 — the enumeration, read from the vendored source (2026-08-05)

`PBR_Renderer::CreateInfo` carries **nineteen** feature toggles. Read rather than
recalled, because "what it can do" decides what four other tickets do not have to
build:

| Group | Fields |
|---|---|
| Core shading | `EnableIBL`, `EnableAO`, `EnableEmissive`, `EnableShadows` |
| Extended materials | `EnableClearCoat`, `EnableSheen`, `EnableAnisotropy`, `EnableIridescence`, `EnableTransmission`, `EnableVolume` |
| Capacity | `MaxLightCount` (16), `MaxShadowCastingLightCount` (8), `MaxJointCount` (64), `OITLayerCount` (0), `PCFKernelSize` (3), `NumBRDFSamples` (512) |
| Packing / layout | `PackMatrixRowMajor`, `PackVertexNormals`, `PackVertexColors`, `UseSkinPreTransform`, `UseSeparateMetallicRoughnessTextures` |
| Convenience | `CreateDefaultTextures`, `AllowHotShaderReload` |

`PSO_FLAGS` is a **64-bit** mask with 39 defined bits — 17 texture slots, 6
vertex-attribute bits, 6 extended-material enables, and ten renderer-level ones
(`USE_IBL`, `USE_LIGHTS`, `USE_TEXTURE_ATLAS`, `ENABLE_TEXCOORD_TRANSFORM`,
`CONVERT_OUTPUT_TO_SRGB`, `ENABLE_CUSTOM_DATA_OUTPUT`, `ENABLE_TONE_MAPPING`,
`UNSHADED`, `COMPUTE_MOTION_VECTORS`, `ENABLE_SHADOWS`).

**The light model is already fully specified, and this is the finding that most
changes another ticket.** `PBRLightAttribs`
(`Shaders/PBR/public/PBR_Structures.fxh:309`) is:

```c
int   Type;            // 1 directional, 2 point, 3 spot
float PosX, PosY, PosZ;
float DirectionX, DirectionY, DirectionZ;
int   ShadowMapIndex;  // -1 if this light casts none
float IntensityR, IntensityG, IntensityB;
float Range4;          // point/spot range to the fourth power
float SpotAngleScale;  // 1 / (cos(inner) - cos(outer))
float SpotAngleOffset; // -cos(outer) * SpotAngleScale
```

That is **T0079's entire "directional, point, spot, with colour, intensity,
range" Done-when, already decided** — as a shader-side representation, with
`GLTF_PBR_Renderer::WritePBRLightShaderAttribs` to fill it. That helper is
**`static`**, so it is usable from our own traversal exactly as
`WritePBRMaterialShaderAttribs` and `WritePBRPrimitiveShaderAttribs` already are.

The frame buffer's layout is fixed and computed by
`GetPRBFrameAttribsSize(LightCount, ShadowCastingLightCount)`:

```
CameraAttribs * 2 + PBRRendererShaderParameters
                  + PBRLightAttribs * LightCount
                  + PBRShadowMapInfo * ShadowCastingLightCount
```

**Post-process components that ship and are referenced by nothing**:
`PostProcess/` contains `Bloom`, `DepthOfField`, `ScreenSpaceAmbientOcclusion`,
`ScreenSpaceReflection`, `TemporalAntiAliasing` and `EpipolarLightScattering`.
TAA there is directly T0111's business; Bloom and DoF are T0096's and T0130.5's.

### The bug this ticket found: `PBRFrameAttribs::Renderer` was never written

**A live defect in shipped code, not a design question.** `SceneRenderer` mapped
the frame-attribs buffer with `MAP_FLAG_DISCARD` — contents undefined — wrote
`Camera` and `PrevCamera`, and **never touched `Renderer`**. `grep -c
"frame->Renderer"` returned **0**.

`PBRRendererShaderParameters` is not optional decoration. `RenderPBR.psh` reads
`g_Frame.Renderer.MipBias` **unconditionally**, in `ReadBaseLayerProperties`
(lines 147-148), on **every texture sample of every draw**, and also reads
`OcclusionStrength` (311), `EmissionScale` (312), `IBLScale`, `LightCount`,
`AverageLogLum`, `MiddleGray` and `WhitePoint`.

**Why nothing showed it.** Every mesh drawn so far has no textures at all, so
each sample hits the renderer's 1x1 default textures — where an arbitrary mip
bias selects the only mip that exists. The first textured material would have
produced randomly blurred surfaces, varying frame to frame, with nothing pointing
at the cause. It is the same shape as T0028's three silent bugs: no error, no
validation warning, and a plausible-looking frame.

Fixed by value-initialising the struct and calling Diligent's own
`PBR_Renderer::SetInternalShaderParameters` — which exists precisely for this and
was simply not called — then setting the fields `GLTFViewer.cpp:1281` sets.
`LightCount` is explicitly **0** until T0079.

**Not covered by a test, and that is stated rather than hidden.** The failure is
uninitialised memory whose observable effect needs a *textured* material, and
there is no texture-loading path in a test yet. The guard that would catch a
regression is a textured mesh rendered and its pixels asserted — which is
T0060's or T0097's to write, and is recorded on both.

### Correction: tonemapping does **not** exist twice

**Recorded because the wrong version was written down first, and because the
evidence in the tree actively invites it.** Two paths both look like
tonemapping implementations:

- `DiligentFX/Components/interface/ToneMapping.hpp`
- `DiligentFX/Shaders/PostProcess/ToneMapping/`

The natural reading — *"there is an in-shader tonemap via
`PSO_FLAG_ENABLE_TONE_MAPPING` **and** a standalone post-process component, so
enabling both would double-apply"* — is **wrong**, and it is wrong in a way that
would have sent T0096 looking for a conflict that does not exist.

What is actually there:

- `Components/ToneMapping.hpp` declares exactly **two functions**:
  `ReverseExpToneMap` and `ToneMappingUpdateUI`. It is a UI/helper header — which
  is why **D6** lists it among the ImGui-calling components — **not** a render
  pass.
- `Shaders/PostProcess/ToneMapping/` holds only `ToneMapping.fxh` and
  `ToneMappingStructures.fxh`. A shader **include**, not a pass. It is under
  `PostProcess/` because it is shared, not because a post-process stage uses it.
- `PostProcess/` on the C++ side has **no `ToneMapping` directory at all** —
  only Bloom, DepthOfField, EpipolarLightScattering, SSAO, SSR and TAA.

**Tonemapping in DiligentFX is a PBR-shader feature and nothing else.**
`RenderPBR.psh:530-540`, under `#if ENABLE_TONE_MAPPING`, calls
`ToneMap(OutColor.rgb, TMAttribs, g_Frame.Renderer.AverageLogLum)` with
`TONE_MAPPING_MODE` as a compile-time define. Twelve modes exist
(`TONE_MAPPING_MODE_NONE` through `_COMMERCE`, including AgX and PBR-neutral).

**The real either/or for T0096, correctly stated.** There is no double-apply
risk; there is a genuine architectural fork:

- **In-shader** (`PSO_FLAG_ENABLE_TONE_MAPPING`): no extra pass, no HDR target
  needed. But it tonemaps **per draw, before blending**, which breaks
  transparency and makes bloom impossible — bloom needs the HDR image, and
  DiligentFX ships Bloom as a `PostProcess` component that would have nothing to
  read.
- **A tonemap pass over an HDR target**: what `TargetFormat::ColourHDR`
  (`RGBA16_FLOAT`) already exists for, and the only option compatible with Bloom
  and with correct transparency. `AverageLogLum` being a frame parameter is the
  hook for auto-exposure, which **T0130.4** already decided lives on the camera.

**Recommended to T0096: do not set `PSO_FLAG_ENABLE_TONE_MAPPING`.** Render
linear to `ColourHDR` and tonemap in a pass. The in-shader path is the cheap
option and it forecloses bloom.

**One thing the current architecture already gets right by accident**, worth
knowing before anyone "fixes" it: T0027's notes worry that tonemapping must apply
to the world and not to UI composited over it. Because tonemapping here happens
inside the world layer's own pixel shader, a HUD layer drawn afterwards is
untouched by it. A tonemap **pass** must preserve that property — it belongs
between the world layers and the UI layers, which is exactly what `IRenderLayer::order`
exists to express.

### The adoption boundary (134.2)

**Adopted, and this is the commitment:**

| What | Why |
|---|---|
| `PBR_Renderer` — shaders, PSO creation and caching, SRB management | The reason to use DiligentFX at all. Replacing it means writing a PBR shader suite. |
| Its shader-side data model — `PBRFrameAttribs`, `CameraAttribs`, `PBRLightAttribs`, `PBRMaterialShaderAttribs` | This is the engine's shader vocabulary now. T0079 and T0060 populate these rather than inventing parallel structs. |
| The `static` writer helpers on `GLTF_PBR_Renderer` | Usable without the derived renderer, and they encode the packing rules. Reimplementing them is how a layout drifts silently. |
| `CreateDefaultTextures` | T0028.2's "a mesh with no material is still visible", for free. |

**Owned by the engine, not DiligentFX:**

| What | Why |
|---|---|
| The `GraphicsPipelineDesc` | Reverse-Z (T0130). This is the whole reason `GLTF_PBR_Renderer` is unusable. |
| The model traversal | Forced by the above — and it is where T0060's per-entity material overrides and T0045's sorting must live anyway. |
| Frame-attribs population | We own the camera (T0081) and, as of this ticket, the renderer parameters. |

**Deferred, with the mechanism named so the owning ticket does not survey again:**

| Feature | Ticket | Mechanism |
|---|---|---|
| Punctual lights | T0079 | `MaxLightCount`, `PBRLightAttribs`, `WritePBRLightShaderAttribs`, `Renderer.LightCount`, `PSO_FLAG_USE_LIGHTS` |
| Shadows | T0086 | `EnableShadows`, `MaxShadowCastingLightCount`, `PBRShadowMapInfo`, `PCFKernelSize`, `ShadowMapManager` |
| IBL / environment | T0087 | `EnableIBL`, `PrecomputeCubemaps`, irradiance + prefiltered cube, `IBLScale`, `PrefilteredCubeLastMip` |
| HDR + tonemapping | T0096 | **A pass, not `PSO_FLAG_ENABLE_TONE_MAPPING`** — see the correction above |
| Motion vectors | T0111 / T0096 | `PSO_FLAG_COMPUTE_MOTION_VECTORS`, `PrevCamera` (already written each frame) |
| Post-process | T0096 / T0111 | Bloom, DepthOfField, SSAO, SSR, TemporalAntiAliasing |
| Ambient occlusion (material) | T0079 | `EnableAO`, `Renderer.OcclusionStrength` |
| Emissive | T0060 | `EnableEmissive`, `Renderer.EmissionScale` |

**Rejected, and these are the ones worth arguing:**

- **`GLTF_PBR_Renderer`** — cannot do reverse-Z; see the correction above.
- **`USD_Renderer`, `Hydrogent`, `Radient`** — whole subsystems for USD/Hydra
  scene description. This engine's asset model is glTF through T0023, and
  adopting a USD stage model would be a far larger decision than a renderer.
- **Extended materials** — clearcoat, sheen, anisotropy, iridescence,
  transmission, volume. Each is a `CreateInfo` toggle plus texture slots plus
  shader permutations, and **none is free**: they widen the PSO permutation space
  and the material attribs buffer whether or not a material uses them. Off until
  a ticket asks by name. T0060 decides.
- **OIT** — `OITLayerCount = 0`. Order-independent transparency is a transparency
  *design*, not a flag, and nothing has designed transparency yet.
- **`EpipolarLightScattering`** — atmospheric scattering, only meaningful for
  outdoor scenes. The old `terrain_lab` used it; nothing here does.
- **An `IMeshRenderBackend` abstraction** — **D22**. Already argued below.

### 134.6 — the stopgap, resolved

`kFeatureMask` **stays**, and is promoted from stopgap to the decided boundary.
It was written as "the features this ticket does not enable"; it is now "the
features whose tickets have not landed", which is the same mask with a different
status. It survives as the single place that states the boundary in code, and
each bit comes off when its ticket lands — which is a better shape than the
alternative of scattering the decision across `CreateInfo` and the flags mask.

`GetMaterialPSOFlags` stays inlined and constant-folded, with its `static_assert`
guard intact: the moment `EnableAO`, `EnableEmissive` or any extended-material
setting is turned on, it must go back to consulting the material. That is now
recorded against the tickets that will turn them on, rather than only in a
comment.

## Cross-ticket obligations

These are recorded on the target tickets too, so the linkage reads both ways:

- **T0060** — the material system must reconcile with `PBR_Renderer`'s material
  attribs, or diverge deliberately. Do not design materials without reading
  134.1.
- **T0079 / T0087** — decide whether to configure DiligentFX's lighting and IBL
  or supersede them, rather than discovering the overlap mid-implementation.
- **T0096** — DiligentFX ships its own `ToneMapping` component (D6 lists it among
  the ImGui-calling ones). The HDR policy must say whether that is used.
- **T0028** — leaves a feature-masked stopgap that this ticket resolves; see
  the correction below for why it is a mask and not `PSO_FLAG_UNSHADED`.
