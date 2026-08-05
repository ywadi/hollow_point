# T0134 — How far DiligentFX's PBR renderer goes, and what inherits it

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 445 |
| **Created** | 2026-08-05 |
| **Refs** | [../inprogress/0028-scene-draw-submission.md](../inprogress/0028-scene-draw-submission.md), T0060, T0079, T0087, T0096, [../completed/0023-asset-manager.md](../completed/0023-asset-manager.md), [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) (D6, D21, D22) |

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

- [ ] The adoption boundary is written down: which of DiligentFX's PBR features
      the engine uses, and which it replaces
- [ ] T0060's material model is reconciled with `PBR_Renderer`'s material
      attribs — either materials map onto them, or the divergence is deliberate
      and recorded
- [ ] T0079 and T0087 know whether they configure DiligentFX's lighting and IBL
      or supersede them
- [ ] T0096's HDR/tonemapping decision accounts for DiligentFX shipping its own
      `ToneMapping` component
- [ ] The feature-masked stopgap T0028 leaves behind is either promoted to the
      real path or removed

## Subtasks

- [ ] 134.1 Enumerate what `PBR_Renderer` / `GLTF_PBR_Renderer` actually provide
      — materials, lights, IBL, OIT, shadows — before deciding what to keep
- [ ] 134.2 Decide the boundary, and record what is rejected and why
- [ ] 134.3 Reconcile with T0060's material assets
- [ ] 134.4 Reconcile with T0079/T0087 lighting and IBL
- [ ] 134.5 Reconcile with T0096 tonemapping, given DiligentFX has its own
- [ ] 134.6 Resolve T0028's feature-masked stopgap

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
