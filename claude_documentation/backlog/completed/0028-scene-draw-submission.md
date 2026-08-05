# T0028 — Scene draw submission and the frame-rendered event

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 410 |
| **Created** | 2026-08-02 |
| **Refs** | T0120, [../open/0033-viewport-panel.md](../open/0033-viewport-panel.md), [../completed/0134-pbr-renderer-adoption.md](../completed/0134-pbr-renderer-adoption.md), [../completed/0130-camera-lens-model.md](../completed/0130-camera-lens-model.md), [../completed/0081-camera-system.md](../completed/0081-camera-system.md), [0027-render-stack.md](0027-render-stack.md), [../inprogress/0079-lighting-system.md](../inprogress/0079-lighting-system.md) — **see the caveat below on what this ticket's pixel evidence actually proves** |

## Why

"Render the scene" only becomes a well-defined operation once the scene and asset
model exist. This turns entities into draw calls, and publishes the result so the
editor viewport can display it without the renderer knowing the editor exists.

## Done when

- [x] Entities with transform + mesh are collected and drawn — **pixels read
      back and asserted**, both backends
- [x] A scene with no camera renders nothing and says so, rather than crashing
- [x] Entities with no material get a visible default — the drawn quad has no
      textures at all and samples the renderer's defaults
- [x] Output goes to an offscreen target, not straight to the swap chain
- [x] A "new frame rendered" event carries that texture to any listener — and
      the editor publishes one every frame
- [x] Assets resolve from the pool by GUID (T0023) — the *success* path, not
      just the missing-asset path

## Subtasks

- [x] 28.1 Parse step: filter to entities with transform + mesh — **done**, see
      "The parse step" below. The *require a camera* half belongs to the submit
      path and is tracked under 28.4/28.5, since the parse step deliberately
      takes no camera
- [x] 28.2 Default material for meshes without one — `CreateDefaultTextures`,
      exercised by a mesh with no textures rendering visibly
- [x] 28.3 Resolve mesh/material GUIDs against the asset pool — mesh resolves
      through `AssetPool::get<MeshAsset>`; **material GUIDs are not yet resolved**,
      because there is no material asset type until T0060
- [x] 28.4 Render to an offscreen target sized to the viewport — `SceneView`,
      GPU-verified on both backends
- [x] 28.5 Emit the frame-rendered event with the texture handle — the event
      type, its dispatch and its consumption semantics are built and tested, and
      the editor's `SceneLayer` publishes one per frame
- [x] 28.6 Profiling zones for parse and submit separately — `HP_PROFILE_ZONE`
      in `parseScene`, in `render`, in `drawModel`, and a named zone around the
      frame-attribs write

## Closed — 2026-08-05

**All six "Done when" clauses met and all six subtasks done**, with a mesh
rendered to a target and the pixels asserted, on both backends.

Two remainders moved to the tickets that own them rather than being left here,
the T0095 → T0105 pattern. **Both are recorded on the receiving ticket, so the
linkage reads both ways:**

- **The dev present path cannot show a frame on Vulkan → [T0033](../open/0033-viewport-panel.md).**
  `RenderLayer::setPresentSource` is a plain `CopyTexture` and needs an exact
  format match; Vulkan's surface here is `BGRA8_UNORM_SRGB` against the scene
  target's `RGBA8_UNORM_SRGB`. It shows a clear colour rather than copying
  regardless, because a red/blue-swapped image reads as a shader bug. **T0033
  deletes this path entirely** — an ImGui image samples the texture in a shader,
  which converts formats as a matter of course — so the gap closes there rather
  than needing a fix first. The alternative, if T0033 is far off, is a
  fullscreen-triangle blit, which is also what a compositor does and would then
  belong to T0027 rather than being written twice.
- **Material texture colour-space conversion → [T0134](../completed/0134-pbr-renderer-adoption.md).**
  `GetPBRTextureSRV` is not public, so textures bind through the model's own
  views with no conversion applied. Untextured materials are unaffected, which is
  why this ticket's own tests pass. It sits next to T0097's sRGB work.

