// Which camera renders, and the matrices that follow from it (T0081).
//
// `hp/Camera.hpp` decides what a camera *describes*. This header decides which
// one is *active*, where it draws, and what view and projection it produces.
// The two are easy to conflate and the split is deliberate.
//
// **Nothing here owns state.** Resolution is a query over the scene, run each
// frame, returning a value. The alternative — an "active camera" pointer held
// somewhere — has to be invalidated when the entity is destroyed, when the scene
// is reloaded, when a module is unloaded, and it is exactly the kind of cached
// handle that outlives what it points at. A query cannot dangle.
//
// **The view matrix comes from the entity's `WorldTransform`** (T0101), so a
// camera on a boom arm or a head socket works with no special case. Cameras are
// not exempt from the hierarchy.
//
// **Two seams are left open on purpose**, because retrofitting either means
// threading a parameter through every consumer of a projection:
//
// - `ResolvedView::view` is produced *after* the camera is resolved, so an
//   additive offset — shake, recoil, sway, or a blend between two cameras — is
//   applied to the resolved view rather than by moving the camera entity
//   (81.9). Moving the entity pollutes the transform that the audio listener and
//   T0100's late update both read.
// - `buildView` assembles the projection in one place, which is where T0111.2's
//   sub-pixel jitter for TAA belongs.
#pragma once

#include <hp/Api.hpp>
#include <hp/Camera.hpp>
#include <hp/Scene.hpp>

#include <cstdint>
#include <optional>

namespace hp {

/// A camera resolved for one frame: which entity, and every matrix that follows.
///
/// A value, not a handle. It is safe to hold for the frame that produced it and
/// meaningless afterwards, which is the intended lifetime — recompute it rather
/// than storing it.
struct ResolvedView {
    /// The entity the camera component lives on.
    entt::entity entity{entt::null};

    /// A copy of the lens, so a consumer does not need the scene to read it.
    Camera camera{};

    /// World-to-view. The inverse of the camera's world transform, with the
    /// post-resolve offset (81.9) already applied if one was given.
    float4x4 view{};

    /// View-to-clip, honouring the aspect policy and the reverse-Z convention.
    float4x4 projection{};

    /// `view * projection`, precomputed because every consumer wants it and
    /// recomputing it per object is a matrix multiply nobody needs.
    float4x4 viewProjection{};

    /// The viewport in **pixels**, already resolved against the target size and
    /// any letterboxing the aspect policy implied.
    int viewportX = 0;

    /// Viewport top edge in pixels.
    int viewportY = 0;

    /// Viewport width in pixels. Never zero for a view that was built.
    int viewportWidth = 0;

    /// Viewport height in pixels. Never zero for a view that was built.
    int viewportHeight = 0;

    /// The aspect ratio the projection was actually built with, after the
    /// policy was applied. Reported rather than left implicit because a
    /// letterboxed view's aspect is *not* the window's, and code that assumes
    /// otherwise produces a subtly stretched image.
    float aspect = 1.0F;
};

/// The six planes of a view frustum, in world space.
///
/// **Extracted here and consumed elsewhere** — culling (T0045) and LOD selection
/// (T0040) both need it, and computing it in three places is how they drift and
/// start disagreeing about what is visible.
///
/// Each plane is stored as `xyz = normal, w = distance`, with the normal
/// pointing **into** the frustum, so a point is inside when
/// `dot(normal, point) + w >= 0` for all six.
struct Frustum {
    /// Left, right, bottom, top, near, far — in that order.
    float4 planes[6]{};

    /// @param point a world-space position.
    /// @returns whether the point is inside every plane.
    [[nodiscard]] HP_API bool contains(const float3& point) const;

