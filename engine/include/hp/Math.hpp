// The math types, and the one place Diligent's headers are exposed publicly
// (T0021, T0056, D21).
//
// **This header exists to make one dependency deliberate instead of accidental.**
// The conventions document says gameplay uses Diligent's math types directly
// rather than a wrapper, because every renderer call takes them and a second
// vector library means conversions at every boundary, forever, plus a
// row-versus-column-major disagreement that produces a transposed matrix once a
// month. That decision stands.
//
// What was not known when it was taken is that **the math cannot be had without
// the RHI**. `BasicMath.hpp` includes `HashUtils.hpp` — for `ComputeHash`, used
// only by the `std::hash` specializations at the very bottom of the file — and
// `HashUtils.hpp` includes `Sampler.h`, `PipelineState.h`, `TextureView.h`,
// `PipelineResourceSignature.h` and `VertexPool.h` to hash *their* descriptors.
// So `#include "BasicMath.hpp"` transitively defines `IRenderDevice`,
// `IDeviceContext`, `IPipelineState`, `ISampler` and `ITextureView`.
//
// Measured on 2026-08-04, not assumed: a translation unit containing nothing but
// `#include "BasicMath.hpp"` preprocesses to **146,280 lines across 74 distinct
// Diligent headers**, and adding it to a TU that already includes entt costs
// **+594 ms** (1557 → 2151 ms, +38%, zig 0.16.0, three-run mean).
//
// That collides head-on with `hp/Render.hpp`'s rule that no public header may
// name a Diligent type, whose stated reason is that doing so hands every
// consumer Diligent's include path. The collision is resolved in favour of the
// math policy and recorded as **D21** — but the resolution is narrow, and the
// narrowness is the point:
//
//   * Only the **include path** is public, never the libraries. `Diligent-Common`
//     is a static library; linking it PUBLIC would put its code in every
//     gameplay module, which is the statics-per-artifact hazard that produced
//     T0105.1's dangling `__cxa_atexit` and T0127's typeinfo mismatch. The
//     graphics engines stay PRIVATE, so the RHI types are *visible* to gameplay
//     and *unlinkable* by it — declared, with no symbol behind them.
//   * `hp/Render.hpp` stays a pimpl. Nothing about D21 makes it acceptable for
//     an engine header to hand out an `IRenderDevice`; the swap chain and device
//     remain the render layer's business.
//   * **This is the only public header that includes Diligent.** If Diligent ever
//     splits the hashers out of `BasicMath.hpp` — the leak is an artifact of
//     their header layout, not of anything intrinsic — this is the single file
//     that changes.
//
// The obvious alternative, defining `hp::Vec3` and converting at the boundary,
// is what the conventions document already rejected and D21 does not reopen.
// Forward declaration does not work either: a component holds a transform *by
// value*, so the type must be complete.
#pragma once

#include "BasicMath.hpp"

namespace hp {

/// Two floats. `Diligent::float2` under a name that reads correctly in `hp::`
/// code — an alias, not a wrapper, so it is the same type the renderer takes and
/// no conversion exists to get wrong.
using float2 = Diligent::float2;

/// Three floats — positions, scales, directions, Euler angles.
using float3 = Diligent::float3;

/// Four floats. Also the colour type; there is no separate colour struct.
using float4 = Diligent::float4;

/// A 3×3 float matrix, for normal matrices and pure rotations.
using float3x3 = Diligent::float3x3;

/// A 4×4 float matrix.
///
/// **Row-major, and multiplied left to right in the order transforms apply** —
/// `World * View * Proj`, not the other way round. Diligent uses Direct3D-style
/// math and documents this at the top of `BasicMath.hpp`; getting it backwards
/// is the single most common way a transform ends up mirrored.
using float4x4 = Diligent::float4x4;

/// A float quaternion, for rotations.
///
/// Named `Quaternion` here rather than Diligent's `QuaternionF`, since there is
/// no double-precision variant in use and the `F` suffix reads as noise.
using Quaternion = Diligent::QuaternionF;

} // namespace hp
