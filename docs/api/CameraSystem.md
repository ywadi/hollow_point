# `<hp/CameraSystem.hpp>`

*Generated from `engine/include/hp/CameraSystem.hpp` — do not edit.*

```cpp
#include <hp/CameraSystem.hpp>
```

11 public declaration(s), 11 documented.

## `ResolvedView`

```cpp
struct ResolvedView
```

 A camera resolved for one frame: which entity, and every matrix that follows.

 A value, not a handle. It is safe to hold for the frame that produced it and
 meaningless afterwards, which is the intended lifetime — recompute it rather
 than storing it.

## `Frustum`

```cpp
struct Frustum
```

 The six planes of a view frustum, in world space.

 **Extracted here and consumed elsewhere** — culling (T0045) and LOD selection
 (T0040) both need it, and computing it in three places is how they drift and
 start disagreeing about what is visible.

 Each plane is stored as `xyz = normal, w = distance`, with the normal
 pointing **into** the frustum, so a point is inside when
 `dot(normal, point) + w >= 0` for all six.

## `Frustum::contains`

```cpp
bool contains(const float3 & point) const
```

 @param point a world-space position.
 @returns whether the point is inside every plane.

## `Frustum::intersectsSphere`

```cpp
bool intersectsSphere(const float3 & centre, float radius) const
```

 Tests a sphere, which is the cheap conservative test culling actually
 uses.
 @param centre sphere centre in world space.
 @param radius sphere radius. A negative radius tests as a point.
 @returns whether any part of the sphere is inside the frustum. False
          positives are possible near the edges and are harmless; a false
          negative would pop geometry out of view, and cannot happen here.

## `resolveCamera`

```cpp
std::optional<Entity> resolveCamera(Scene & scene, std::uint8_t viewSlot)
```

 Picks the camera that should render a given view slot (T0081.2).

 Highest `priority` among enabled cameras carrying that slot. **A tie is a
 content bug and is logged**, because the fallback — registry order — is not a
 stable guarantee and a scene that renders correctly today would change on an
 unrelated edit.

 @param scene the scene to search.
 @param viewSlot which composited view to resolve for; a `RenderStack` layer
        passes its own.
 @returns the winning entity, or nothing when no enabled camera carries the
          slot — which a caller must handle visibly rather than by rendering
          an empty frame (see `buildView`).

## `buildView`

```cpp
std::optional<ResolvedView> buildView(Entity entity, int targetWidth, int targetHeight, const float4x4 & viewOffset)
```

 Builds every matrix for a resolved camera.

 @param entity a camera entity, normally from `resolveCamera`. It carries its
        own scene, so none is passed separately.
 @param targetWidth render target width in pixels. Must be positive.
 @param targetHeight render target height in pixels. Must be positive.
 @param viewOffset an additive world-space offset applied to the camera's
        world transform *after* resolution (81.9) — shake, recoil, or a blend.
        Identity by default. **This is the seam that keeps camera shake out of
        the entity transform**, which the audio listener and late update both
        read.
 @returns the resolved view, or nothing when the entity has no `Camera`, no
          `WorldTransform`, or the target size or lens is unusable.

## `effectiveAspect`

```cpp
float effectiveAspect(AspectPolicy policy, float viewportAspect, float referenceAspect)
```

 The aspect a projection should be built with, given the policy.

 Split out because it is the whole of 81.10's decision and is worth testing on
 its own, without a scene or a device.
 @param policy what to do when the window is not `referenceAspect`.
 @param viewportAspect the actual viewport's width divided by its height.
 @param referenceAspect the aspect the camera was framed for.
 @returns the aspect to project with. Equal to `viewportAspect` under
          `FreeAspect`.

## `letterboxViewport`

```cpp
ViewportRect letterboxViewport(ViewportRect rect, float viewportAspect, float referenceAspect)
```

 The sub-rectangle a letterboxed view draws into.

 @param rect the camera's own viewport rect, which letterboxing insets.
 @param viewportAspect the aspect of `rect` once resolved against the target.
 @param referenceAspect the aspect to preserve.
 @returns the inset rectangle. Returns `rect` unchanged when it already
          matches the reference aspect.

## `extractFrustum`

```cpp
Frustum extractFrustum(const float4x4 & viewProjection)
```

 Extracts the six world-space frustum planes from a view-projection matrix
 (81.6).

 Works for any projection this engine produces, including reverse-Z, because
 it is derived from the matrix rather than from the lens — which is also why
 it does not need to know which convention built it. The near plane is
 formed for the [0, 1] clip space, which is the only one the engine has
 since T0144.3.
 @param viewProjection a combined view-projection matrix.
 @returns the frustum, with normals pointing inwards and planes normalised.

## `worldToScreen`

```cpp
bool worldToScreen(const ResolvedView & view, const float3 & world, float & outX, float & outY, float & outDepth)
```

 Projects a world-space point to pixel coordinates (81.7).

 @param view the resolved view to project through.
 @param world a world-space position.
 @param outX receives the x pixel coordinate, relative to the render target.
 @param outY receives the y pixel coordinate, with y down.
 @param outDepth receives the clip-space depth, which under reverse-Z is 1 at
        the near plane and 0 at the far plane. Useful for depth-sorting UI
        markers.
 @returns false when the point is behind the camera or on the plane where the
          projection is undefined, in which case the outputs are untouched.
          **Callers must check this**: a point behind the camera projects to a
          plausible on-screen position, which is how world-space UI markers
          end up mirrored behind the player.

## `screenToWorldRay`

```cpp
bool screenToWorldRay(const ResolvedView & view, float screenX, float screenY, float3 & outOrigin, float3 & outDirection)
```

 Builds a world-space ray from a pixel coordinate (81.7).

 The inverse direction of `worldToScreen`, and what picking and click-to-move
 both need.
 @param view the resolved view to unproject through.
 @param screenX x pixel coordinate relative to the render target.
 @param screenY y pixel coordinate, y down.
 @param outOrigin receives the ray origin on the near plane, in world space.
 @param outDirection receives a normalised world-space direction.
 @returns false when the view is not invertible or the pixel is outside the
          viewport, in which case the outputs are untouched.
