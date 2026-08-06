// The engine's depth convention (T0130.3).
//
// **Deliberately a header of its own, and deliberately tiny.** The natural home
// for these is `hp/Camera.hpp`, next to the projection matrix that implements
// them — but that header includes `hp/Math.hpp`, which is 146,280 lines of
// Diligent and costs about 594 milliseconds per translation unit (D21). The
// depth convention is needed by things that have no interest in matrices at
// all: `RenderStack` for its clear value, and eventually every pipeline state
// in T0028. Making those pay for Diligent's maths headers to read two constants
// would be a bad trade, and the kind that is invisible until a build is slow
// for reasons nobody can point at.
//
// So this header includes nothing.
#pragma once

namespace hp {

/// Whether the engine maps the near plane to depth 1 and the far plane to 0.
///
/// **True, and this is the single place that decides it.** Reverse-Z spends a
/// float depth buffer's precision where perspective projection destroys it:
/// `1/z` crowds almost every representable value into the first few percent of
/// the range, and floating point crowds its own precision near zero. Pointing
/// those two at each other cancels both, and turns z-fighting on distant
/// geometry from a tuning problem into a non-problem.
///
/// **It is not free, and the cost is that it is not local.** Choosing reverse-Z
/// commits four things that must agree, and any one of them left behind
/// produces a depth buffer that is precisely inverted — which does not look like
/// a depth bug, it looks like the far geometry is in front:
///
/// 1. The projection matrix swaps near and far — `hp::projectionMatrix` does
///    this.
/// 2. The depth buffer is a **float** format. `TargetFormat::Depth` is
///    `D32_FLOAT` (T0046), chosen to keep this open. A `D24_UNORM` buffer gains
///    nothing from reverse-Z: its precision is already uniform, so inverting the
///    mapping moves the crowding rather than removing it.
/// 3. Every pipeline state's depth comparison is `GREATER_EQUAL` rather than
///    `LESS_EQUAL` (**T0028**, which owns pipeline creation and must honour
///    this).
/// 4. The depth clear is `kDepthClearValue`, not 1.0.
///
/// **And it requires a [0, 1] clip space.** On a [-1, 1] clip space the depth
/// value is remapped through the midpoint before it reaches the buffer, which
/// throws away exactly the precision reverse-Z was bought for. Vulkan — the
/// only backend since **D29** — clips Z to [0, 1] by specification, so this is
/// a guarantee rather than a device query. (It used to be a query: OpenGL
/// clips to [-1, 1] unless `glClipControl` says otherwise, and the engine
/// carried a `minZ` field and a second projection branch to refuse or serve
/// that case. T0144.3 removed them with the backend.)
///
/// Deciding this now costs the lines below. Deciding it after T0028, T0045 and
/// T0096 exist costs a sweep through every pipeline state and every shader that
/// reconstructs position from depth, with a symptom that looks plausible at
/// every intermediate stage.
inline constexpr bool kReverseZ = true;

/// The value a depth target is cleared to before rendering.
///
/// 0 under reverse-Z, because the far plane is 0 and a clear means "nothing has
/// been drawn, everything is at infinity".
inline constexpr float kDepthClearValue = kReverseZ ? 0.0F : 1.0F;

/// The device's texture-space convention (T0130.3, simplified by T0144.3).
///
/// This struct used to carry the clip-space Z range too (`minZ`, and a
/// `negativeOneToOneZ()` every projection consulted), because Vulkan and
/// OpenGL disagreed about it. D29 removed OpenGL, Vulkan's [0, 1] range is a
/// specification guarantee, and the projection now assumes it — so what
/// remains is the one convention that is still worth carrying around:
///
/// **Which way texture space runs.** `yToV` is the scale taking an NDC Y
/// coordinate to a texture V coordinate — **-0.5 on Vulkan** (Diligent's
/// `NDCAttribs::YtoVScale`; the magnitude is the whole 0.5 scale, not a sign).
/// A render-to-texture pass that ignores it samples its source vertically
/// flipped. Still read from the device rather than hardcoded, because it is
/// the device's property and reading it is what kept the present blit right
/// when the backends disagreed.
///
/// **It lives in this header rather than next to `RenderLayer::clipSpace()`,
/// which is where the value comes from**, because the things that honour it
/// sit beside the things that honour reverse-Z, and several of them have no
/// business including a swap chain to read one float. `RenderPassContext`
/// carries one so a gameplay-authored layer (T0094) flipping a
/// render-to-texture pass cannot get it wrong by omission — and a
/// default-constructed `ClipSpace` is deliberately *not* a usable one, since
/// `yToV` of zero flips nothing in either direction.
struct ClipSpace {
    /// Scale taking an NDC Y coordinate to a texture V coordinate. -0.5 on
    /// Vulkan.
    float yToV = 0.0F;
};

} // namespace hp
