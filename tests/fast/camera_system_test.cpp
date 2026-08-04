// Active-camera resolution, viewports, frustums and the screen helpers (T0081).
//
// Bucket: fast. All of it is a scene plus arithmetic; nothing here needs a
// device, because `buildView` takes the clip-space convention as a parameter
// precisely so it can be exercised without one.

#include <doctest/doctest.h>

#include <hp/CameraSystem.hpp>
#include <hp/Scene.hpp>

#include <cmath>

namespace {

/// A [0, 1] clip space, which is what every device the engine accepts reports.
hp::ClipSpace zeroToOne() {
    hp::ClipSpace clip;
    clip.minZ = 0.0F;
    clip.yToV = -0.5F;
    return clip;
}

/// A camera entity at a position, looking down +Z (the engine is left-handed).
hp::Entity makeCamera(hp::Scene& scene, const char* name, int priority, std::uint8_t slot = 0) {
    hp::Entity entity = scene.create(name);
    hp::Camera camera;
    camera.priority = priority;
    camera.viewSlot = slot;
    entity.add<hp::Camera>(camera);
    return entity;
}

} // namespace

TEST_CASE("resolution picks the highest priority enabled camera") {
    hp::Scene scene;
    makeCamera(scene, "low", 0);
    const hp::Entity high = makeCamera(scene, "high", 10);
    makeCamera(scene, "middle", 5);

    const auto resolved = hp::resolveCamera(scene);
    REQUIRE(resolved.has_value());
    CHECK(resolved->raw() == high.raw());
}

TEST_CASE("a disabled camera is not a candidate") {
    hp::Scene scene;
    const hp::Entity backup = makeCamera(scene, "backup", 0);
    hp::Entity main = makeCamera(scene, "main", 10);

    // Switching cameras is one field, which is the whole of "switching cameras
    // from gameplay is one call".
    main.get<hp::Camera>().enabled = false;

    const auto resolved = hp::resolveCamera(scene);
    REQUIRE(resolved.has_value());
    CHECK(resolved->raw() == backup.raw());
}

TEST_CASE("view slots are resolved independently") {
    // The case the whole slot mechanism exists for: a world layer and a HUD
    // layer each get their own camera without either knowing about the other's.
    hp::Scene scene;
    const hp::Entity world = makeCamera(scene, "world", 0, 0);
    const hp::Entity hud = makeCamera(scene, "hud", 0, 1);

    const auto worldView = hp::resolveCamera(scene, 0);
    const auto hudView = hp::resolveCamera(scene, 1);
    REQUIRE(worldView.has_value());
    REQUIRE(hudView.has_value());
    CHECK(worldView->raw() == world.raw());
    CHECK(hudView->raw() == hud.raw());

    // A slot nobody serves resolves to nothing rather than to a wrong camera.
    CHECK_FALSE(hp::resolveCamera(scene, 7).has_value());
}

TEST_CASE("a scene with no camera resolves to nothing") {
    hp::Scene scene;
    scene.create("not a camera");
    CHECK_FALSE(hp::resolveCamera(scene).has_value());
}

TEST_CASE("a view is built from the entity's world transform") {
    hp::Scene scene;
    hp::Entity entity = makeCamera(scene, "camera", 0);

    hp::Transform transform;
    transform.position = hp::float3(0.0F, 2.0F, -10.0F);
    scene.setLocalTransform(entity, transform);
    scene.propagateTransforms();

    const auto view = hp::buildView(entity, 1920, 1080, zeroToOne());
    REQUIRE(view.has_value());
    CHECK(view->viewportWidth == 1920);
    CHECK(view->viewportHeight == 1080);
    CHECK(view->aspect == doctest::Approx(1920.0F / 1080.0F));

    // A point at the origin is 10 units in front of a camera at z = -10, and
    // the engine is left-handed, so it must land at positive view-space z.
    const hp::float4 viewSpace = hp::float4(0.0F, 2.0F, 0.0F, 1.0F) * view->view;
    CHECK(viewSpace.z == doctest::Approx(10.0F));
    CHECK(viewSpace.x == doctest::Approx(0.0F));
    CHECK(viewSpace.y == doctest::Approx(0.0F));
}