    /// Tests a sphere, which is the cheap conservative test culling actually
    /// uses.
    /// @param centre sphere centre in world space.
    /// @param radius sphere radius. A negative radius tests as a point.
    /// @returns whether any part of the sphere is inside the frustum. False
    ///          positives are possible near the edges and are harmless; a false
    ///          negative would pop geometry out of view, and cannot happen here.
    [[nodiscard]] HP_API bool intersectsSphere(const float3& centre, float radius) const;
};

/// Picks the camera that should render a given view slot (T0081.2).
///
/// Highest `priority` among enabled cameras carrying that slot. **A tie is a
/// content bug and is logged**, because the fallback — registry order — is not a
/// stable guarantee and a scene that renders correctly today would change on an
/// unrelated edit.
///
/// @param scene the scene to search.
/// @param viewSlot which composited view to resolve for; a `RenderStack` layer
///        passes its own.
/// @returns the winning entity, or nothing when no enabled camera carries the
///          slot — which a caller must handle visibly rather than by rendering
///          an empty frame (see `buildView`).
[[nodiscard]] HP_API std::optional<Entity> resolveCamera(Scene& scene, std::uint8_t viewSlot = 0);

/// Builds every matrix for a resolved camera.
///
/// @param entity a camera entity, normally from `resolveCamera`. It carries its
///        own scene, so none is passed separately.
/// @param targetWidth render target width in pixels. Must be positive.
/// @param targetHeight render target height in pixels. Must be positive.
/// @param clip the device's clip-space convention, from
///        `RenderLayer::clipSpace()`.
/// @param viewOffset an additive world-space offset applied to the camera's
///        world transform *after* resolution (81.9) — shake, recoil, or a blend.
///        Identity by default. **This is the seam that keeps camera shake out of
///        the entity transform**, which the audio listener and late update both
///        read.
/// @returns the resolved view, or nothing when the entity has no `Camera`, no
///          `WorldTransform`, or the target size or lens is unusable.
[[nodiscard]] HP_API std::optional<ResolvedView> buildView(Entity entity, int targetWidth,
                                                           int targetHeight, ClipSpace clip,
                                                           const float4x4& viewOffset =
                                                               float4x4::Identity());

/// The aspect a projection should be built with, given the policy.
///
/// Split out because it is the whole of 81.10's decision and is worth testing on
/// its own, without a scene or a device.
/// @param policy what to do when the window is not `referenceAspect`.
/// @param viewportAspect the actual viewport's width divided by its height.
/// @param referenceAspect the aspect the camera was framed for.
/// @returns the aspect to project with. Equal to `viewportAspect` under
///          `FreeAspect`.
[[nodiscard]] HP_API float effectiveAspect(AspectPolicy policy, float viewportAspect,
                                           float referenceAspect);

/// The sub-rectangle a letterboxed view draws into.
///
/// @param rect the camera's own viewport rect, which letterboxing insets.
/// @param viewportAspect the aspect of `rect` once resolved against the target.
/// @param referenceAspect the aspect to preserve.
/// @returns the inset rectangle. Returns `rect` unchanged when it already
///          matches the reference aspect.
[[nodiscard]] HP_API ViewportRect letterboxViewport(ViewportRect rect, float viewportAspect,
                                                    float referenceAspect);

/// Extracts the six world-space frustum planes from a view-projection matrix
/// (81.6).
///
/// Works for any projection this engine produces, including reverse-Z, because
/// it is derived from the matrix rather than from the lens — which is also why
/// it does not need to know which convention built it.
/// @param viewProjection a combined view-projection matrix.
/// @param clip the device's clip-space convention, which decides how the near
///        plane is formed.
/// @returns the frustum, with normals pointing inwards and planes normalised.
[[nodiscard]] HP_API Frustum extractFrustum(const float4x4& viewProjection, ClipSpace clip);

/// Projects a world-space point to pixel coordinates (81.7).
///
/// @param view the resolved view to project through.
/// @param world a world-space position.
/// @param outX receives the x pixel coordinate, relative to the render target.
/// @param outY receives the y pixel coordinate, with y down.
/// @param outDepth receives the clip-space depth, which under reverse-Z is 1 at
///        the near plane and 0 at the far plane. Useful for depth-sorting UI
///        markers.
/// @returns false when the point is behind the camera or on the plane where the
///          projection is undefined, in which case the outputs are untouched.
///          **Callers must check this**: a point behind the camera projects to a
///          plausible on-screen position, which is how world-space UI markers
///          end up mirrored behind the player.
[[nodiscard]] HP_API bool worldToScreen(const ResolvedView& view, const float3& world, float& outX,
                                        float& outY, float& outDepth);

/// Builds a world-space ray from a pixel coordinate (81.7).
///
/// The inverse direction of `worldToScreen`, and what picking and click-to-move
/// both need.
/// @param view the resolved view to unproject through.
/// @param screenX x pixel coordinate relative to the render target.
/// @param screenY y pixel coordinate, y down.
/// @param outOrigin receives the ray origin on the near plane, in world space.
/// @param outDirection receives a normalised world-space direction.
/// @returns false when the view is not invertible or the pixel is outside the
///          viewport, in which case the outputs are untouched.
[[nodiscard]] HP_API bool screenToWorldRay(const ResolvedView& view, float screenX, float screenY,
                                           float3& outOrigin, float3& outDirection);

} // namespace hp