**Not moved, because they were never this ticket's:** the runtime publishing a
frame is T0042's (it can now copy the editor's `SceneLayer` verbatim), material
*asset* resolution waits on T0060 for a material type to exist, and sorting,
culling and instancing were out of scope by design — the parse step's output is
an explicit list precisely so T0045 can insert a pass without restructuring
anything.

## Notes / findings

### Survey before writing anything (2026-08-05) — what already exists

Read rather than assumed, because three of the six subtasks turned out to be
wiring rather than building:

| Need | Already there |
|---|---|
| Entities to draw | `MeshRenderer{Guid mesh, Guid material}` — `Scene.hpp:140`. Both **GUIDs, never pointers**, which is what 28.3 resolves |
| World matrix | `WorldTransform` (T0101), dirty-tracked |
| Camera | `resolveCamera(scene, viewSlot)` + `buildView(...)` → `ResolvedView` (T0081) |
| Asset lookup | `AssetPool::get<MeshAsset>(guid)` → `shared_ptr`, null when absent or wrong type |
| The mesh itself | `MeshAsset::model()` → `Diligent::GLTF::Model*` (T0023) |
| Compositing | `RenderStack` + `IRenderLayer::onRenderLayer(RenderPassContext&)` (T0027) |
| Offscreen target | `FrameTargets::declare/create/resize` (T0046) |
| Device + clip space | `RenderLayer::device()`, `context()`, `clipSpace()` (T0025) |

`MeshRenderer`'s comments already anticipate 28.2: a default-constructed mesh
GUID means "nothing to draw" and is **a legitimate state, not an error**, and a
default material GUID means "the renderer's fallback material, so a mesh with no
material assigned is still visible rather than silently absent."

**Nothing wires `RenderLayer` to `RenderStack` or `FrameTargets` today.**
`RenderLayer` exposes the device and clip space and owns the swap chain; it holds
no stack and no targets. The editor pushes a `RenderLayer` and sets a clear
colour (`EditorMain.cpp:105`), and that is the whole render path. So this ticket
owns that wiring, not just the parse and submit.

### The one architecture decision this ticket has to make: how a mesh is drawn

`MeshAsset` holds a `Diligent::GLTF::Model*`. Turning that into pixels needs
either DiligentFX's `GLTF_PBR_Renderer` — which consumes exactly that type — or
a pipeline written here. **Checked before choosing, per the don't-reinvent rule:**

- `DiligentFX` is `add_library(DiligentFX STATIC ...)` and is **already
  configured in this build** — D7 records that EnTT reaches the engine through
  DiligentFX's `FetchContent`, so the subtree is not new, only unlinked.
- **`hp_engine` does not link DiligentFX today.** It links the two RHI backends,
  `Diligent-Common`, and `Diligent-TextureLoader` / `Diligent-AssetLoader`
  PRIVATE (the parsers, T0023.3). Adding FX is a real linkage change.
- DiligentFX links `Diligent-Imgui` **PUBLIC**. **D6 already accounts for this**
  and corrects the assumption it invalidates: ImGui is linked into the *runtime*
  binary too, not only the editor. So this does not spend a decision that was
  being saved — it spends one already made.

**Reverse-Z compatibility — the thing that would have disqualified it, measured
rather than assumed.** T0130 mandates `COMPARISON_FUNC_GREATER_EQUAL`, and a PBR
renderer that hardcoded `LESS_EQUAL` would produce exactly the failure this
ticket warns about: distant geometry occluding near geometry, read as a broken
mesh import and investigated in the wrong place.

It does not. `PBR_Renderer::GetPsoCacheAccessor(const GraphicsPipelineDesc&)`
takes the **caller's** pipeline desc, and grepping every depth write in
`PBR_Renderer.cpp` shows it touches only `DepthEnable` and `DepthWriteEnable` —
for the env-map and OIT passes — and **never `DepthFunc`**. The depth comparison
is the caller's throughout.

**That conclusion does not carry to `GLTF_PBR_Renderer`, and the next section is
the correction.** It is left standing rather than deleted because it is correct
about the base class, and because the mistake was reasoning from the base to the
derived without checking the derived.

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