TEST_CASE("a camera inherits its parent's transform") {
    // Cameras are not exempt from the hierarchy -- this is what makes a camera
    // on a boom arm or a head socket work with no special case (T0101).
    hp::Scene scene;
    hp::Entity rig = scene.create("rig");
    hp::Entity entity = makeCamera(scene, "camera", 0);
    REQUIRE(scene.setParent(entity, rig));

    hp::Transform rigTransform;
    rigTransform.position = hp::float3(5.0F, 0.0F, 0.0F);
    scene.setLocalTransform(rig, rigTransform);
    scene.propagateTransforms();

    const auto view = hp::buildView(entity, 800, 600, zeroToOne());
    REQUIRE(view.has_value());

    // The camera is at x = 5, so a world point at x = 5 is straight ahead.
    const hp::float4 viewSpace = hp::float4(5.0F, 0.0F, 4.0F, 1.0F) * view->view;
    CHECK(viewSpace.x == doctest::Approx(0.0F));
    CHECK(viewSpace.z == doctest::Approx(4.0F));
}

TEST_CASE("the view offset moves the view without touching the entity") {
    // 81.9's seam. Camera shake must not move the camera entity, because the
    // audio listener and T0100's late update both read that transform -- a
    // shaking listener is an audible bug with a visual-looking cause.
    hp::Scene scene;
    hp::Entity entity = makeCamera(scene, "camera", 0);
    scene.propagateTransforms();

    const float beforeX = scene.worldTransform(entity)._41;

    hp::float4x4 shake = hp::float4x4::Identity();
    shake._41 = 0.25F; // a quarter-unit kick sideways

    const auto view = hp::buildView(entity, 800, 600, zeroToOne(), shake);
    REQUIRE(view.has_value());

    // The entity did not move.
    CHECK(scene.worldTransform(entity)._41 == doctest::Approx(beforeX));

    // The view did. A point at x = 0.25 is now dead ahead.
    const hp::float4 viewSpace = hp::float4(0.25F, 0.0F, 3.0F, 1.0F) * view->view;
    CHECK(viewSpace.x == doctest::Approx(0.0F));
}

TEST_CASE("free aspect shows more when the window is wider") {
    // 81.10's default, and the reason it is a fairness question: a 21:9 window
    // genuinely sees more world than 16:9.
    CHECK(hp::effectiveAspect(hp::AspectPolicy::FreeAspect, 21.0F / 9.0F, 16.0F / 9.0F)
          == doctest::Approx(21.0F / 9.0F));
    CHECK(hp::effectiveAspect(hp::AspectPolicy::FreeAspect, 4.0F / 3.0F, 16.0F / 9.0F)
          == doctest::Approx(4.0F / 3.0F));
}

TEST_CASE("clamping horizontal fov caps a wider window but does not widen a narrower one") {
    const float reference = 16.0F / 9.0F;

    // Wider than the reference: held to the reference, so no advantage.
    CHECK(hp::effectiveAspect(hp::AspectPolicy::ClampHorizontalFov, 21.0F / 9.0F, reference)
          == doctest::Approx(reference));

    // Narrower: left alone. Clamping here would *widen* the view on a 4:3
    // monitor, which is the opposite of what the policy is for -- and is the
    // bug a one-line `min` would produce.
    CHECK(hp::effectiveAspect(hp::AspectPolicy::ClampHorizontalFov, 4.0F / 3.0F, reference)
          == doctest::Approx(4.0F / 3.0F));
}

TEST_CASE("letterboxing projects at the reference aspect whatever the window") {
    const float reference = 16.0F / 9.0F;
    CHECK(hp::effectiveAspect(hp::AspectPolicy::Letterbox, 21.0F / 9.0F, reference)
          == doctest::Approx(reference));
    CHECK(hp::effectiveAspect(hp::AspectPolicy::Letterbox, 4.0F / 3.0F, reference)
          == doctest::Approx(reference));
}

