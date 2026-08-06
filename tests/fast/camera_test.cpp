// The camera lens model and its projection (T0130).
//
// Bucket: fast. All of this is arithmetic — no device is needed to check that a
// projection maps the near plane to 1. What *does* need a device is the
// clip-space convention the matrix is built against, and that is measured in the
// gpu bucket rather than assumed here.
//
// **These cases assert endpoints, not coefficients.** Checking `_33` against a
// hand-derived expression only proves the test and the implementation share a
// derivation; checking that a point on the near plane comes out at depth 1
// proves the thing that actually matters, and would have caught a reverse-Z
// convention applied to the perspective path and forgotten on the orthographic
// one.

#include <doctest/doctest.h>

#include <hp/Camera.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

constexpr float kPi = 3.14159265358979323846F;

/// Projects a point in view space and returns its NDC depth.
///
/// This is the whole point: it does the perspective divide, so it measures what
/// the rasteriser will see rather than what the matrix contains.
float ndcDepth(const hp::float4x4& projection, float viewZ) {
    const hp::float4 clip = hp::float4(0.0F, 0.0F, viewZ, 1.0F) * projection;
    return clip.w != 0.0F ? clip.z / clip.w : clip.z;
}

/// How many representable floats separate two values.
///
/// The honest unit for depth precision. An absolute difference says nothing on
/// its own, because a gap of 1e-7 is two steps near 1.0 and a hundred thousand
/// steps near 1e-5 -- and that ratio *is* what reverse-Z buys.
int ulpsBetween(float a, float b) {
    std::int32_t abits = 0;
    std::int32_t bbits = 0;
    std::memcpy(&abits, &a, sizeof abits);
    std::memcpy(&bbits, &b, sizeof bbits);
    // Both inputs here are non-negative, so the bit patterns are monotonic in
    // the value and a plain subtraction is the step count.
    return static_cast<int>(std::abs(abits - bbits));
}

/// A [0, 1] clip space, which is what every device the engine accepts reports.
hp::ClipSpace zeroToOne() {
    hp::ClipSpace clip;
    clip.minZ = 0.0F;
    clip.yToV = -0.5F;
    return clip;
}

} // namespace

TEST_CASE("the engine is on reverse-Z, and the clear value follows it") {
    // If this ever flips, every case below that asserts near->1 flips with it,
    // which is the point of asserting against the constant rather than a
    // literal.
    CHECK(hp::kReverseZ);
    CHECK(hp::kDepthClearValue == 0.0F);
}

TEST_CASE("a perspective projection puts the near plane at 1 and the far plane at 0") {
    hp::Camera camera;
    camera.nearPlane = 0.1F;
    camera.farPlane = 1000.0F;

    const hp::float4x4 projection = hp::projectionMatrix(camera, 16.0F / 9.0F, zeroToOne());

    // Reverse-Z. Getting this backwards renders a scene in which distant
    // geometry occludes near geometry -- which does not look like a depth bug,
    // it looks like the model is inside out.
    CHECK(ndcDepth(projection, camera.nearPlane) == doctest::Approx(1.0F));
    CHECK(ndcDepth(projection, camera.farPlane) == doctest::Approx(0.0F));

    // Monotonic in between, or depth testing is meaningless regardless of which
    // end is which.
    float previous = 1.0F;
    for (float z = 1.0F; z < 1000.0F; z *= 2.0F) {
        const float depth = ndcDepth(projection, z);
        CHECK(depth < previous);
        previous = depth;
    }
}

TEST_CASE("an orthographic projection uses the same reverse-Z convention") {
    // The failure this exists for: reverse-Z applied to the perspective path
    // and forgotten on the orthographic one. Both go through the same swap, and
    // the swap is *not* obviously correct for ortho -- it uses a different pair
    // of expressions -- so it is checked rather than assumed.
    hp::Camera camera;
    camera.orthographic = true;
    camera.orthographicSize = 5.0F;
    camera.nearPlane = 0.1F;
    camera.farPlane = 100.0F;

    const hp::float4x4 projection = hp::projectionMatrix(camera, 1.0F, zeroToOne());

    CHECK(ndcDepth(projection, camera.nearPlane) == doctest::Approx(1.0F));
    CHECK(ndcDepth(projection, camera.farPlane) == doctest::Approx(0.0F));
    CHECK(ndcDepth(projection, 50.05F) == doctest::Approx(0.5F));
}