### Decided (2026-08-05): adopt it, no wrapper, and file the rest

**Adopt `GLTF_PBR_Renderer`**, behind the pimpl, configured to touch none of
T0079/T0087/T0096's territory.

> **Correction (2026-08-05).** This first said "driven in unshaded mode", and
> that is **wrong** — recorded rather than quietly edited, because the wrong
> version is the one a reader would naturally assume.
>
> `PSO_FLAG_UNSHADED` is not reachable the way it looks. In
> `GLTF_PBR_Renderer::Render` (`GLTF_PBR_Renderer.cpp:617-635`) the flags are
> accumulated, then `PSOFlags &= RenderParams.Flags` — **the caller's flags are a
> mask that can only remove, never add** — and `PSO_FLAG_UNSHADED` is OR'd in
> afterwards **only** when `RenderParams.Wireframe` is true. So "unshaded" is
> reachable only *as wireframe*, which is not what this ticket wants.
>
> The working lever is different: `CreateInfo::EnableIBL = false`, plus a
> `RenderInfo::Flags` mask that excludes `PSO_FLAG_USE_IBL`,
> `PSO_FLAG_USE_LIGHTS`, `PSO_FLAG_ENABLE_TONE_MAPPING`,
> `PSO_FLAG_ENABLE_SHADOWS` and `PSO_FLAG_COMPUTE_MOTION_VECTORS`. Those five are
> precisely the features owned by T0079, T0087 and T0096, and masking them off is
> what keeps this ticket out of their way.
>
> Worth knowing for whoever picks up T0134: `PSO_FLAG_DEFAULT` **includes**
> `USE_IBL | USE_LIGHTS | ENABLE_TONE_MAPPING` (`PBR_Renderer.hpp:606`), so the
> default is the opposite of minimal. And when `PSO_FLAG_UNSHADED` *is* set,
> `PBR_Renderer.cpp:123-131` masks the flags down to
> `USE_JOINTS | ALL_USER_DEFINED | UNSHADED` — the renderer enforces the
> stripping itself rather than merely permitting it.

**No abstraction is built over it, and D22 is why:** "an abstraction over an
abstraction buys nothing until there is a concrete second target." An
`IMeshRenderBackend` with one implementation costs indirection everywhere and
buys nothing until a second renderer exists. Building one speculatively would be
a deliberate change to D22, not a free precaution.

The swappability comes from structure that already exists and is enforced by the
linker rather than by review: `engine/CMakeLists.txt:194` links Diligent
**PRIVATE unless a public engine header names a Diligent type**, and every engine
class is pimpl'd. The renderer lives in one `.cpp` behind an `Impl`, so replacing
it touches that file.

Two findings that reverse the obvious intuition, recorded because a future reader
will arrive with the intuition:

- **Writing our own pipeline would couple the engine *more* to Diligent**, not
  less. `MeshAsset` holds a `Diligent::GLTF::Model*`, so a hand-written path
  means walking Diligent's nodes, primitives, material indices and attribute
  layout ourselves, and depending on their in-memory shape across upgrades.
- **The real lock-in is not the renderer.** `Assets.hpp:368` returns
  `Diligent::GLTF::Model*` from a **public** header — T0023's commitment, not
  this ticket's. That is the seam that would hurt in a renderer change, and a
  renderer interface would not touch it.

**The broad question is not this ticket's**, and burying it here would make T0060
inherit a renderer by accident. How far DiligentFX's PBR path is adopted — and
what T0060, T0079, T0087 and T0096 do about materials, lighting, IBL and
tonemapping — is [T0134](../completed/0134-pbr-renderer-adoption.md), which carries
this survey and references all four both ways. **This ticket leaves an
unshaded-mode stopgap that T0134.6 resolves.**

### The parse step — built 2026-08-05 (28.1)

`hp/DrawSubmission.hpp` — `parseScene(const Scene&, DrawParseStats*) -> DrawList`.

