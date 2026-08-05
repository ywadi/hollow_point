# T0028 — Scene draw submission and the frame-rendered event

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 410 |
| **Created** | 2026-08-02 |
| **Refs** | T0120, [../completed/0130-camera-lens-model.md](../completed/0130-camera-lens-model.md), [../inprogress/0081-camera-system.md](../inprogress/0081-camera-system.md) |

## Why

"Render the scene" only becomes a well-defined operation once the scene and asset
model exist. This turns entities into draw calls, and publishes the result so the
editor viewport can display it without the renderer knowing the editor exists.

## Done when

- [ ] Entities with transform + mesh are collected and drawn
- [ ] A scene with no camera renders nothing and says so, rather than crashing
- [ ] Entities with no material get a visible default
- [ ] Output goes to an offscreen target, not straight to the swap chain
- [ ] A "new frame rendered" event carries that texture to any listener
- [ ] Assets resolve from the pool by GUID (T0023)

## Subtasks

- [ ] 28.1 Parse step: filter to entities with transform + mesh, require a camera
- [ ] 28.2 Default material for meshes without one
- [ ] 28.3 Resolve mesh/material GUIDs against the asset pool
- [ ] 28.4 Render to an offscreen target sized to the viewport
- [ ] 28.5 Emit the frame-rendered event with the texture handle
- [ ] 28.6 Profiling zones for parse and submit separately

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
is the caller's throughout, so reverse-Z survives adoption.

### Decided (2026-08-05): adopt it, no wrapper, and file the rest

**Adopt `GLTF_PBR_Renderer`, driven in unshaded mode**, behind the pimpl.

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
tonemapping — is [T0134](../open/0134-pbr-renderer-adoption.md), which carries
this survey and references all four both ways. **This ticket leaves an
unshaded-mode stopgap that T0134.6 resolves.**

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
