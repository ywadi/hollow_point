#include <hp/CameraSystem.hpp>

#include <hp/Log.hpp>
#include <hp/Profiling.hpp>

#include <algorithm>
#include <cmath>

namespace hp {
namespace {

const LogCategory kLog("camera");

/// Normalises a plane so `dot(n, p) + w` is a true signed distance.
///
/// Not cosmetic: an unnormalised plane still answers inside/outside correctly,
/// but every distance-based test built on it -- LOD selection, soft culling
/// margins -- silently scales with the matrix.
float4 normalisePlane(const float4& plane) {
    const float length = std::sqrt(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
    if (!(length > 0.0F)) {
        return plane;
    }
    return float4(plane.x / length, plane.y / length, plane.z / length, plane.w / length);
}

} // namespace

bool Frustum::contains(const float3& point) const {
    return intersectsSphere(point, 0.0F);
}

bool Frustum::intersectsSphere(const float3& centre, float radius) const {
    const float r = radius > 0.0F ? radius : 0.0F;
    for (const float4& plane : planes) {
        const float distance = plane.x * centre.x + plane.y * centre.y + plane.z * centre.z
                               + plane.w;
        if (distance < -r) {
            return false;
        }
    }
    return true;
}

float effectiveAspect(AspectPolicy policy, float viewportAspect, float referenceAspect) {
    if (!(viewportAspect > 0.0F)) {
        return 1.0F;
    }
    switch (policy) {
    case AspectPolicy::FreeAspect:
        return viewportAspect;
    case AspectPolicy::ClampHorizontalFov:
        // Only *wider* than the reference is clamped. A narrower window already
        // sees less horizontally, and clamping it would widen the view on a
        // 4:3 monitor -- the opposite of what the policy is for.
        if (!(referenceAspect > 0.0F) || viewportAspect <= referenceAspect) {
            return viewportAspect;
        }
        return referenceAspect;
    case AspectPolicy::Letterbox:
        // The projection is built at the reference aspect and drawn into an
        // inset viewport of that shape, so the framing is identical everywhere.
        return referenceAspect > 0.0F ? referenceAspect : viewportAspect;
    }
    return viewportAspect;
}

ViewportRect letterboxViewport(ViewportRect rect, float viewportAspect, float referenceAspect) {
    if (!(viewportAspect > 0.0F) || !(referenceAspect > 0.0F)) {
        return rect;
    }
    if (std::abs(viewportAspect - referenceAspect) < 1.0e-5F) {
        return rect;
    }

    if (viewportAspect > referenceAspect) {
        // Too wide: pillarbox, keeping full height.
        const float scale = referenceAspect / viewportAspect;
        const float width = rect.width * scale;
        rect.x += (rect.width - width) * 0.5F;
        rect.width = width;
    } else {
        // Too tall: letterbox, keeping full width.
        const float scale = viewportAspect / referenceAspect;
        const float height = rect.height * scale;
        rect.y += (rect.height - height) * 0.5F;
        rect.height = height;
    }
    return rect;
}

std::optional<Entity> resolveCamera(Scene& scene, std::uint8_t viewSlot) {
    HP_PROFILE_ZONE();

    auto& registry = scene.registry();

    entt::entity best = entt::null;
    int bestPriority = 0;
    bool tied = false;

    for (auto [entity, camera] : registry.view<const Camera>().each()) {
        if (!camera.enabled || camera.viewSlot != viewSlot) {
            continue;
        }
        if (best == entt::null || camera.priority > bestPriority) {
            best = entity;
            bestPriority = camera.priority;
            tied = false;
        } else if (camera.priority == bestPriority) {
            tied = true;
        }
    }

    if (best == entt::null) {
        return std::nullopt;
    }
    if (tied) {
        // Registry order decides, and registry order is not a stable guarantee
        // -- an unrelated edit can change which camera wins, and the scene that
        // rendered correctly yesterday renders from somewhere else today. Say so
        // rather than picking silently.
        HP_LOG_WARN(kLog,
                    "two or more enabled cameras share priority {} on view slot {}; the winner is "
                    "registry order, which is not stable. Give one a higher priority.",
                    bestPriority, static_cast<int>(viewSlot));
    }
    return Entity(scene, best);
}

std::optional<ResolvedView> buildView(Entity entity, int targetWidth, int targetHeight,
                                      ClipSpace clip, const float4x4& viewOffset) {
    HP_PROFILE_ZONE();

    if (targetWidth <= 0 || targetHeight <= 0 || !entity.valid()) {
        return std::nullopt;
    }

    // The handle carries its own scene, so nothing here needs one passed in --
    // which also removes the chance of being handed an entity from a different
    // scene than the one queried.
    const Camera* camera = entity.tryGet<Camera>();
    const WorldTransform* world = entity.tryGet<WorldTransform>();
    if (camera == nullptr || world == nullptr) {
        return std::nullopt;
    }
    if (!camera->viewport.valid()) {
        HP_LOG_ERROR(kLog, "camera has an invalid viewport rect ({}, {}, {} x {}); not rendering",
                     camera->viewport.x, camera->viewport.y, camera->viewport.width,
                     camera->viewport.height);
        return std::nullopt;
    }

    ResolvedView resolved;
    resolved.entity = entity.raw();
    resolved.camera = *camera;

    // The camera's own rect, then whatever the aspect policy insets.
    ViewportRect rect = camera->viewport;
    const float rectWidthPx = static_cast<float>(targetWidth) * rect.width;
    const float rectHeightPx = static_cast<float>(targetHeight) * rect.height;
    if (!(rectWidthPx >= 1.0F) || !(rectHeightPx >= 1.0F)) {
        return std::nullopt;
    }
    const float rectAspect = rectWidthPx / rectHeightPx;

    if (camera->aspectPolicy == AspectPolicy::Letterbox) {
        rect = letterboxViewport(rect, rectAspect, camera->referenceAspect);
    }

    resolved.viewportX = static_cast<int>(std::lround(static_cast<double>(rect.x) * targetWidth));
    resolved.viewportY = static_cast<int>(std::lround(static_cast<double>(rect.y) * targetHeight));
    resolved.viewportWidth =
        std::max(1, static_cast<int>(std::lround(static_cast<double>(rect.width) * targetWidth)));
    resolved.viewportHeight =
        std::max(1, static_cast<int>(std::lround(static_cast<double>(rect.height) * targetHeight)));

    resolved.aspect =
        effectiveAspect(camera->aspectPolicy, rectAspect, camera->referenceAspect);

    // 81.9's seam. The offset multiplies the *world* transform, so shake and
    // recoil never touch the entity -- which matters because the audio listener
    // and T0100's late update both read that transform, and a shaking listener
    // is an audible bug.
    const float4x4 cameraWorld = viewOffset * world->current;
    resolved.view = cameraWorld.Inverse();

    // T0111.2's jitter seam is here: projection is assembled once, so sub-pixel
    // offsets for TAA go in at this point rather than in every consumer.
    resolved.projection = projectionMatrix(*camera, resolved.aspect, clip);
    if (resolved.projection == float4x4::Identity()) {
        HP_LOG_ERROR(kLog, "camera lens is unusable (near {}, far {}, fov {}); not rendering",
                     camera->nearPlane, camera->farPlane, camera->verticalFov);
        return std::nullopt;
    }

    resolved.viewProjection = resolved.view * resolved.projection;
    return resolved;
}

Frustum extractFrustum(const float4x4& viewProjection, ClipSpace clip) {
    HP_PROFILE_ZONE();

    // Gribb-Hartmann: each plane is a sum or difference of matrix rows, because
    // clipping is exactly the comparison of a clip coordinate against w. Derived
    // from the matrix rather than the lens, which is why it needs no knowledge
    // of reverse-Z -- the matrix already carries it.
    const float4x4& m = viewProjection;

    Frustum frustum;
    // left: x >= -w
    frustum.planes[0] = float4(m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41);
    // right: x <= w
    frustum.planes[1] = float4(m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41);
    // bottom: y >= -w
    frustum.planes[2] = float4(m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42);
    // top: y <= w
    frustum.planes[3] = float4(m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42);

    // The near plane is the one that depends on the clip-space convention: on a
    // [0, 1] clip space it is z >= 0, on [-1, 1] it is z >= -w. Getting this
    // wrong culls geometry just in front of the camera on exactly one backend,
    // which is the failure T0130.3 exists to prevent.
    if (clip.negativeOneToOneZ()) {
        frustum.planes[4] = float4(m._14 + m._13, m._24 + m._23, m._34 + m._33, m._44 + m._43);
    } else {
        frustum.planes[4] = float4(m._13, m._23, m._33, m._43);
    }
    // far: z <= w
    frustum.planes[5] = float4(m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43);

    for (float4& plane : frustum.planes) {
        plane = normalisePlane(plane);
    }
    return frustum;
}

bool worldToScreen(const ResolvedView& view, const float3& world, float& outX, float& outY,
                   float& outDepth) {
    const float4 clip = float4(world.x, world.y, world.z, 1.0F) * view.viewProjection;

    // A point behind the camera has w <= 0 and projects to a perfectly plausible
    // on-screen position. Reporting it as visible is how a world-space marker
    // ends up mirrored behind the player, so this is a hard refusal rather than
    // a clamp.
    if (!(clip.w > 0.0F)) {
        return false;
    }

    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;

    outX = static_cast<float>(view.viewportX)
           + (ndcX * 0.5F + 0.5F) * static_cast<float>(view.viewportWidth);
    // NDC y is up, pixels are down.
    outY = static_cast<float>(view.viewportY)
           + (1.0F - (ndcY * 0.5F + 0.5F)) * static_cast<float>(view.viewportHeight);
    outDepth = clip.z / clip.w;
    return true;
}

bool screenToWorldRay(const ResolvedView& view, float screenX, float screenY, float3& outOrigin,
                      float3& outDirection) {
    if (view.viewportWidth <= 0 || view.viewportHeight <= 0) {
        return false;
    }

    const float localX = screenX - static_cast<float>(view.viewportX);
    const float localY = screenY - static_cast<float>(view.viewportY);
    if (localX < 0.0F || localY < 0.0F || localX > static_cast<float>(view.viewportWidth)
        || localY > static_cast<float>(view.viewportHeight)) {
        return false;
    }

    const float ndcX = (localX / static_cast<float>(view.viewportWidth)) * 2.0F - 1.0F;
    const float ndcY = 1.0F - (localY / static_cast<float>(view.viewportHeight)) * 2.0F;

    const float4x4 inverse = view.viewProjection.Inverse();

    // Under reverse-Z the near plane is 1 and the far plane is 0, so the ray is
    // built from 1 towards 0 -- the opposite of the conventional reading, and
    // exactly the kind of place a depth convention silently inverts a result.
    const float nearZ = kReverseZ ? 1.0F : 0.0F;
    const float farZ = kReverseZ ? 0.0F : 1.0F;

    const float4 nearPoint = float4(ndcX, ndcY, nearZ, 1.0F) * inverse;
    const float4 farPoint = float4(ndcX, ndcY, farZ, 1.0F) * inverse;
    if (!(std::abs(nearPoint.w) > 0.0F) || !(std::abs(farPoint.w) > 0.0F)) {
        return false;
    }

    const float3 origin =
        float3(nearPoint.x / nearPoint.w, nearPoint.y / nearPoint.w, nearPoint.z / nearPoint.w);
    const float3 target =
        float3(farPoint.x / farPoint.w, farPoint.y / farPoint.w, farPoint.z / farPoint.w);

    float3 direction = target - origin;
    const float length =
        std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    if (!(length > 0.0F)) {
        return false;
    }
    direction = float3(direction.x / length, direction.y / length, direction.z / length);

    outOrigin = origin;
    outDirection = direction;
    return true;
}

} // namespace hp