**It takes no device, no asset pool and no camera**, and that is the design
rather than an omission. It is what lets a sort or cull pass be inserted between
parse and submit without restructuring (T0045), what keeps the path callable
per-camera against an arbitrary target (T0120.2), and what puts the whole step in
the **fast** bucket with no GPU — 7 cases, all green on both targets.

Three decisions worth keeping:

- **Filters on `WorldTransform`, not `Transform`.** A child's local position is
  meaningless to a draw, and the world matrix exists only after propagation
  (T0101). Parsing off `Transform` would render every parented entity at its
  local offset from the origin the first frame a hierarchy is built — which
  reads as a broken scene, not as a parse bug. There is a test that parents an
  entity at +5 under a parent at +10 and asserts the item carries 15.
- **The mesh/material asymmetry is deliberate and is 28.2.** An unset *mesh*
  GUID drops the entity; an unset *material* GUID is carried through. Both come
  straight from `MeshRenderer`'s own documentation: a default mesh means
  "nothing to draw", a legitimate state for an entity that exists before its
  asset is assigned, while a default material means the fallback, so a mesh with
  no material is still visible rather than silently absent.
- **The world matrix is copied, not referenced.** The list outlives the walk, and
  a pointer into component storage dangles as soon as anything creates an
  entity, which entt may answer by reallocating.

`DrawParseStats` is separate from the list because "how many were skipped and
why" is exactly what is worth reporting when a scene renders nothing, and a
dropped entity that is *counted* is what lets an empty frame explain itself. A
test asserts an entity with no `MeshRenderer` is not merely absent from the list
but is never *considered*, or the report would count every entity in the scene.

### The `PBR_Renderer` path is viable on public API alone (verified 2026-08-05)

Checked member by member before writing anything, because "drive the base class
instead" is only a good answer if it does not need a subclass, a patch or a
friend declaration. It needs none — every piece is public on `PBR_Renderer`
(public section runs to line 892) or static on `GLTF_PBR_Renderer`:

| Need | Call | Where |
|---|---|---|
| Pipeline states **with our depth state** | `GetPsoCacheAccessor(const GraphicsPipelineDesc&)` | `PBR_Renderer.hpp:795` |
| Frame constant buffer size | `GetPRBFrameAttribsSize()` | `:830` region |
| An SRB per material | `CreateResourceBinding(&srb, idx)` | `:536` region |
| Bind frame attribs into it | `InitCommonSRBVars(srb, frameAttribsCB)` | public |
| Material textures | `SetMaterialTexture(srb, srv, texId)` | public |
| Per-primitive constant buffer | `GetPBRPrimitiveAttribsCB()` | `:472` |
| Joints buffer (skinning, T0049) | `GetJointsBuffer()` | `:474` |
| The attribs memory layout | `GLTF_PBR_Renderer::WritePBRPrimitiveShaderAttribs(...)` — **static** | `GLTF_PBR_Renderer.hpp:237` |

So the whole of the expensive machinery — shader generation, PSO creation and
caching, SRB layout, material attribs packing — is reused, and what this ticket
writes is the traversal: scene → nodes → mesh → primitives, with
`ModelTransforms::NodeGlobalMatrices` indexed by `Node.Index`, binding vertex and
index buffers and issuing the draw.

**No patch, no subclass, no `friend`.** That matters beyond convenience: it means
a Diligent upgrade cannot silently break this through a private detail, which is
precisely what would have happened had the answer been "reach into
`m_PbrPSOCache`".

**Qualification found while writing against it (2026-08-05).** "All public API" is
true of the *draw* path and **not** of material SRB setup. `InitMaterialSRB` is a
`GLTF_PBR_Renderer` member, and replicating it needs two things this engine
cannot reach:

- **`GetPBRTextureSRV(texture, id, colourConversionMode)`** — absent from
  `PBR_Renderer.hpp` entirely, so private or file-local. It is what applies the
  **colour-space conversion** per texture slot, which is the same concern T0097
  owns for imported textures.
- **`m_pDefaultPhysDescSRV`** — private (`:1000`) with no getter, unlike
  `GetWhiteTexSRV()` / `GetBlackTexSRV()` which are public at `:469-470`.

