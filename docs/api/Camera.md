# `<hp/Camera.hpp>`

*Generated from `engine/include/hp/Camera.hpp` — do not edit.*

```cpp
#include <hp/Camera.hpp>
```

7 public declaration(s), 7 documented.

## `kDefaultSensorHeightMm`

```cpp
inline constexpr float kDefaultSensorHeightMm = 24.0F
```

 The height of the reference sensor, in millimetres, for a camera that does
 not name its own.

 24mm is full-frame 35mm (36 x 24), which is the frame most people mean when
 they say "50mm looks like this".

## `Camera`

```cpp
struct Camera
```

 A point of view. Which camera renders, and into what viewport, is not
 decided here (T0081).

## `verticalFovFromFocalLength`

```cpp
float verticalFovFromFocalLength(float focalLengthMm, float sensorHeightMm)
```

 Converts a photographic focal length to the vertical field of view it
 produces (130.1).
 @param focalLengthMm focal length in millimetres. Must be greater than zero.
 @param sensorHeightMm sensor height in millimetres, usually a camera's
        `sensorHeightMm`. Must be greater than zero.
 @returns the vertical field of view in radians, or 0 for a non-positive
          input rather than a NaN that propagates into a projection matrix and
          shows up as a blank screen three subsystems away.

## `focalLengthFromVerticalFov`

```cpp
float focalLengthFromVerticalFov(float verticalFov, float sensorHeightMm)
```

 Converts a vertical field of view to the focal length that produces it.

 The exact inverse of `verticalFovFromFocalLength` at the same sensor height.
 @param verticalFov vertical field of view in radians, in (0, pi).
 @param sensorHeightMm sensor height in millimetres. Must be greater than zero.
 @returns the focal length in millimetres, or 0 for an out-of-range input.

## `horizontalFovFromVertical`

```cpp
float horizontalFovFromVertical(float verticalFov, float aspect)
```

 Converts a vertical field of view to the horizontal one it implies at a given
 aspect ratio.

 Useful for culling and for UI that reports what a camera sees; the projection
 does not need it.
 @param verticalFov vertical field of view in radians, in (0, pi).
 @param aspect viewport width divided by height. Must be greater than zero.
 @returns the horizontal field of view in radians, or 0 for an out-of-range
          input.

## `exposureMultiplierFromEv100`

```cpp
float exposureMultiplierFromEv100(float ev100)
```

 Converts EV100 to the linear multiplier a shader applies to radiance.
 @param ev100 exposure value at ISO 100.
 @returns the linear multiplier. Always positive.

## `projectionMatrix`

```cpp
float4x4 projectionMatrix(const Camera & camera, float aspect, ClipSpace clip)
```

 Builds the projection matrix for a camera.

 **Left-handed, +Z into the screen**, which is Diligent's camera-space
 convention and not ours to reopen — its projection helpers, its
 `SetNearFarClipPlanes` and its sample shaders all assume it, and picking the
 other handedness means fighting every one of them for no gain.

 **Reverse-Z**, so the near plane maps to 1 and the far plane to 0. See
 `kReverseZ` for what else must agree.

 @param camera the lens. `nearPlane` and `farPlane` must be positive with
        near strictly less than far; `verticalFov` must be in (0, pi) for a
        perspective camera and `orthographicSize` positive for an
        orthographic one.
 @param aspect viewport width divided by height (130.2). Must be greater than
        zero. **Derived from the viewport by the caller, never stored on the
        camera**, because a stored aspect goes stale on the first resize.
 @param clip the device's clip-space convention, from
        `RenderLayer::clipSpace()`. Read from the device rather than assumed:
        it is the difference between a matrix that is right on Vulkan and
        mirrored on OpenGL.
 @returns the projection matrix, or the identity when a parameter is out of
          range — which renders nothing recognisable rather than propagating a
          NaN through the transform chain, where it would present as
          disappearing geometry somewhere else entirely.
