# `<hp/Math.hpp>`

*Generated from `engine/include/hp/Math.hpp` — do not edit.*

```cpp
#include <hp/Math.hpp>
```

6 public declaration(s), 6 documented.

## `float2`

```cpp
using float2 = Diligent::float2
```

 Two floats. `Diligent::float2` under a name that reads correctly in `hp::`
 code — an alias, not a wrapper, so it is the same type the renderer takes and
 no conversion exists to get wrong.

## `float3`

```cpp
using float3 = Diligent::float3
```

 Three floats — positions, scales, directions, Euler angles.

## `float4`

```cpp
using float4 = Diligent::float4
```

 Four floats. Also the colour type; there is no separate colour struct.

## `float3x3`

```cpp
using float3x3 = Diligent::float3x3
```

 A 3×3 float matrix, for normal matrices and pure rotations.

## `float4x4`

```cpp
using float4x4 = Diligent::float4x4
```

 A 4×4 float matrix.

 **Row-major, and multiplied left to right in the order transforms apply** —
 `World * View * Proj`, not the other way round. Diligent uses Direct3D-style
 math and documents this at the top of `BasicMath.hpp`; getting it backwards
 is the single most common way a transform ends up mirrored.

## `Quaternion`

```cpp
using Quaternion = Diligent::QuaternionF
```

 A float quaternion, for rotations.

 Named `Quaternion` here rather than Diligent's `QuaternionF`, since there is
 no double-precision variant in use and the `F` suffix reads as noise.