Workable, with substitutions that must be recorded rather than slipped in: bind
the model's own texture views via `Model.GetTexture(idx)` and
`SetMaterialTexture` (public), fall back to white for the missing
physical-descriptor default, and handle — or deliberately skip — the colour
conversion `GetPBRTextureSRV` would have applied.

**That last one is a correctness question, not a detail**, and it lands next to
T0097's sRGB work: skipping the conversion silently is exactly the kind of
lighting bug that gets compensated by hand-tuning and then has to be
un-compensated. Whatever is chosen goes in writing here and on T0134.

**Not yet verified**, and not to be claimed until it is: that the pipeline states
actually build on both backends here, that the traversal draws correctly, and
that the substituted texture binding produces the right colours. All three need
`zig build test -Dtest=gpu`, which `all` builds and never runs.

### Built and measured on hardware (2026-08-05) — `SceneRenderer`

`engine/src/SceneRenderer.cpp`, driving `PBR_Renderer` directly. **The line the
whole design exists to make writable:**

```cpp
pipeline.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_GREATER_EQUAL;
static_assert(kReverseZ, "the depth comparison above assumes T0130's reverse-Z");
```

**Verified on a real device**, which is the only thing that could settle it —
`zig build test -Dtest=gpu`, **5 cases / 233 assertions / SUCCESS**:

```
[info ] render.scene: scene renderer ready, reverse-Z depth
```

on **both** backends. **Correction (2026-08-05, from T0134): the device was not
an NVIDIA RTX 4070**, which this said and which was not checked. Both backends
resolve to Mesa's software rasteriser here — `Vulkan on 'llvmpipe (LLVM 20.1.2,
256 bits)'` — because `/usr/share/vulkan/icd.d/` carries only `lvp_icd.json` and
no NVIDIA ICD, despite the machine having an RTX 4070 and `/dev/dxg`. Lavapipe
and llvmpipe are conformant, so the result below stands; it is simply not
hardware coverage. **Amended again the same day (T0135): the OpenGL backend now has
hardware coverage** on an RTX 4070 via Mesa's `d3d12` gallium driver, with results
pixel-identical to software; Vulkan still has no hardware path here. See
[../open/0135-gpu-tests-on-real-hardware.md](../open/0135-gpu-tests-on-real-hardware.md).
So `PBR_Renderer`
compiles its shaders and builds pipeline states against `GREATER_EQUAL` on both
— the substitution for `GLTF_PBR_Renderer` works, and reverse-Z is not something
the backends object to.

*(The OpenGL run came up on llvmpipe, a software rasterizer, rather than the
NVIDIA GL driver. It is a real GL implementation and it exercised pipeline
creation, but it is not a hardware GL result and should not be reported as one.)*

Three things worth keeping:

- **Two members had to be inlined**, both on `GLTF_PBR_Renderer` rather than the
  base, and both trivial once read. `Begin()` only maps the joints buffer, which
  next-gen backends require before first use each frame — omitting it is a
  validation error, not a visible bug, so it is easy to miss.
  `GetMaterialPSOFlags()` collapses to a **constant** under this `CreateInfo`:
  every optional flag it sets is gated on a setting (`EnableAO`,
  `EnableEmissive`, clear coat, sheen, anisotropy, iridescence, transmission,
  volume) and all are off, leaving the three always-on maps. If any is ever
  enabled, that must go back to consulting the material — T0134's business.
- **28.2 comes free from `CreateDefaultTextures = true`.** The renderer creates
  white/black/default-normal textures, which is exactly what a material with
  nothing assigned samples. Turning it off is what would make an unassigned mesh
  invisible.
- **SRBs are cached per mesh GUID and rebuilt when the model behind the GUID
  changes.** That second half is for T0058: a hot reload is the same identity
  with different data, and an SRB built against the old model's textures would
  bind freed views.

### 28.4 and 28.5 — built and measured (2026-08-05)

`hp::SceneView` owns a colour and a depth target, resolves the camera, submits
the scene and hands back the texture. `hp::FrameRenderedEvent` carries it.

