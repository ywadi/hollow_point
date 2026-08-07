# `<hp/Camera.hpp>`

*Generated from `engine/include/hp/Camera.hpp` — do not edit.*

```cpp
#include <hp/Camera.hpp>
```

11 public declaration(s), 11 documented.

## `kDefaultSensorHeightMm`

```cpp
inline constexpr float kDefaultSensorHeightMm = 24.0F
```

 The height of the reference sensor, in millimetres, for a camera that does
 not name its own.

 24mm is full-frame 35mm (36 x 24), which is the frame most people mean when
 they say "50mm looks like this".

## `AspectPolicy`

```cpp
enum class AspectPolicy
```

| Enumerator | Value |
|---|---|
| `FreeAspect` | 0 |
| `ClampHorizontalFov` | 1 |
| `Letterbox` | 2 |

 What happens to the framing when the window is not the shape the camera was
 authored for (T0081.10).

 **This is a fairness question before it is a rendering one**, which is why it
 is a policy rather than a constant. On a 21:9 monitor, free aspect literally
 shows more world than 16:9 — an advantage in any game where what you can see
 is a mechanic.

 **Per camera rather than global, and that is deliberate.** A world camera can
 clamp for fairness while a HUD's orthographic camera stays free, and
 letterboxing a HUD camera is never the right answer. A single global setting
 could not express that. It is a plain field, so gameplay assigns it directly
 (D12) and the next frame's projection picks it up — there is no invalidation
 step to forget.

## `ViewportRect`

```cpp
struct ViewportRect
```

 A rectangle of a render target, in normalised coordinates.

 Normalised rather than pixels so it survives a resize: split-screen halves
 stay halves, and a picture-in-picture inset stays the same fraction of the
 screen, without anything recomputing them (T0081.4).

 The origin is the top-left corner, matching the render target rather than
 OpenGL's bottom-left convention — the engine has one texture-space
 convention and this follows it.

## `ViewportRect::valid`

```cpp
bool valid() const
```

 @returns whether the rectangle covers a non-empty area inside the target.

## `SurfaceDebugView`

```cpp
enum class SurfaceDebugView
```

| Enumerator | Value |
|---|---|
| `None` | 0 |
| `Texcoord0` | 1 |
| `BaseColor` | 3 |
| `Occlusion` | 5 |
| `Emissive` | 6 |
| `Metallic` | 7 |
| `Roughness` | 8 |
| `MeshNormal` | 12 |
| `ShadingNormal` | 13 |
| `ClearCoatFactor` | 21 |
| `ClearCoatRoughness` | 22 |
| `ClearCoatNormal` | 23 |
| `SheenColor` | 25 |
| `SheenRoughness` | 26 |
| `AnisotropyStrength` | 27 |
| `AnisotropyDirection` | 28 |
| `IridescenceFactor` | 30 |
| `IridescenceThickness` | 31 |
| `Transmission` | 32 |
| `Thickness` | 33 |

 What a view draws instead of the shaded image (T0141).

 **A surface has half a dozen inputs and one output, so a wrong one is
 invisible in the shaded frame.** A normal map that is never applied, an
 occlusion channel read from the wrong component, a roughness that is always
 1 — each of those produces an image that still looks like a lit material,
 and none of them can be told apart by eye or by an average colour. Rendering
 a single channel on its own is how each becomes a thing you can look at, and
 it is what every engine with a material editor provides.

 **The values match DiligentFX's `PBR_Renderer::DebugViewType`**, which is
 what `PBRRendererShaderParameters::DebugView` is declared to carry. The
 engine cannot name that enum in a public header (D21 keeps Diligent types
 out), so it is restated — and restated *with their numbering*, so the two
 cannot disagree about what a stored `3` means. Only the modes the engine's
 shader actually implements appear here; the gaps in the sequence are theirs,
 and a mode is added when the thing it shows exists.

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
float4x4 projectionMatrix(const Camera & camera, float aspect)
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
 @returns the projection matrix, or the identity when a parameter is out of
          range — which renders nothing recognisable rather than propagating a
          NaN through the transform chain, where it would present as
          disappearing geometry somewhere else entirely.

 (A `ClipSpace` parameter used to sit here, because OpenGL's [-1, 1] Z range
 meant the matrix depended on the device. Vulkan's [0, 1] range is a
 specification guarantee, so since T0144.3 the projection is built for it
 unconditionally.)
