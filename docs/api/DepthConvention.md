# `<hp/DepthConvention.hpp>`

*Generated from `engine/include/hp/DepthConvention.hpp` — do not edit.*

```cpp
#include <hp/DepthConvention.hpp>
```

2 public declaration(s), 2 documented.

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

 **And it requires a [0, 1] clip space, which is why `hp::ClipSpace` exists.**
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