**GPU-verified on both backends** — `zig build test -Dtest=gpu`, **7 cases / 323
assertions / SUCCESS** (was 5 / 233):

- a scene with **no camera** renders nothing, says so at debug level, and still
  publishes a *cleared* frame. Both halves matter: not crashing is the Done-when,
  and publishing anyway is what stops a stale image reading as "the renderer
  froze". A warning every frame would train people to ignore the log, so a scene
  under construction is not warned about;
- a camera with nothing drawable publishes a cleared frame;
- an entity whose mesh is not loaded is **counted**, draws nothing, and does not
  take the frame down;
- resize works and is a no-op when unchanged, so it is safe every frame;
- **the published view is the shader-resource view**, which is 28.5's whole
  point — a render-target view that was not also sampleable would pass every
  other assertion here and be useless to both consumers.

The event's own behaviour is in the **fast** bucket, because it needs no device
(4 cases): it reaches a listener that never sees the renderer, it is categorised
`Render` rather than `Window`, a consuming listener stops it reaching layers
below, and two non-consuming listeners both receive it. That last pair is
deliberate — a game view and a preview on one frame is legitimate, so
consumption is the listener's choice rather than automatic.

Two decisions worth keeping:

- **The viewport comes from the `ResolvedView`, not from the target size.** Under
  a letterboxing aspect policy they differ, and using the target's size produces
  an image stretched by exactly the letterbox ratio (T0081 warned about this).
- **`SceneView` does not dispatch.** It returns the texture and the caller
  publishes, because dispatch belongs to `Application` and an engine class
  reaching into the layer stack to announce itself inverts that ownership.

### The editor is wired, and the dev present path is half a success (2026-08-05)

The editor now owns a `SceneLayer` that creates a `SceneView`, renders the scene
offscreen, publishes a `FrameRenderedEvent`, and hands the texture to
`RenderLayer::setPresentSource`. **Run and observed**, not merely compiled.

**Layer order is the opposite of the obvious one, and it is load-bearing.**
`LayerStack::render` walks layers in *push* order and `RenderLayer` clears, blits
and presents in a single `onRender` — so the scene must be pushed **first** to
draw first. Pushed after, it would render into a frame already presented and
nothing would ever appear. The render layer is therefore constructed before it is
pushed so the scene layer can hold a reference; constructing creates no device,
`push` attaches and does, which is why `SceneView` is built lazily on the first
frame rather than in `onAttach`.

**The dev blit works on OpenGL and correctly refuses on Vulkan.** It is a plain
`CopyTexture` — no shader, no scaling — which requires an exact format match:

| backend | swap chain | result |
|---|---|---|
| OpenGL | `RGBA8_UNORM_SRGB` (29) | blits, **0 warnings over a 20s run** |
| Vulkan | `BGRA8_UNORM_SRGB` (91) | **skipped**, reported once |

The scene target is `RGBA8_UNORM_SRGB`, and Vulkan's surface here is BGRA.
`CopyTexture` does not convert, so copying anyway would put a red/blue-swapped
image on screen — worse than nothing, because it looks like a shader bug.
Refusing is correct, and it means **the dev path shows a frame on GL and a plain
clear on Vulkan**.

Not worth patching around: the honest fix is a fullscreen-triangle blit with a
trivial shader, which converts formats as a matter of course. That is small, and
it is also *exactly* what a compositor does, so it may belong to T0027 rather
than being built twice. **Recorded rather than fixed, because choosing where it
lives is not this ticket's call to make quietly.**

**A bug worth recording because of where it was.** The mismatch warning was
written "report once, because at 60fps a per-frame warning buries everything
else within seconds" — and then reported every frame, because
`setPresentSource` reset the flag and the caller sets the source every frame.
The comment describing the failure sat three lines from the code committing it.
Fixed by resetting only when the source pointer actually changes; verified as
1 warning over a 20-second run where it had been one per frame.

### It draws. And three bugs stood between "a draw was issued" and "pixels exist"