TEST_CASE("the projection is Diligent's [0, 1] mapping with the planes swapped, byte for byte") {
    // The regression guard for T0144.3: simplifying the clip-space machinery
    // must not move the matrix. The expected value is constructed through
    // Diligent's own helper with the exact arguments the engine is meant to
    // pass -- reversed planes, [0, 1] clip space -- so a drift in the
    // implementation fails exactly rather than approximately, and a
    // simplification that produces the same construction provably changes
    // nothing.
    hp::Camera camera;
    camera.verticalFov = 1.0472F;
    camera.nearPlane = 0.25F;
    camera.farPlane = 500.0F;
    const float aspect = 16.0F / 9.0F;

    const hp::float4x4 expected = hp::float4x4::Projection(
        camera.verticalFov, aspect, camera.farPlane, camera.nearPlane,
        /*NegativeOneToOne = */ false);
    CHECK(hp::projectionMatrix(camera, aspect, zeroToOne()) == expected);

    hp::Camera ortho;
    ortho.orthographic = true;
    ortho.orthographicSize = 5.0F;
    ortho.nearPlane = 0.1F;
    ortho.farPlane = 100.0F;
    const float height = ortho.orthographicSize * 2.0F;
    const hp::float4x4 expectedOrtho =
        hp::float4x4::Ortho(height, height, ortho.farPlane, ortho.nearPlane,
                            /*NegativeOneToOne = */ false);
    CHECK(hp::projectionMatrix(ortho, 1.0F, zeroToOne()) == expectedOrtho);
}

TEST_CASE("reverse-Z is what makes a float depth buffer worth having") {
    // The measurement behind the decision, rather than a restatement of it.
    //
    // Depth precision is how many distinct float values separate two nearby
    // world distances. Under a conventional mapping almost every representable
    // value is spent in the first fraction of the range; reverse-Z cancels that
    // against float's own crowding near zero.
    hp::Camera camera;
    camera.nearPlane = 0.1F;
    camera.farPlane = 1000.0F;

    const hp::float4x4 reverse = hp::projectionMatrix(camera, 1.0F, zeroToOne());

    // The same lens without the swap: near->0, far->1. This is the comparison
    // that makes the case a measurement rather than a restatement of the
    // decision -- asserting only that reverse-Z has some precision proves
    // nothing unless the alternative is shown to have less.
    const hp::float4x4 conventional = hp::float4x4::Projection(
        camera.verticalFov, 1.0F, camera.nearPlane, camera.farPlane, false);

    // At 900 metres -- far from the camera, where z-fighting actually happens --
    // one metre of world movement must still change the stored depth.
    const float reverseNear = ndcDepth(reverse, 900.0F);
    const float reverseFar = ndcDepth(reverse, 901.0F);
    const float plainNear = ndcDepth(conventional, 900.0F);
    const float plainFar = ndcDepth(conventional, 901.0F);

    // **Measured in ULPs, not in absolute difference, and the distinction is
    // the entire decision.** In exact arithmetic the two mappings are `1 - x`
    // of each other, so the absolute gap between 900m and 901m is necessarily
    // almost identical -- measuring that showed 1.23e-7 against 1.19e-7 and
    // proved nothing. What differs is *where on the number line* those values
    // sit. The conventional mapping puts them just below 1.0, where consecutive
    // floats are 6e-8 apart, so the metre is worth about two representable
    // steps. Reverse-Z puts them near 1e-5, where floats are vastly denser.
    const int reverseUlps = ulpsBetween(reverseNear, reverseFar);
    const int plainUlps = ulpsBetween(plainNear, plainFar);

    MESSAGE("900m..901m resolves to " << reverseUlps << " float steps under reverse-Z, and "
                                      << plainUlps << " under the conventional mapping");

    CHECK(plainUlps > 0);
    CHECK(reverseUlps > plainUlps);

    // Three orders of magnitude, not a marginal win. This is the number the
    // decision rests on, so it is asserted rather than described.
    CHECK(reverseUlps > plainUlps * 1000);
}