TEST_CASE("letterboxing insets the viewport and keeps it centred") {
    const float reference = 16.0F / 9.0F;
    hp::ViewportRect full;

    // Too wide: pillarbox, full height, bars at the sides.
    const hp::ViewportRect wide = hp::letterboxViewport(full, 21.0F / 9.0F, reference);
    CHECK(wide.height == doctest::Approx(1.0F));
    CHECK(wide.width < 1.0F);
    CHECK(wide.x == doctest::Approx((1.0F - wide.width) * 0.5F));

    // Too tall: letterbox, full width, bars top and bottom.
    const hp::ViewportRect tall = hp::letterboxViewport(full, 4.0F / 3.0F, reference);
    CHECK(tall.width == doctest::Approx(1.0F));
    CHECK(tall.height < 1.0F);
    CHECK(tall.y == doctest::Approx((1.0F - tall.height) * 0.5F));

    // Already the right shape: untouched, rather than nudged by rounding.
    const hp::ViewportRect exact = hp::letterboxViewport(full, reference, reference);
    CHECK(exact.width == doctest::Approx(1.0F));
    CHECK(exact.height == doctest::Approx(1.0F));
}

TEST_CASE("a letterboxed view reports the reference aspect, not the window's") {
    // The thing that goes wrong when this is left implicit: code reads the
    // window aspect, builds something with it, and the result is stretched by
    // exactly the letterbox ratio.
    hp::Scene scene;
    hp::Entity entity = makeCamera(scene, "camera", 0);
    entity.get<hp::Camera>().aspectPolicy = hp::AspectPolicy::Letterbox;
    entity.get<hp::Camera>().referenceAspect = 16.0F / 9.0F;
    scene.propagateTransforms();

    const auto view = hp::buildView(entity, 2560, 1080, zeroToOne());
    REQUIRE(view.has_value());
    CHECK(view->aspect == doctest::Approx(16.0F / 9.0F));
    // Pillarboxed: narrower than the window, full height.
    CHECK(view->viewportWidth < 2560);
    CHECK(view->viewportHeight == 1080);
}

TEST_CASE("a split-screen viewport resolves to pixels and survives a resize") {
    hp::Scene scene;
    hp::Entity entity = makeCamera(scene, "left", 0);
    entity.get<hp::Camera>().viewport = {0.0F, 0.0F, 0.5F, 1.0F};
    scene.propagateTransforms();

    const auto small = hp::buildView(entity, 1280, 720, zeroToOne());
    REQUIRE(small.has_value());
    CHECK(small->viewportWidth == 640);
    CHECK(small->viewportHeight == 720);
    CHECK(small->aspect == doctest::Approx(640.0F / 720.0F));

    // The same normalised rect at a different size is still half the screen --
    // which is why the rect is normalised rather than stored in pixels.
    const auto large = hp::buildView(entity, 3840, 2160, zeroToOne());
    REQUIRE(large.has_value());
    CHECK(large->viewportWidth == 1920);
    CHECK(large->viewportHeight == 2160);
}

TEST_CASE("an unusable camera or target yields no view rather than a broken one") {
    hp::Scene scene;
    hp::Entity entity = makeCamera(scene, "camera", 0);
    scene.propagateTransforms();

    CHECK_FALSE(hp::buildView(entity, 0, 720, zeroToOne()).has_value());
    CHECK_FALSE(hp::buildView(entity, 1280, -1, zeroToOne()).has_value());

    // An entity that is not a camera at all.
    hp::Entity plain = scene.create("plain");
    scene.propagateTransforms();
    CHECK_FALSE(hp::buildView(plain, 1280, 720, zeroToOne()).has_value());

    // A lens that cannot make a projection degrades to no view, which a caller
    // can show visibly -- rather than to an identity matrix that renders a
    // scene nobody can interpret.
    entity.get<hp::Camera>().nearPlane = 0.0F;
    CHECK_FALSE(hp::buildView(entity, 1280, 720, zeroToOne()).has_value());

    // An off-target viewport rect is refused rather than clamped.
    hp::Entity bad = makeCamera(scene, "bad", 0);
    bad.get<hp::Camera>().viewport = {0.8F, 0.0F, 0.5F, 1.0F};
    scene.propagateTransforms();
    CHECK_FALSE(hp::buildView(bad, 1280, 720, zeroToOne()).has_value());
}