`tests/gpu/scene_draws_mesh_test.cpp` loads a glTF quad through the VFS, puts it
in front of a default camera, renders, and **reads the pixels back**:
**65,536 of 65,536 differ from the clear colour**, on both backends. GPU suite:
**9 cases / 219 assertions / SUCCESS**.

**That assertion is the entire reason this test exists.** Every earlier check
passed throughout the debugging below — pipeline states built, the draw was
issued, `submitted == 1`, and **Diligent reported no error or validation warning
of any kind**. The frame was nonetheless pure clear colour. Statistics cannot
tell "drew nothing" from "drew correctly"; only pixels can.

The three faults, in the order they were found, because each was hidden behind
the last:

1. **The material constant buffer was never written.** `PBR_Renderer` keeps
   material data in `GetPBRMaterialAttribsCB()` — a *second* buffer, separate
   from the primitive one — filled via the static
   `GLTF_PBR_Renderer::WritePBRMaterialShaderAttribs`. Unwritten, it is a zero
   base colour and an alpha cutoff that discards every fragment.
2. **`CreateInfo::InputLayout` was empty.** It defaults empty, and the renderer
   builds its vertex-shader input struct from it — so the pipeline compiled,
   bound and rasterised nothing. Fixed with
   `GLTF::VertexAttributesToInputLayout(GLTF::DefaultVertexAttributes…)`, which
   is what `MeshAsset`'s models are loaded with, so the two now agree by
   construction rather than by luck.
3. **`PackMatrixRowMajor` was left at its default of `false`.** This was the
   one that actually kept the screen empty. `hp::float4x4` is row-major —
   `hp/Math.hpp` says so — and the setting defaulting to false compiles the
   shaders for **column-major**, so every matrix in the frame constants was read
   transposed and the geometry was transformed off screen. Diligent's own
   GLTFViewer sets it true (`GLTFViewer.cpp:560`); it was missed because the
   default is silent.

**What kept the search honest** was ruling things out with probes rather than
reasoning. Disabling the depth test changed nothing, which eliminated reverse-Z
early — worth stating, since this ticket's whole theme made depth the obvious
suspect and it was innocent. Projecting the quad's centre through the real
view-projection gave NDC (0, 0, 0.033) with w = +3, which eliminated the camera.
And `PSOKey` was *suspected* and turned out fine: it has a three-argument
overload defaulting alpha to opaque, so the construction was already correct and
"fixing" it would have been a change for nothing.

### Amendment (2026-08-05, from T0027): what the pixel evidence actually proves

T0027.4 needed two layers to be distinguishable by colour, tried to give them
different materials, and measured this instead:

```
sampled colour of the quad: (0, 0, 0, 255)   -- whatever the material says
```

**Every mesh this engine renders is pure black**, and `baseColorFactor`,
`emissiveFactor` and alpha make no difference. `SceneRenderer` sets
`MaxLightCount = 0`, `EnableIBL = false` and `EnableEmissive = false`, so the
shading result is zero. Nothing here is broken — there is no light in the world
until T0079.

So this ticket's decisive assertion — *"65,536 of 65,536 pixels differ from the
clear colour"* — is true, and it proves less than it reads. It proves **geometry
reached the target**: the VFS, the asset load, the traversal, the vertex layout,
the matrices and the reverse-Z depth comparison all work, which is exactly the
class of bug that cost an afternoon here. It does **not** prove anything about
shading, and would pass identically if shading were completely broken, because
the pixels differ from blue by being black.

Left standing rather than rewritten: the test is still the right test for what
this ticket owns. **T0079 carries the obligation** to assert that a lit surface
is the colour it was authored as, which is the first point at which that is
answerable at all.

### What is NOT verified, and must not be read as working

**Nothing has been drawn yet.** The GPU test proves pipeline states build, that
an empty list submits nothing, that a missing mesh is counted rather than fatal,
and that an uncreated renderer is a safe no-op. **No test loads a real mesh and
issues a draw**, so the traversal — node walk, vertex/index binding, primitive
attribs, the draw calls — is written and compiled and **entirely unexercised**.

Specifically still open:

