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
in unshaded mode**, and that choice reaches much further than T0028's scope:
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
- [ ] The unshaded-mode stopgap T0028 leaves behind is either promoted to the
      real path or removed

## Subtasks

- [ ] 134.1 Enumerate what `PBR_Renderer` / `GLTF_PBR_Renderer` actually provide
      — materials, lights, IBL, OIT, shadows — before deciding what to keep
- [ ] 134.2 Decide the boundary, and record what is rejected and why
- [ ] 134.3 Reconcile with T0060's material assets
- [ ] 134.4 Reconcile with T0079/T0087 lighting and IBL
- [ ] 134.5 Reconcile with T0096 tonemapping, given DiligentFX has its own
- [ ] 134.6 Resolve T0028's unshaded stopgap

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
- **T0028** — leaves an unshaded-mode stopgap that this ticket resolves.