TEST_CASE("the frustum contains what is in front and rejects what is not") {
    hp::Scene scene;
    hp::Entity entity = makeCamera(scene, "camera", 0);
    hp::Camera& camera = entity.get<hp::Camera>();
    camera.nearPlane = 1.0F;
    camera.farPlane = 100.0F;
    camera.verticalFov = 1.0F;
    scene.propagateTransforms();

    const auto view = hp::buildView(entity, 800, 600, zeroToOne());
    REQUIRE(view.has_value());
    const hp::Frustum frustum = hp::extractFrustum(view->viewProjection, zeroToOne());

    // Straight ahead, comfortably between the planes.
    CHECK(frustum.contains(hp::float3(0.0F, 0.0F, 10.0F)));
    CHECK(frustum.contains(hp::float3(0.0F, 0.0F, 99.0F)));

    // Behind the camera, and beyond the far plane.
    CHECK_FALSE(frustum.contains(hp::float3(0.0F, 0.0F, -10.0F)));
    CHECK_FALSE(frustum.contains(hp::float3(0.0F, 0.0F, 200.0F)));

    // Between the camera and the near plane -- the case that a wrong near-plane
    // sign gets backwards, and which culls geometry in front of the player.
    CHECK_FALSE(frustum.contains(hp::float3(0.0F, 0.0F, 0.5F)));

    // Far off to the side at close range.
    CHECK_FALSE(frustum.contains(hp::float3(100.0F, 0.0F, 5.0F)));
}

TEST_CASE("a sphere straddling a plane is not culled") {
    // The conservative direction matters: a false positive costs a wasted draw,
    // a false negative pops geometry out of view.
    hp::Scene scene;
    hp::Entity entity = makeCamera(scene, "camera", 0);
    entity.get<hp::Camera>().nearPlane = 1.0F;
    entity.get<hp::Camera>().farPlane = 100.0F;
    scene.propagateTransforms();

    const auto view = hp::buildView(entity, 800, 600, zeroToOne());
    REQUIRE(view.has_value());
    const hp::Frustum frustum = hp::extractFrustum(view->viewProjection, zeroToOne());

    // Centre is behind the far plane, but the sphere reaches back inside it.
    CHECK(frustum.intersectsSphere(hp::float3(0.0F, 0.0F, 105.0F), 10.0F));
    // Centre behind the camera, radius large enough to reach the near plane.
    CHECK(frustum.intersectsSphere(hp::float3(0.0F, 0.0F, -2.0F), 5.0F));
    // Genuinely nowhere near.
    CHECK_FALSE(frustum.intersectsSphere(hp::float3(0.0F, 0.0F, -500.0F), 10.0F));
}

