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
