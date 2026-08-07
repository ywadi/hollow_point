// The engine's winding convention (T0152, amended by T0165).
//
// **Deliberately a header of its own, and deliberately tiny**, for exactly
// `DepthConvention.hpp`'s reasons: the things that must agree with it —
// pipeline rasterizer state, the glTF importer, the shadow passes, the test
// assets — have no business paying for Diligent's headers to read two bools.
//
// So this header includes only `HandednessConvention.hpp`, which also
// includes nothing.
#pragma once

#include <hp/HandednessConvention.hpp>

namespace hp {

/// Whether the importer mirrors content — negates one axis — to convert
/// glTF's coordinate space into the engine's.
///
/// **False: imported geometry passes through untouched, and this is now a
/// decision rather than an accident** (T0165, D33 as amended). The engine's
/// camera space is right-handed like glTF's (`kRightHandedCameraSpace`), so
/// there is nothing left to convert: what a Blender artist authored is what
/// the engine stores, debugs and displays.
///
/// **The Unity-style import mirror was considered and rejected.** It fixes
/// the display and leaves the *boundary* permanently converted — every future
/// attribute type owes a conversion, and a missed one renders subtly mirrored
/// rather than failing. Flipping this to true reverses apparent winding once,
/// so `kFrontFaceCounterClockwise` must flip with it; the `static_assert`
/// below is what enforces that at compile time rather than as a code-review
/// hope.
inline constexpr bool kImportMirrorsContent = false;

/// The number of mirrors between authored glTF space and the framebuffer.
///
/// **This is the quantity the engine actually depends on, and modelling it
/// with one term too few is how T0165 nearly went wrong.** Each entry is an
/// independent place a determinant can go negative on the way from a glTF
/// index buffer to the rasteriser's facing decision; only the *parity* of the
/// total matters, because each mirror flips apparent winding exactly once.
///
/// - **The importer** (`kImportMirrorsContent`) — an axis negation applied to
///   every position and normal at load. Currently none.
/// - **The camera chain** (`kRightHandedCameraSpace`) — the screen frame
///   (screen-right, screen-up, toward-viewer) expressed in world coordinates.
///   Under a right-handed camera that frame is `(+X, +Y, +Z)` of the camera's
///   own basis, which is right-handed: no mirror. Under a left-handed one —
///   what the engine had until T0165 — it is `(+X, +Y, -Z)`, which is
///   left-handed: **one mirror**, and the one T0152's chirality probe
///   measured on screen.
///
/// **Not counted here, deliberately: per-node mirrors.** A negative-determinant
/// node transform is the glTF spec's own determinant clause, it varies per
/// draw, and it is handled per draw (T0152.5) — on the CPU by flipping the
/// cull face and in `HpSurface.slang` by flipping the facing bit. This
/// constant is the engine's *global* baseline, which is what a compile-time
/// constant can honestly be.
inline constexpr int kChainMirrorCount =
    (kImportMirrorsContent ? 1 : 0) + (kRightHandedCameraSpace ? 0 : 1);

/// Whether counter-clockwise framebuffer winding is front-facing.
///
/// **True, declared rather than defaulted, and this is the single place that
/// decides it.** Vulkan has no preferred winding — facing is the sign of the
/// triangle's area in framebuffer coordinates, and both `VkFrontFace` values
/// are equally standard. The convention that binds this engine is glTF's, its
/// only mesh format: front faces wind counter-clockwise seen from the front.
///
/// Since T0165 the engine's chain contains **zero mirrors** — the importer
/// converts nothing, the view is a rigid inverse, the right-handed projection
/// negates only the Z row (no XY mirror), reverse-Z touches only the Z
/// column, and Diligent's Vulkan backend folds its internal viewport flip
/// into this flag's D3D screen-space semantics. So a glTF front face facing
/// the camera arrives at the rasteriser **counter-clockwise as displayed**,
/// and counter-clockwise-front — this value — is the setting under which
/// **hardware facing equals glTF facing**, which is the invariant everything
/// downstream assumes.
///
/// **This value moved once, and the move is the whole of T0165.** It was
/// `false` while the camera was left-handed, because that chain carried one
/// mirror. The invariant did not change; the mirror count did. That is why
/// the `static_assert` below counts mirrors rather than comparing two bools:
/// the old form modelled the importer as the only possible mirror source, so
/// a handedness change would have had to be smuggled past it by flipping a
/// constant that should not move.
///
/// **It is not free, and the cost is that it is not local.** Five things must
/// agree, and any one left behind produces a failure that does not look like
/// a winding bug:
///
/// 1. Every pipeline's `RasterizerStateDesc::FrontCounterClockwise` is this
///    constant. A pipeline that defaults it happens to agree today and stops
///    agreeing the day the convention moves — silently.
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
///    framebuffer — an import-time axis flip, a camera-space handedness
///    change, a negative camera-parent scale, a render-to-texture blit that
///    flips Y inside the scene pass — toggles apparent winding once, and this
///    flag must toggle with it. The first two are `kChainMirrorCount`'s two
///    terms; the last two are **not modelled**, because they are per-scene
///    rather than per-build, and the trigger for modelling them is written
///    down on T0152.5. A mirror left uncompensated does not look like a
///    mirror: it looks like every single-sided mesh in the world vanishing.
///
/// **Written as a literal, not as `kChainMirrorCount % 2 == 0`.** Deriving it
/// would make the assert below a tautology, and the assert is the point: the
/// value a pipeline actually rasterises with, and the mirror count it must
/// agree with, are stated independently so that disagreeing about them is a
/// compile error rather than a rendering one.
inline constexpr bool kFrontFaceCounterClockwise = true;

static_assert(kFrontFaceCounterClockwise == ((kChainMirrorCount % 2) == 0),
              "Each mirror between authored glTF space and the framebuffer flips "
              "apparent winding exactly once, so the front-face flag is the parity "
              "of the mirror count. Move one without the other and single-sided "
              "geometry inverts engine-wide (T0152, T0165).");

} // namespace hp