- **The runtime is still unwired.** The editor publishes a frame; `apps/runtime`
  does not. T0042 owns that, and it can now copy the editor's `SceneLayer`.
- **Vulkan shows a clear, not a scene**, until the blit converts formats — see
  the dev-present table above.
- **The traversal itself.** Until a mesh is drawn and looked at, "it renders" is
  not a claim this ticket may make.
- **Colour-space conversion on material textures.** `GetPBRTextureSRV` is not
  public, so textures bind through `SetMaterialTexture` with the model's own
  views and no conversion is applied. Untextured materials are unaffected. This
  is a real gap next to T0097's sRGB work and is recorded on T0134.
- **Material GUIDs are parsed and carried but never resolved** — there is no
  material asset until T0060.

### Camera

**T0081 built the camera resolution; this ticket is what calls it each frame.**
`hp::resolveCamera(scene, viewSlot)` picks the active camera and
`hp::buildView(entity, width, height, clip)` produces every matrix. Neither is
wired into a frame, because nothing draws yet — that is this ticket.

`RenderStack` deliberately does **not** depend on `Scene`: a compositing pass has
no business knowing about the ECS, and making it know would couple every layer to
the scene graph. So the resolve belongs here, where draws are submitted, and the
resolved view is handed to whatever renders.

Two things T0081 left for this ticket specifically:

- **Resolve per view slot, not once globally.** A world layer and a HUD layer
  each resolve their own slot, which is how a HUD gets an orthographic camera
  without the world knowing about it.
- **`ResolvedView::aspect` is the aspect to use, not the window's.** Under a
  letterboxing camera they differ, and using the window's produces an image
  stretched by exactly the letterbox ratio.


**T0130 decided reverse-Z, and this ticket is where it is honoured or silently
broken.** Every pipeline state this ticket creates must set the depth
comparison to `COMPARISON_FUNC_GREATER_EQUAL`, not `LESS_EQUAL`. The engine maps
the near plane to depth 1 and the far plane to 0 (`hp::kReverseZ` in
`hp/DepthConvention.hpp`, which carries the full argument), the depth target is
`D32_FLOAT`, and the clear value is `hp::kDepthClearValue` — 0.

Getting this wrong does not look like a depth bug. It renders a scene in which
distant geometry occludes near geometry, which reads as models being inside out
or as a broken mesh import, and it will be investigated in the wrong place.

The measurement behind the choice, from T0130's fast tests: over the range
900m–901m with a 0.1m near plane, reverse-Z resolves **135,604** distinct float
values where the conventional mapping resolves **2**.


**Rendering to an offscreen target rather than the swap chain is what makes the
editor viewport possible at all** — the viewport is an ImGui image of that
texture. The runtime (Phase 8) then just stretches the same texture full-window,
which is why both apps can share this code unchanged.

The frame-rendered event is the *only* thing connecting renderer and viewport.
Resist the temptation to hand the viewport a renderer pointer; that coupling is
what the event system exists to avoid.

Sorting, culling and instancing are deliberately out of scope here — get correct
output first. But leave the parse step's output as an explicit list so a sort or
cull pass can be inserted without restructuring.

### Second review pass (2026-08-03) — nothing displays this texture until Phase 6

The two consumers of the frame-rendered event are the viewport panel (T0033,
Phase 6) and the runtime (T0042, Phase 8) — so as written, all of Phase 4 has
no on-screen output. Add a trivial dev-only present path (blit the offscreen
target to the swap chain in the editor app's stub layer) so Phase 4 work is
visually verifiable as it lands. It is ~20 lines, it exercises the same event,
and T0033 simply replaces it.

### Cross-ticket obligations (2026-08-04, T0124 backfill)

- **T0120.2** makes this submission callable per-camera against an arbitrary
  target (portals, security monitors, thumbnails). Keep the parse/submit path
  free of one-implicit-camera, one-viewport-target assumptions — the explicit
  parse-output list in the notes is half of that; camera and target as
  parameters are the other half. Retrofitting them means restructuring
  submission, which is what T0120 was filed to avoid.