TEST_CASE("frustum planes are normalised, so distances mean something") {
    // LOD selection and soft culling margins both use the plane distance as a
    // real distance. An unnormalised plane still answers inside/outside
    // correctly and silently scales every one of those.
    hp::Scene scene;
    hp::Entity entity = makeCamera(scene, "camera", 0);
    scene.propagateTransforms();

    const auto view = hp::buildView(entity, 800, 600, zeroToOne());
    REQUIRE(view.has_value());
    const hp::Frustum frustum = hp::extractFrustum(view->viewProjection, zeroToOne());

    for (const hp::float4& plane : frustum.planes) {
        const float length =
            std::sqrt(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
        CHECK(length == doctest::Approx(1.0F));
    }
}

TEST_CASE("world to screen and back is a round trip") {
    hp::Scene scene;
    hp::Entity entity = makeCamera(scene, "camera", 0);
    scene.propagateTransforms();

    const auto view = hp::buildView(entity, 1920, 1080, zeroToOne());
    REQUIRE(view.has_value());

    const hp::float3 world(1.5F, -0.75F, 12.0F);
    float x = 0.0F;
    float y = 0.0F;
    float depth = 0.0F;
    REQUIRE(hp::worldToScreen(*view, world, x, y, depth));

    // Reverse-Z: a point well inside the range is nearer 0 than 1, and it must
    // be strictly inside.
    CHECK(depth > 0.0F);
    CHECK(depth < 1.0F);

    hp::float3 origin;
    hp::float3 direction;
    REQUIRE(hp::screenToWorldRay(*view, x, y, origin, direction));

    // The ray must pass through the original point. Project it back rather than
    // comparing positions, because the ray origin is on the near plane and the
    // point is not.
    const float distance = std::sqrt((world.x - origin.x) * (world.x - origin.x)
                                     + (world.y - origin.y) * (world.y - origin.y)
                                     + (world.z - origin.z) * (world.z - origin.z));
    const hp::float3 along(origin.x + direction.x * distance, origin.y + direction.y * distance,
                           origin.z + direction.z * distance);
    CHECK(along.x == doctest::Approx(world.x).epsilon(0.01));
    CHECK(along.y == doctest::Approx(world.y).epsilon(0.01));
    CHECK(along.z == doctest::Approx(world.z).epsilon(0.01));
}

TEST_CASE("the screen centre maps to the centre of the viewport") {
    hp::Scene scene;
    hp::Entity entity = makeCamera(scene, "camera", 0);
    scene.propagateTransforms();

    const auto view = hp::buildView(entity, 1920, 1080, zeroToOne());
    REQUIRE(view.has_value());

    float x = 0.0F;
    float y = 0.0F;
    float depth = 0.0F;
    REQUIRE(hp::worldToScreen(*view, hp::float3(0.0F, 0.0F, 10.0F), x, y, depth));
    CHECK(x == doctest::Approx(960.0F));
    CHECK(y == doctest::Approx(540.0F));

    // Up in the world is up the screen, which means a *smaller* y in pixels.
    float upY = 0.0F;
    REQUIRE(hp::worldToScreen(*view, hp::float3(0.0F, 1.0F, 10.0F), x, upY, depth));
    CHECK(upY < 540.0F);
}

TEST_CASE("a point behind the camera is refused, not projected") {
    // The failure this prevents is specific and common: a point behind the
    // camera projects to a perfectly plausible on-screen position, so a
    // world-space marker appears mirrored behind the player.
    hp::Scene scene;
    hp::Entity entity = makeCamera(scene, "camera", 0);
    scene.propagateTransforms();

    const auto view = hp::buildView(entity, 1920, 1080, zeroToOne());
    REQUIRE(view.has_value());

    float x = -1.0F;
    float y = -1.0F;
    float depth = -1.0F;
    CHECK_FALSE(hp::worldToScreen(*view, hp::float3(0.0F, 0.0F, -5.0F), x, y, depth));
    // Untouched, so a caller that ignores the return value at least does not
    // get a plausible-looking coordinate.
    CHECK(x == -1.0F);
    CHECK(y == -1.0F);
}

TEST_CASE("a pixel outside the viewport produces no ray") {
    hp::Scene scene;
    hp::Entity entity = makeCamera(scene, "half", 0);
    entity.get<hp::Camera>().viewport = {0.5F, 0.0F, 0.5F, 1.0F};
    scene.propagateTransforms();

    const auto view = hp::buildView(entity, 1280, 720, zeroToOne());
    REQUIRE(view.has_value());
    CHECK(view->viewportX == 640);

    hp::float3 origin;
    hp::float3 direction;
    // In the left half, which this camera does not own.
    CHECK_FALSE(hp::screenToWorldRay(*view, 100.0F, 360.0F, origin, direction));
    // In its own half.
    CHECK(hp::screenToWorldRay(*view, 900.0F, 360.0F, origin, direction));
}

TEST_CASE("a resolved view carries the culling mask for T0045") {
    // Storage only -- nothing tests visibility against it yet. It is here so
    // that adding it after cameras are authored does not cost a migration.
    hp::Scene scene;
    hp::Entity entity = makeCamera(scene, "camera", 0);
    entity.get<hp::Camera>().cullingMask = 0x00000005U;
    scene.propagateTransforms();

    const auto view = hp::buildView(entity, 800, 600, zeroToOne());
    REQUIRE(view.has_value());
    CHECK(view->camera.cullingMask == 0x00000005U);
}