TEST_CASE("a matrix built for the wrong clip space is not the same matrix") {
    // Guards the thing that is invisible on one backend: if `ClipSpace` were
    // ignored, these two would be identical and OpenGL would silently render
    // with a Vulkan matrix. The engine refuses a [-1, 1] device, but the
    // parameter must still be honoured -- a parameter that is accepted and
    // dropped is worse than one that does not exist.
    hp::Camera camera;

    hp::ClipSpace negativeOneToOne;
    negativeOneToOne.minZ = -1.0F;
    negativeOneToOne.yToV = 0.5F;
    CHECK(negativeOneToOne.negativeOneToOneZ());
    CHECK_FALSE(zeroToOne().negativeOneToOneZ());

    const hp::float4x4 forZeroToOne = hp::projectionMatrix(camera, 1.0F, zeroToOne());
    const hp::float4x4 forNegative = hp::projectionMatrix(camera, 1.0F, negativeOneToOne);
    CHECK_FALSE(forZeroToOne == forNegative);

    // On a [-1, 1] clip space the near plane still maps to the near end of the
    // range, which is +1 in both conventions under reverse-Z; the far plane goes
    // to -1 rather than 0.
    CHECK(ndcDepth(forNegative, camera.nearPlane) == doctest::Approx(1.0F));
    CHECK(ndcDepth(forNegative, camera.farPlane) == doctest::Approx(-1.0F));
}

TEST_CASE("aspect ratio changes the horizontal extent and leaves the vertical alone") {
    // 130.2's decision, made observable: the vertical field of view is what is
    // authored, so widening the viewport must show more at the sides rather
    // than cropping the top.
    hp::Camera camera;
    camera.verticalFov = 1.0F;

    const hp::float4x4 narrow = hp::projectionMatrix(camera, 1.0F, zeroToOne());
    const hp::float4x4 wide = hp::projectionMatrix(camera, 2.0F, zeroToOne());

    CHECK(narrow._22 == doctest::Approx(wide._22));
    CHECK(wide._11 == doctest::Approx(narrow._11 / 2.0F));
}

TEST_CASE("an unusable lens yields the identity rather than a NaN") {
    // A NaN here would not fail here. It would multiply into a world transform
    // and present as geometry that stopped being drawn, in a subsystem with no
    // connection to cameras.
    const hp::ClipSpace clip = zeroToOne();
    const hp::float4x4 identity = hp::float4x4::Identity();

    hp::Camera zeroNear;
    zeroNear.nearPlane = 0.0F;
    CHECK(hp::projectionMatrix(zeroNear, 1.0F, clip) == identity);

    hp::Camera inverted;
    inverted.nearPlane = 100.0F;
    inverted.farPlane = 1.0F;
    CHECK(hp::projectionMatrix(inverted, 1.0F, clip) == identity);

    hp::Camera fine;
    CHECK(hp::projectionMatrix(fine, 0.0F, clip) == identity);
    CHECK(hp::projectionMatrix(fine, -1.0F, clip) == identity);

    hp::Camera degenerateFov;
    degenerateFov.verticalFov = kPi;
    CHECK(hp::projectionMatrix(degenerateFov, 1.0F, clip) == identity);

    hp::Camera flatOrtho;
    flatOrtho.orthographic = true;
    flatOrtho.orthographicSize = 0.0F;
    CHECK(hp::projectionMatrix(flatOrtho, 1.0F, clip) == identity);
}

