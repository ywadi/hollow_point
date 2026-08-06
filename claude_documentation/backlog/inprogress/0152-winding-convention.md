# T0152 — The winding convention: hardware facing equals glTF facing, and the test assets were the bug

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 459 |
| **Created** | 2026-08-06 |
| **Blocks** | **T0086** — shadow bias and the shadow-pass cull mode must be chosen against a declared convention, or the tuned bias bakes the inversion in permanently; **T0087** — IBL baselines are view-dependent and multiply the re-baseline cost; **T0143** — every extended-material pixel test added before this lands is calibrated against inconsistent assets |
| **Refs** | [../completed/0141-custom-shader-materials.md](../completed/0141-custom-shader-materials.md) — 141.12 found the symptom and recorded the (mis)diagnosis this ticket corrects; [0086-shadows.md](../open/0086-shadows.md) — its Refs carry 141.12's warning, which this ticket re-states correctly; [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) **D33** (this ticket's decision), D26 (why the engine subclasses `PBR_Renderer` and therefore configures its own rasterizer state), D29 (Vulkan-only, which makes Diligent's internal viewport flip a constant rather than a variable); `engine/include/hp/DepthConvention.hpp` / T0130.3 — the precedent this ticket's header is modelled on |

## Why

**The owner's concern, in their words:** *"my worry is we are moving away from
Vulkan standards, are we? what's the right direction?"*

The direct answer, from the Vulkan specification: **Vulkan has no winding
standard to move away from.** Facing is computed from the signed area of the
triangle in framebuffer coordinates, and `VkFrontFace`'s two values are
defined symmetrically — the spec expresses no preference and OpenGL's CCW
default has no Vulkan equivalent
([primsrast — Basic Polygon Rasterization](https://docs.vulkan.org/spec/latest/chapters/primsrast.html#primsrast-polygons-basic)).
The standard that *does* bind us is **glTF's**, because glTF is the engine's
only mesh format: front faces are counter-clockwise as seen from the front,
and — the part nobody implements — **the sign of the determinant of the
node's global transform flips it**
([glTF 2.0 §instantiation](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#instantiation)).

T0141.12 landed the engine's first single-sided draw, it came back invisible,
and the finding was recorded as *"a glTF front face reaches the rasteriser as
a hardware back face"* — attributed to glTF's CCW winding × the left-handed
view × Vulkan's viewport flip, worked around by culling `FRONT` for
single-sided materials. **That diagnosis is wrong, and this ticket exists
because the trace in 152.1 shows where.** The engine's winding chain is
correct and glTF-conformant as it stands; the invisible quad was invisible
because **the test asset is wound backwards** — its indices produce a
right-hand-rule normal of +Z while its authored `NORMAL` attributes say −Z,
so the camera genuinely views the primitive's glTF *back* face, and a
conformant renderer culls exactly what the engine culled. The `CULL_FRONT`
workaround keeps faces a conformant renderer discards, and discards the faces
it keeps.

What this ticket does: declares the convention in one header modelled on
`DepthConvention.hpp`, reverts the workaround, re-winds the assets, and closes
the glTF determinant gap — before T0086 tunes shadow bias against any of it.

## The root-cause trace (152.1, done 2026-08-06) — count the reversals

Every step measured in this tree, none reasoned from memory. The question:
how many times does apparent winding flip between a glTF file's index buffer
and the rasteriser's facing decision?

| # | Stage | Where measured | Reversals |
|---|---|---|---|
| 1 | glTF import | `Diligent::GLTF::Model` via `engine/src/AssetImport.cpp:343`; no coordinate conversion, no index reversal anywhere in `DiligentTools/AssetLoader/src/GLTFLoader.cpp` (grepped for flip/handed/negate/mirror: nothing) | **0** — vertices and indices reach the GPU as authored |
| 2 | Model → world → view | `resolved.view = cameraWorld.Inverse()` (`engine/src/CameraSystem.cpp:185`) — a rigid inverse, determinant +1 | **0** (until a negative-scale node appears — see 152.5) |
| 3 | Projection | `hp::projectionMatrix` → `float4x4::Projection`, Diligent's left-handed form (`BasicMath.hpp:1835`): `_11`, `_22` positive, `_34 = +1`, no XY mirror | **0** |
| 4 | **Reverse-Z** | `SetNearFarClipPlanes` (`BasicMath.hpp:1762`) writes only `_33` and `_43`; the near/far swap never touches the X/Y columns or the sign of `_34` | **0** — reverse-Z is winding-neutral. The brief's suspicion ("a near/far swap is itself a determinant change") is true of the 4×4 determinant and irrelevant to facing, which depends only on the projected 2D vertices |
| 5 | Viewport | The engine sets **positive** height everywhere (`SceneView.cpp:198–205`, `SceneRenderLayer.cpp:116–123`). Diligent's Vulkan backend then flips it to negative internally and unconditionally (`DeviceContextVkImpl.cpp:1807–1832`, with a diagram) to normalise Vulkan to **D3D screen conventions** | **1** — but *inside the abstraction*: `FrontCounterClockwise` is passed through to `VkFrontFace` untouched (`VulkanTypeConversions.cpp:718`), so the flag's meaning is D3D's screen-space one on every backend, flip included |
| 6 | Rasterizer flag | `FrontCounterClockwise` defaulted `false` everywhere = D3D's clockwise-front | the decision point |

**Net: under the engine's conventions, a glTF front face whose normal points
at the camera arrives *clockwise as displayed* — which D3D's clockwise-front
default classifies as FRONT.** The current default is glTF-conformant.
The measured anchor, from T0141.12's own probes: the test quad — wound with
RH-rule normal **+Z**, viewed from **−Z** — returned `SV_IsFrontFace ==
false`. That is the *correct* answer: the camera views that primitive's
winding-defined back face. The probe was read as "front face classified back"
because the quad's authored normals (−Z, toward the camera) were taken as
ground truth for facing; glTF facing is defined by winding, and this asset's
winding contradicts its normals.

**Every quad in the gpu suite shares the inconsistency.** Six test files
author the same pattern — vertices BL, BR, TR, TL and indices
`{0,1,2, 0,2,3}` (RH normal +Z, i.e. away from the camera) with `NORMAL`
attributes `(0,0,−1)` (toward it): `lit_surface_test.cpp:98–104`,
`textured_surface_test.cpp:139`, `material_assignment_test.cpp:106–112`,
`scene_draws_mesh_test.cpp:92`, `present_blit_test.cpp:114`,
`render_stack_composites_test.cpp:136`. The one consistent asset in the suite
is `asset_import_test.cpp:113`'s triangle (winding and normal both +Z).

**Why Diligent's own sample does not transfer — the trap 141.12 fell toward.**
`GLTFViewer.cpp:559` sets `FrontCounterClockwise = true`, which looks like the
reference answer. It is not, because the viewer's chain differs by exactly one
mirror: it applies `InvYAxis` (`_22 = −1`, determinant −1) to the model
transform (`GLTFViewer.cpp:240–243`) and `InvZAxis` to glTF cameras
(`GLTFViewer.cpp:1498–1501`). One extra mirror flips apparent winding once,
so *their* correct flag is `true` and *ours* — with no mirror anywhere — is
`false`. The field itself lives on `GLTF_PBR_Renderer::CreateInfo`
(`GLTF_PBR_Renderer.hpp:61`), the derived class D26 deliberately stopped
using; the base `PBR_Renderer` has no such field, which is why nothing ever
prompted the engine to decide it. Same failure shape as
`TextureAttribIndices` (T0141): the derived class configures it in a private
wrapper, a direct subclass must do it itself, and nothing warns.

**The three-factor attribution, scored:** glTF's CCW convention — real, and
honoured by the current default. The left-handed view — contributes **zero**
reversals (the view matrix is a rigid inverse; the LH projection has no XY
mirror). Vulkan's viewport flip — real, internal to Diligent, and already
folded into the flag's semantics. Reverse-Z — zero. **No step in the engine's
chain inverts winding.** The odd one out was never in the chain; it is in the
assets.

### The lit suite went black for the reason hypothesised, with a sharper cause

The hypothesis on the table: the lit tests' quads are double-sided
(`CULL_MODE_NONE`), so winding never affected their visibility — only
`SV_IsFrontFace`, and therefore the two-sided normal flip. **Confirmed, from
the geometry:**

- The lit quad's authored normals point at the camera (−Z); its *winding*
  points away (+Z). Hardware correctly reports camera-facing fragments as
  back faces.
- The two-sided flip (`HpSurface.slang:299`,
  `surfaceIn.Normal = geometricNormal * (IsFrontFace ? 1.0 : -1.0)`, and
  `GetPerturbNormalInfo`'s `Info.Face`, `PBR_Shading.fxh:161`) then inverts
  the authored normal to **+Z — away from the viewer**. The flip exists to
  point normals *at* the viewer; on a winding-inconsistent asset it does the
  opposite.
- The test light travels −Z (identity transform; `Light.cpp:29–34`,
  glTF's KHR_lights_punctual convention), i.e. it illuminates the **+Z side —
  the side facing away from the camera**. `N·L` is 1 only because both N and
  the lit side are inverted together. `lit_surface_test.cpp:331–337` states
  it plainly: *"the quad is lit from behind, not from the camera side"*.
- Under `FrontCounterClockwise = true` the same fragments classified front,
  the flip stopped, N stayed −Z, the light stayed behind: `N·L = −1`, black.
  **The suite did not go black because `true` is wrong for some deeper
  reason; it went black because `true` is wrong for this engine's
  mirror-free chain *and* the scenes depend on the inverted flip.**

So the baselines do encode a second latent inconsistency, exactly as feared —
**but re-baselining against existing light positions is not the risk it
appeared to be**, because almost every asserted value survives the fix (see
the cost table below). The scenes were, to their credit, deliberately built
on orientation-independent assertions.

### The finding underneath the finding: the display is mirrored, and the owner must see it before T0086

One real consequence of the left-handed view survives the corrected
diagnosis, and it is not about winding classification. The engine displays an
unconverted right-handed world **mirror-imaged**: with an identity camera
(looking +Z), world +X lands on screen right, while a physical observer
facing +Z has +X on their *left*. Equivalently: the displayed frame
(x right, y up, z into the screen) is left-handed. Cross-checked against the
hardware measurement: mirrored display ⟺ glTF front faces appear visually
clockwise ⟺ clockwise-front is the conformant flag — which is exactly what
152.1 measured. glTF mandates the right-handed reading (*"the left side of a
glTF asset faces +X"*), so any DCC-authored asymmetric content — text on a
sign, a watch on a left wrist — will render flipped. **Invisible today**
because every test asset is a symmetric quad authored directly against engine
conventions; visible the day the first real model is imported.

Derived and cross-anchored, **not yet observed on hardware** — 152.6 builds
the probe that settles it on screen. The decision it forces is the owner's:
accept and document the mirrored convention (content authored against it, the
D3D-native position), or introduce exactly one mirror (an import-time axis
flip à la Unity, or a view-space handedness change à la Godot/GL) — which
flips apparent winding once and **requires `kFrontFaceCounterClockwise` to
flip with it**. That coupling is why the header below carries both constants
and a `static_assert` chaining them: whichever way the owner decides, the two
cannot move separately.

## Done when

- [x] `WindingConvention.hpp` exists, `SurfacePipeline` declares
      `FrontCounterClockwise` from it, and no rasterizer state in the engine
      leaves the field defaulted *(2026-08-06 — the fullscreen blit in
      `Render.cpp` declares it too, though `CULL_NONE` never consults it)*
- [x] Single-sided materials cull `BACK`, with no apology needed — the
      `CULL_FRONT` workaround and its comment are gone from `SceneRenderer`
      *(2026-08-06)*
- [x] Every gpu test asset winds consistently with its authored normals, and
      the suite is green on both targets with the values recorded here
      *(2026-08-06 — six files re-wound plus `parallax_test.cpp`, which was
      created mid-correction with the old order; `triplanar_test.cpp` was
      authored correct. **The "zero exact baselines move" prediction was
      wrong** — see the measurement note below)*
- [x] A camera-facing glTF front face measures `SV_IsFrontFace == true` — the
      inverse of T0141.12's probe, asserted so it cannot regress silently
      *(2026-08-06 — the material-assignment test's assigned-green case is the
      probe: a **single-sided** correctly-wound quad through `CULL_MODE_BACK`
      must land its exact (0, 255, 0), which only a hardware front face can)*
- [ ] Negative-determinant node transforms flip facing per the glTF spec, or
      the decision not to support mirrored-scale nodes is written down with a
      trigger
- [ ] The display-handedness question is put to the owner with 152.6's probe
      rendered, and the answer recorded — **open, deliberately; it is not
      this ticket's to decide**
- [~] T0086's Refs stop pointing at the inverted diagnosis and point here —
      staged as an exact edit (INTEGRATION-WINDING.md §3) being applied by the
      coordinating session; verify it landed before this ticket closes

## Subtasks

- [x] 152.1 **Root-cause trace, measured in-tree** — done 2026-08-06, evidence
      above: zero winding reversals in the engine's chain; reverse-Z proven
      winding-neutral (`SetNearFarClipPlanes` writes only `_33`/`_43`); the
      Vulkan viewport flip located inside Diligent's abstraction
      (`DeviceContextVkImpl.cpp:1831`) with the flag passed through raw
      (`VulkanTypeConversions.cpp:718`); the six backwards-wound test assets
      identified; `GLTFViewer`'s `FrontCounterClockwise = true` explained by
      its `InvYAxis` mirror and shown not to transfer
- [x] 152.2 **`WindingConvention.hpp`** — the header below, verbatim; plus
      `SurfacePipeline::build` and `buildEngineSurfacePipeline` declaring the
      flag from it, replacing the "stays at Diligent's default" comment at
      `SurfacePipeline.cpp:478–490` with a pointer to the header.
      *Done 2026-08-06. `buildEngineSurfacePipeline` flows through
      `SurfacePipeline::build`, so one declaration covers both; the blit
      pipeline in `Render.cpp` is the only other rasterizer state in the
      engine and declares it too.*
- [x] 152.3 *(done 2026-08-06 — plus `parallax_test.cpp`, created with the
      old order mid-correction; measured values in the notes, one prediction
      corrected there)* **Re-wind the six test assets and re-aim two lights.** Indices
      `{0,1,2, 0,2,3}` → `{0,2,1, 0,3,2}` in the six files named above; yaw
      the lit-surface directional light π so it travels +Z onto the
      camera-facing side (`N·L` returns to 1 and **(211, 144, 144) survives
      by symmetry** — same incidence, same view, same BRDF); move the
      point-light subcase to the camera side at the same distances (z = 1,
      −6, −34 for the 2-, 9- and 37-unit cases); aim the spot the same way;
      mirror the textured test's rake (its `direction.y < 0` assert and all
      variation thresholds are orientation-independent and expected to
      survive — verify, do not assume)
- [x] 152.4 **`CULL_MODE_FRONT` → `CULL_MODE_BACK`** in
      `SceneRenderer.cpp:779`, in the **same commit** as 152.3 — either
      change alone makes every single-sided draw invisible, which is the
      original symptom from the other side. *Done 2026-08-06, one commit with
      152.2 and 152.3.*
- [ ] 152.5 **The glTF determinant rule.** A negative-determinant node
      transform flips effective winding by specification; nothing in the
      chain honours it — not the loader, not DiligentFX (`grep determinant`
      across `GLTFLoader.cpp` and `DiligentFX/PBR`: nothing), not the engine.
      Cheapest correct form: sign of the upper-3×3 determinant of
      `nodeMatrix` at draw time flips the cull enum in the `PSOKey`. Decide
      whether the two-sided normal flip needs the same treatment, and write
      down whichever half is deferred
- [ ] 152.6 **The chirality probe.** One gpu test with an asymmetric mesh (an
      "F" glyph outline is enough) asserting which screen side each world
      direction lands on — the test that pins display handedness forever, and
      the rendering the owner decides the mirror question against. Until it
      exists, the mirror claim above stays "derived, cross-anchored, not
      observed"
- [ ] 152.7 **Docs**: `06-engine-conventions.md` gains the winding section;
      T0086's Refs updated (it currently instructs settling "the inverted
      convention" that does not exist); `08-frame-anatomy.md` if the facing
      decision's location merits a line

## Measured 2026-08-06 — the fix on hardware, and the prediction that did not survive

Applied atomically (152.2 + 152.3 + 152.4) and run on an RTX 4070 Laptop GPU,
Vulkan, both targets, wine for the Windows suite: **19 gpu cases, 515
assertions, zero failures, zero skips**; 302 fast + 89 integration beside
them; docs regenerated (the new public header gets a page).

**Every inequality assertion held. Several recorded values moved, and the
cost table's "zero exact baselines expected to move" was wrong** — for a
reason worth keeping: the symmetry argument covers `N·L`, but not the
**view-dependent** terms. The backwards asset put the flipped shading normal
*away* from the camera, so `NdotV` sat at its `1e-4` clamp and the Fresnel
term inflated the achromatic specular. The old baselines quietly encoded the
inversion; the new values are what a red dielectric under a white light
should look like at normal incidence. Before → after:

| measurement | pre-T0152 | post-T0152 |
|---|---|---|
| lit red quad | (211, 144, 144) | **(242, 25, 25)** |
| …at intensity 0.5 | (92, 61, 61) | (107, 5, 5) |
| point light near / far | (255, 145, 145) / (80, 46, 46) | (255, 39, 39) / (90, 4, 4) |
| point past range | (0, 0, 0) | (0, 0, 0) |
| spot wide / narrow | (255, 251, 251) / (0, 0, 0) | (255, 58, 58) / (0, 0, 0) |
| rock shaded | (85, 80, 57) var 14.52 | (86, 81, 57) var 18.50 |
| metal shaded | (12, 12, 11) var 6.89 | (8, 8, 7) var 8.15 |
| base colour channel | (116, 108, 69) var 16.38 | (117, 109, 70) var 16.35 |
| shading-normal channel var | 12.17 | 12.90 |
| occlusion channel var | 6.37 | 6.33 |
| metallic rock / metal | 0 / 249 | 0 / 250 |
| parallax displacement diff | 15.69 | 15.69 |

The lit-surface subcase placements moved to the camera side per the recipe
(z = 1, −6, −34; spot z = 1 with the π yaw), the textured rake gained its π
yaw with `direction.y < 0` intact, and the "lit from behind" comments died
with the fix. The material-assignment exact values — (0, 255, 0), (0, 0, 0),
the checkerboard counts — survived unchanged, as predicted; the assigned
single-sided green quad through `CULL_MODE_BACK` now doubles as the
`SV_IsFrontFace == true` probe.

**Still open here**: 152.5 (the determinant rule), 152.6 (the chirality
probe and the owner's mirror decision), 152.7 (docs), and verifying the
T0086-refs edit landed from the coordinating session.

## The header, exactly as intended (152.2)

```cpp
// The engine's winding convention (T0152).
//
// **Deliberately a header of its own, and deliberately tiny**, for exactly
// `DepthConvention.hpp`'s reasons: the things that must agree with it —
// pipeline rasterizer state, the glTF importer, the shadow passes, the test
// assets — have no business paying for Diligent's headers to read two bools.
//
// So this header includes nothing.
#pragma once

namespace hp {

/// Whether counter-clockwise framebuffer winding is front-facing.
///
/// **False, declared rather than defaulted, and this is the single place
/// that decides it.** Vulkan has no preferred winding — facing is the sign
/// of the triangle's area in framebuffer coordinates, and both `VkFrontFace`
/// values are equally standard. The convention that binds this engine is
/// glTF's, its only mesh format: front faces wind counter-clockwise seen
/// from the front. Measured through this engine's chain (T0152.1), a glTF
/// front face facing the camera arrives at the rasteriser **clockwise as
/// displayed**: the importer converts nothing, the view is a rigid inverse,
/// the left-handed projection has no XY mirror, reverse-Z touches only the
/// Z column, and Diligent's Vulkan backend folds its internal viewport flip
/// into this flag's D3D screen-space semantics. Clockwise-front — this
/// value — is therefore the setting under which **hardware facing equals
/// glTF facing**, which is the invariant everything downstream assumes.
///
/// **It is not free, and the cost is that it is not local.** Five things
/// must agree, and any one left behind produces a failure that does not
/// look like a winding bug:
///
/// 1. Every pipeline's `RasterizerStateDesc::FrontCounterClockwise` is this
///    constant. A pipeline that defaults it happens to agree today and
///    stops agreeing the day the convention moves — silently.
/// 2. Single-sided materials cull `BACK`. Culling anything else is not a
///    tuning choice; it inverts which side of every wall exists.
/// 3. The two-sided flip (`SV_IsFrontFace` in `HpSurface.slang`, and
///    DiligentFX's `GetPerturbNormalInfo`) assumes authored normals agree
///    with winding. An asset that violates that renders lit from the wrong
///    side — the bug is the asset, not the flip.
/// 4. Shadow passes (T0086) choose their cull face *as depth-bias policy*
///    against this convention. Bias tuned against an inverted one bakes the
///    inversion into every tuned value permanently.
/// 5. A mirror introduced anywhere between authored space and the
///    framebuffer — an import-time axis flip, a negative camera-parent
///    scale, a render-to-texture blit that flips Y inside the scene pass —
///    toggles apparent winding once, and this flag must toggle with it.
///    That is what the static_assert below is for. A mirror left
///    uncompensated does not look like a mirror: it looks like every
///    single-sided mesh in the world vanishing.
///
/// Per-node mirrors are the glTF determinant rule and are handled per draw
/// (T0152.5), not here: this constant is the zero-mirror baseline of the
/// engine's own chain.
inline constexpr bool kFrontFaceCounterClockwise = false;

/// Whether the importer mirrors content — negates one axis — to convert
/// glTF's right-handed space into a left-handed one.
///
/// **False: imported geometry passes through untouched.** The consequence,
/// derived in T0152 and pinned by its chirality probe, is that the engine
/// displays right-handed content mirror-imaged; the compensating
/// consequence is that no winding flip is needed at import. Flipping this
/// to true (the Unity-style answer to the mirror) reverses apparent winding
/// once, so `kFrontFaceCounterClockwise` must flip with it — which the
/// assert below enforces at compile time rather than as a code review
/// hope.
inline constexpr bool kImportMirrorsContent = false;

static_assert(kFrontFaceCounterClockwise == kImportMirrorsContent,
              "One mirror between authored space and the framebuffer flips "
              "apparent winding once. These two move together or single-sided "
              "geometry inverts engine-wide (T0152).");

} // namespace hp
```

## Cost, in numbers (the §5 question)

The feared cost — "re-baseline every pixel test" — was measured against what
the tests actually assert, and it shrinks to this:

| What changes | Where | Asserted values at risk |
|---|---|---|
| One index line per file | 6 gpu test files | none — the line is authoring, not assertion |
| Light re-aim, ~6 lines + comments | `lit_surface_test.cpp` | **(211, 144, 144) survives by symmetry** — the corrected scene is the mirror image of the current one, same incidence angles, same BRDF; the point/spot subcases keep their distances |
| Rake mirrored, ~3 lines | `textured_surface_test.cpp` | all assertions are variation thresholds, orderings, and channel identities — none pins an exact shaded value; the normal-map handedness *un*-inverts, so values shift but every recorded threshold is expected to hold. Verify per channel |
| Cull enum + comment | `SceneRenderer.cpp:779` | covered by the material-assignment cases |
| Flag declared | `SurfacePipeline.cpp` | none — `false` is what it already was |
| unchanged | `material_assignment` (exact (0,255,0), (0,0,0), checkerboard counts), `scene_draws_mesh`, `present_blit`, `render_stack_composites` | all orientation-independent — no light, or no lighting assertions |

Roughly: **6 files re-wound, 2 scenes re-aimed, 2 engine lines, zero exact
baselines expected to move** — against a gpu suite of 22 cases / 505
assertions. The monotonic-growth argument stands regardless of how small this
is today: T0086 adds shadow-bias values and a shadow-pass cull choice, T0087
adds view-dependent IBL baselines, T0143 adds per-feature pixel tests, and
every one of them calibrated against inverted assets makes this ticket's
change strictly more expensive. **Do it before T0086 starts. That is the
whole sequencing argument, and the numbers above are why it is cheap now and
will not be later.**

## What other engines do (research, 2026-08-06, sources in D33)

Every surveyed engine ends at the same invariant — glTF front = hardware
front, single-sided culls back — and they differ only in where the mirrors
sit, which is why comparing raw enums across engines misleads:

- **Godot 4**: right-handed, −Z forward, clockwise front
  (`rendering_device_commons.h`), Y-flip in the projection; glTF converted
  at import.
- **Filament**: right-handed, CCW front (`Conversion.cpp:674`), viewport
  repositioned without height negation; glTF native.
- **Unity**: left-handed, clockwise front (official manual), axis-flip at
  import.
- **bgfx**: default state culls CW, so CCW front.
- **Diligent's GLTFViewer**: D3D conventions + one model mirror + `true`.
- **HollowPoint**: D3D conventions + zero mirrors + `false`. Same invariant,
  fewest moving parts.

"Left-handed view + clockwise front" is D3D's historical pairing and is
coherent (`D3DCULL_CCW` — cull CCW backs — is the D3D9 default); the engine
did arrive at it by defaulting rather than deciding, and this ticket is the
deciding.

## Notes / findings

*(2026-08-06, at creation)* Two convention splits surfaced during the trace
and are recorded here so they are not rediscovered:

- **The engine already mixes axis conventions between components**: the
  camera's forward is +Z (Diligent/D3D, via the LH projection), the light's
  forward is −Z (`Light.cpp:25–34`, deliberately per KHR_lights_punctual).
  Both are individually reasoned and jointly undocumented; the winding header
  is where the facts get a fixed address.
- **`buildEngineSurfacePipeline`'s validation key uses `CULL_MODE_BACK`**
  (`ShaderSources.cpp:159`) — a PSO permutation the shipping renderer
  currently never draws (it emits only `NONE` and `FRONT`). After 152.4 the
  validation permutation matches reality again; until then it validates a
  pipeline nothing uses.

**Not verified by this ticket's author:** no gpu suite was executed during
the trace — every hardware-dependent claim above cites T0141.12's recorded
probes (`SV_IsFrontFace` false, the cull-enum flip, the lit suite under
`FrontCounterClockwise = true`), and the geometric analysis is static and
checkable by eye against the files cited. The display-mirror claim is derived
and cross-anchored but not yet observed on a screen; 152.6 is its probe.
