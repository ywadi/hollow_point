# `<hp/DepthConvention.hpp>`

*Generated from `engine/include/hp/DepthConvention.hpp` — do not edit.*

```cpp
#include <hp/DepthConvention.hpp>
```

4 public declaration(s), 4 documented.

## `kReverseZ`

```cpp
inline constexpr bool kReverseZ = true
```

 Whether the engine maps the near plane to depth 1 and the far plane to 0.

 **True, and this is the single place that decides it.** Reverse-Z spends a
 float depth buffer's precision where perspective projection destroys it:
 `1/z` crowds almost every representable value into the first few percent of
 the range, and floating point crowds its own precision near zero. Pointing
 those two at each other cancels both, and turns z-fighting on distant
 geometry from a tuning problem into a non-problem.

 **It is not free, and the cost is that it is not local.** Choosing reverse-Z
 commits four things that must agree, and any one of them left behind
 produces a depth buffer that is precisely inverted — which does not look like
 a depth bug, it looks like the far geometry is in front:

 1. The projection matrix swaps near and far — `hp::projectionMatrix` does
    this.
 2. The depth buffer is a **float** format. `TargetFormat::Depth` is
    `D32_FLOAT` (T0046), chosen to keep this open. A `D24_UNORM` buffer gains
    nothing from reverse-Z: its precision is already uniform, so inverting the
    mapping moves the crowding rather than removing it.
 3. Every pipeline state's depth comparison is `GREATER_EQUAL` rather than
    `LESS_EQUAL` (**T0028**, which owns pipeline creation and must honour
    this).
 4. The depth clear is `kDepthClearValue`, not 1.0.

 **And it requires a [0, 1] clip space, which is why `hp::ClipSpace` exists
 and why it is declared below rather than beside the device that reports it.**
 On a [-1, 1] clip space the depth value is remapped through the midpoint
 before it reaches the buffer, which throws away exactly the precision
 reverse-Z was bought for. The engine turns on Diligent's `ZeroToOneNDZ` and
 refuses an OpenGL device that cannot honour it, so the [-1, 1] case does not
 exist here rather than being handled by a second code path.

 Deciding this now costs the lines below. Deciding it after T0028, T0045 and
 T0096 exist costs a sweep through every pipeline state and every shader that
 reconstructs position from depth, with a symptom that looks plausible at
 every intermediate stage.

## `kDepthClearValue`

```cpp
inline constexpr float kDepthClearValue = kReverseZ ? 0.0F : 1.0F
```

 The value a depth target is cleared to before rendering.

 0 under reverse-Z, because the far plane is 0 and a clear means "nothing has
 been drawn, everything is at infinity".

## `ClipSpace`

```cpp
struct ClipSpace
```

 The device's clip-space convention (T0130.3).

 **This is a property of the device, not of the engine, and the two backends
 disagree by default.** Vulkan clips Z to [0, 1]; OpenGL clips it to [-1, 1]
 unless `glClipControl` says otherwise, and Diligent gates that on an opt-in
 flag that defaults to off. The engine turns it on and refuses a device that
 cannot honour it, so in practice `minZ` is always 0 here — but the value is
 still read from the device rather than assumed, because "assumed" is how a
 projection matrix ends up right on one backend and mirrored on the other.

 **It lives in this header rather than next to `RenderLayer::clipSpace()`,
 which is where the value comes from**, because the things that must honour it
 are the same things that must honour reverse-Z, and several of them have no
 business including a swap chain to read two floats. `RenderPassContext`
 carries one so that a gameplay-authored layer (T0094) building its own
 projection cannot get it wrong by omission — and a default-constructed
 `ClipSpace` is deliberately *not* a usable one, since `yToV` of zero flips
 nothing in either direction.

## `ClipSpace::negativeOneToOneZ`

```cpp
bool negativeOneToOneZ() const
```

 @returns whether clip-space Z runs [-1, 1] rather than [0, 1]. This is
          the argument every Diligent projection helper takes, so it is
          spelled the way they spell it.