TEST_CASE("focal length and field of view are exact inverses") {
    // 130.1: the conversion exists so millimetres can be an input, and a
    // conversion that does not round-trip would let a camera drift every time
    // an inspector displayed it in one unit and stored it in the other.
    const float sensor = hp::kDefaultSensorHeightMm;

    for (const float mm : {14.0F, 24.0F, 35.0F, 50.0F, 85.0F, 200.0F}) {
        const float fov = hp::verticalFovFromFocalLength(mm, sensor);
        CHECK(fov > 0.0F);
        CHECK(hp::focalLengthFromVerticalFov(fov, sensor) == doctest::Approx(mm));
    }

    // A 50mm lens on a full-frame sensor is the standard reference: about 27
    // degrees vertically. If the maths used sensor *width* this would come out
    // near 40, which is the mistake worth pinning.
    const float fifty = hp::verticalFovFromFocalLength(50.0F, sensor);
    CHECK(fifty * 180.0F / kPi == doctest::Approx(26.99F).epsilon(0.01));

    // Longer lens, narrower view. Trivially true and trivially easy to invert.
    CHECK(hp::verticalFovFromFocalLength(200.0F, sensor) <
          hp::verticalFovFromFocalLength(24.0F, sensor));
}

TEST_CASE("a bad conversion returns zero rather than a NaN or an infinity") {
    CHECK(hp::verticalFovFromFocalLength(0.0F, 24.0F) == 0.0F);
    CHECK(hp::verticalFovFromFocalLength(50.0F, 0.0F) == 0.0F);
    CHECK(hp::verticalFovFromFocalLength(-50.0F, 24.0F) == 0.0F);
    CHECK(hp::focalLengthFromVerticalFov(0.0F, 24.0F) == 0.0F);
    CHECK(hp::focalLengthFromVerticalFov(kPi, 24.0F) == 0.0F);
    CHECK(hp::focalLengthFromVerticalFov(1.0F, 0.0F) == 0.0F);
    CHECK(hp::horizontalFovFromVertical(1.0F, 0.0F) == 0.0F);
    CHECK(hp::horizontalFovFromVertical(0.0F, 1.0F) == 0.0F);
}

TEST_CASE("horizontal field of view follows the aspect ratio") {
    const float vertical = 1.0F;

    // Square viewport: the two agree.
    CHECK(hp::horizontalFovFromVertical(vertical, 1.0F) == doctest::Approx(vertical));

    // Wider viewport shows more horizontally, and it is *not* a linear scaling
    // of the angle -- a test that asserted 2x here would be asserting a bug.
    const float wide = hp::horizontalFovFromVertical(vertical, 16.0F / 9.0F);
    CHECK(wide > vertical);
    CHECK(wide < vertical * 16.0F / 9.0F);
}

TEST_CASE("exposure converts from EV100 and halves per stop") {
    // One stop of exposure is a factor of two. That relationship is the whole
    // reason EV is a useful unit, so it is what gets checked rather than a
    // magic constant.
    const float base = hp::exposureMultiplierFromEv100(13.0F);
    CHECK(base > 0.0F);
    CHECK(hp::exposureMultiplierFromEv100(14.0F) == doctest::Approx(base / 2.0F));
    CHECK(hp::exposureMultiplierFromEv100(12.0F) == doctest::Approx(base * 2.0F));

    // EV100 0 is the calibration point: 1 / 1.2.
    CHECK(hp::exposureMultiplierFromEv100(0.0F) == doctest::Approx(1.0F / 1.2F));
}

TEST_CASE("a default camera is a usable camera") {
    // Defaults matter more than they look: an entity gets one the moment a
    // Camera is added in the editor, and a default that produced an identity
    // matrix would present as "the viewport is broken".
    const hp::Camera camera;
    const hp::float4x4 projection = hp::projectionMatrix(camera, 16.0F / 9.0F, zeroToOne());

    CHECK_FALSE(projection == hp::float4x4::Identity());
    CHECK(ndcDepth(projection, camera.nearPlane) == doctest::Approx(1.0F));
    CHECK(ndcDepth(projection, camera.farPlane) == doctest::Approx(0.0F));
    CHECK(camera.verticalFov * 180.0F / kPi == doctest::Approx(60.0F).epsilon(0.001));
}
