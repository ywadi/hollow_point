// The camera lens model and its projection (T0130).
//
// Bucket: fast. All of this is arithmetic — no device is needed to check that a
// projection maps the near plane to 1. The projection is built for the [0, 1]
// clip space unconditionally since T0144.3 — Vulkan, the only backend,
// guarantees it by specification — and a case below pins the construction
// byte-for-byte against Diligent's own helper.
//
// **These cases assert endpoints, not coefficients.** Checking `_33` against a
// hand-derived expression only proves the test and the implementation share a
// derivation; checking that a point on the near plane comes out at depth 1
// proves the thing that actually matters, and would have caught a reverse-Z
// convention applied to the perspective path and forgotten on the orthographic
// one.
//
// **The camera is right-handed since T0165** (`hp::kRightHandedCameraSpace`),
// so a point in front of the lens has *negative* view-space Z. Every case here
// goes through `viewZFor`, which converts a distance into a view-space Z, so
// the cases read in distances and the convention lives in one place. The first
// case below is the gate that proved a right-handed projection composes with
// reverse-Z at all, and it is deliberately the only one that touches
// coefficients.

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

/// The view-space Z of a point a given distance in front of the camera.
///
/// Negative under the engine's right-handed convention, positive under a
/// left-handed one. Written against the constant rather than as a literal
/// negation so the cases below state distances -- which is what a lens
/// parameter is -- and flipping the convention moves one expression.
constexpr float viewZFor(float distanceInFront) {
    return hp::kRightHandedCameraSpace ? -distanceInFront : distanceInFront;
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

} // namespace

TEST_CASE("the engine is on reverse-Z, and the clear value follows it") {
    // If this ever flips, every case below that asserts near->1 flips with it,
    // which is the point of asserting against the constant rather than a
    // literal.
    CHECK(hp::kReverseZ);
    CHECK(hp::kDepthClearValue == 0.0F);
}

TEST_CASE("the camera is right-handed, and right-handed composes with reverse-Z") {
    // **T0165.1: the gate.** Diligent's `Matrix4x4::Projection` is commented
    // "Left-handed projection" and every rotation helper in `BasicMath.hpp` is
    // "D3D-style left-handed", so a right-handed engine is built against their
    // grain. Whether that grain can carry reverse-Z was a decision, not an
    // implementation detail: if it could not compose, the answer was to stop
    // and say so rather than work around it. It composes exactly, and this case
    // is what says so -- deliberately the one place in this file that touches
    // coefficients, because the claim *is* about the matrix's structure.
    CHECK(hp::kRightHandedCameraSpace);

    hp::Camera camera;
    camera.verticalFov = 1.0472F;
    camera.nearPlane = 0.1F;
    camera.farPlane = 100.0F;
    const float aspect = 16.0F / 9.0F;
    const hp::float4x4 rh = hp::projectionMatrix(camera, aspect);
    const hp::float4x4 lh = hp::float4x4::Projection(camera.verticalFov, aspect,
                                                     camera.farPlane, camera.nearPlane, false);

    SUBCASE("it differs from the left-handed form in the third row and nowhere else") {
        // A right-handed projection is `diag(1, 1, -1, 1) * M` -- a negation of
        // view-space Z before the projection, which in this engine's row-vector
        // convention is a negation of the matrix's third row. `_31` and `_32`
        // are zero in both forms, so two elements are the whole difference.
        CHECK(rh._33 == -lh._33);
        CHECK(rh._34 == -lh._34);
        CHECK(rh._11 == lh._11);
        CHECK(rh._22 == lh._22);
        CHECK(rh._43 == lh._43);
        CHECK(rh._44 == lh._44);
    }

    SUBCASE("it is not a screen-space mirror -- world +X still lands on screen right") {
        // The distinction that makes the whole change safe to describe as "the
        // camera looks the other way" rather than "the image flips". `_11` and
        // `_22` are untouched above; this measures the consequence through the
        // perspective divide, which is what the rasteriser sees.
        const hp::float4 clip = hp::float4(1.0F, 2.0F, viewZFor(5.0F), 1.0F) * rh;
        REQUIRE(clip.w > 0.0F);
        CHECK(clip.x / clip.w > 0.0F);
        CHECK(clip.y / clip.w > 0.0F);
    }

    SUBCASE("w is positive in front of the camera and negative behind it") {
        // `CameraSystem::worldToScreen` refuses `w <= 0` to keep a point behind
        // the player off the screen. That test is only a test if `w` actually
        // carries the sign, which under a right-handed projection means
        // `_34 = -1` rather than `+1`.
        CHECK(rh._34 == -1.0F);
        const hp::float4 front = hp::float4(0.0F, 0.0F, viewZFor(5.0F), 1.0F) * rh;
        const hp::float4 behind = hp::float4(0.0F, 0.0F, -viewZFor(5.0F), 1.0F) * rh;
        CHECK(front.w > 0.0F);
        CHECK(behind.w < 0.0F);
    }

    SUBCASE("reverse-Z survives the mirror, perspective and orthographic alike") {
        // The gate's actual question. `SetNearFarClipPlanes` is handed the
        // planes the other way round -- Diligent's own comment says that is how
        // it is told the buffer is reversed -- and the mirror is applied on top.
        // The endpoints are what prove the two do not interfere.
        CHECK(ndcDepth(rh, viewZFor(camera.nearPlane)) == doctest::Approx(1.0F));
        CHECK(ndcDepth(rh, viewZFor(camera.farPlane)) == doctest::Approx(0.0F));

        hp::Camera ortho;
        ortho.orthographic = true;
        ortho.orthographicSize = 5.0F;
        ortho.nearPlane = 0.1F;
        ortho.farPlane = 100.0F;
        const hp::float4x4 orthoProj = hp::projectionMatrix(ortho, 1.0F);
        // The orthographic form needs no special case: its `_34` is already
        // zero, so the mirror is entirely in `_33`. Checked because "it needs no
        // special case" is exactly the kind of claim that is true until it is
        // not.
        CHECK(orthoProj._34 == 0.0F);
        CHECK(ndcDepth(orthoProj, viewZFor(ortho.nearPlane)) == doctest::Approx(1.0F));
        CHECK(ndcDepth(orthoProj, viewZFor(ortho.farPlane)) == doctest::Approx(0.0F));
    }

    SUBCASE("the handedness change is exactly one mirror") {
        // The determinant is the honest statement of "one mirror", and it is
        // what `WindingConvention.hpp`'s mirror count is counting. Facing never
        // consults the 4x4 determinant -- T0152.1 is emphatic about that -- but
        // the *chain's* chirality is what the winding flag has to agree with,
        // and this is where that chirality is introduced.
        CHECK(rh.Determinant() * lh.Determinant() < 0.0F);
    }
}

TEST_CASE("a perspective projection puts the near plane at 1 and the far plane at 0") {
    hp::Camera camera;
    camera.nearPlane = 0.1F;
    camera.farPlane = 1000.0F;

    const hp::float4x4 projection = hp::projectionMatrix(camera, 16.0F / 9.0F);

    // Reverse-Z. Getting this backwards renders a scene in which distant
    // geometry occludes near geometry -- which does not look like a depth bug,
    // it looks like the model is inside out.
    CHECK(ndcDepth(projection, viewZFor(camera.nearPlane)) == doctest::Approx(1.0F));
    CHECK(ndcDepth(projection, viewZFor(camera.farPlane)) == doctest::Approx(0.0F));

    // Monotonic in between, or depth testing is meaningless regardless of which
    // end is which.
    float previous = 1.0F;
    for (float z = 1.0F; z < 1000.0F; z *= 2.0F) {
        const float depth = ndcDepth(projection, viewZFor(z));
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

    const hp::float4x4 projection = hp::projectionMatrix(camera, 1.0F);

    CHECK(ndcDepth(projection, viewZFor(camera.nearPlane)) == doctest::Approx(1.0F));
    CHECK(ndcDepth(projection, viewZFor(camera.farPlane)) == doctest::Approx(0.0F));
    CHECK(ndcDepth(projection, viewZFor(50.05F)) == doctest::Approx(0.5F));
}

TEST_CASE("the projection is Diligent's [0, 1] mapping, planes swapped, third row negated") {
    // The regression guard for T0144.3, extended by T0165: simplifying the
    // clip-space machinery must not move the matrix, and neither must the
    // handedness change. The expected value is constructed through Diligent's
    // own helper with the exact arguments the engine is meant to pass --
    // reversed planes, [0, 1] clip space -- and then mirrored, so a drift in
    // the implementation fails exactly rather than approximately.
    //
    // **The mirror is applied to the expectation, not asserted away.** Building
    // the expectation as `Projection(...)` alone would silently pass a
    // left-handed engine; building it as a hand-written matrix would only prove
    // the test and the implementation share a derivation, which is the failure
    // mode the file header rejects.
    auto mirrored = [](hp::float4x4 m) {
        m._33 = -m._33;
        m._34 = -m._34;
        return m;
    };

    hp::Camera camera;
    camera.verticalFov = 1.0472F;
    camera.nearPlane = 0.25F;
    camera.farPlane = 500.0F;
    const float aspect = 16.0F / 9.0F;

    const hp::float4x4 lh = hp::float4x4::Projection(
        camera.verticalFov, aspect, camera.farPlane, camera.nearPlane,
        /*NegativeOneToOne = */ false);
    const hp::float4x4 expected = hp::kRightHandedCameraSpace ? mirrored(lh) : lh;
    CHECK(hp::projectionMatrix(camera, aspect) == expected);

    hp::Camera ortho;
    ortho.orthographic = true;
    ortho.orthographicSize = 5.0F;
    ortho.nearPlane = 0.1F;
    ortho.farPlane = 100.0F;
    const float height = ortho.orthographicSize * 2.0F;
    const hp::float4x4 lhOrtho =
        hp::float4x4::Ortho(height, height, ortho.farPlane, ortho.nearPlane,
                            /*NegativeOneToOne = */ false);
    const hp::float4x4 expectedOrtho =
        hp::kRightHandedCameraSpace ? mirrored(lhOrtho) : lhOrtho;
    CHECK(hp::projectionMatrix(ortho, 1.0F) == expectedOrtho);
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

    const hp::float4x4 reverse = hp::projectionMatrix(camera, 1.0F);

    // The same lens without the swap: near->0, far->1. This is the comparison
    // that makes the case a measurement rather than a restatement of the
    // decision -- asserting only that reverse-Z has some precision proves
    // nothing unless the alternative is shown to have less.
    const hp::float4x4 conventional = hp::float4x4::Projection(
        camera.verticalFov, 1.0F, camera.nearPlane, camera.farPlane, false);

    // At 900 metres -- far from the camera, where z-fighting actually happens --
    // one metre of world movement must still change the stored depth.
    // The engine's matrix is right-handed, so it is fed a negative view Z; the
    // `conventional` control is Diligent's raw left-handed helper and is fed a
    // positive one. Both describe the same point 900 m in front of the lens --
    // the comparison is between depth *mappings*, not between handednesses.
    const float reverseNear = ndcDepth(reverse, viewZFor(900.0F));
    const float reverseFar = ndcDepth(reverse, viewZFor(901.0F));
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

TEST_CASE("aspect ratio changes the horizontal extent and leaves the vertical alone") {
    // 130.2's decision, made observable: the vertical field of view is what is
    // authored, so widening the viewport must show more at the sides rather
    // than cropping the top.
    hp::Camera camera;
    camera.verticalFov = 1.0F;

    const hp::float4x4 narrow = hp::projectionMatrix(camera, 1.0F);
    const hp::float4x4 wide = hp::projectionMatrix(camera, 2.0F);

    CHECK(narrow._22 == doctest::Approx(wide._22));
    CHECK(wide._11 == doctest::Approx(narrow._11 / 2.0F));
}

TEST_CASE("an unusable lens yields the identity rather than a NaN") {
    // A NaN here would not fail here. It would multiply into a world transform
    // and present as geometry that stopped being drawn, in a subsystem with no
    // connection to cameras.
    const hp::float4x4 identity = hp::float4x4::Identity();

    hp::Camera zeroNear;
    zeroNear.nearPlane = 0.0F;
    CHECK(hp::projectionMatrix(zeroNear, 1.0F) == identity);

    hp::Camera inverted;
    inverted.nearPlane = 100.0F;
    inverted.farPlane = 1.0F;
    CHECK(hp::projectionMatrix(inverted, 1.0F) == identity);

    hp::Camera fine;
    CHECK(hp::projectionMatrix(fine, 0.0F) == identity);
    CHECK(hp::projectionMatrix(fine, -1.0F) == identity);

    hp::Camera degenerateFov;
    degenerateFov.verticalFov = kPi;
    CHECK(hp::projectionMatrix(degenerateFov, 1.0F) == identity);

    hp::Camera flatOrtho;
    flatOrtho.orthographic = true;
    flatOrtho.orthographicSize = 0.0F;
    CHECK(hp::projectionMatrix(flatOrtho, 1.0F) == identity);
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
    const hp::float4x4 projection = hp::projectionMatrix(camera, 16.0F / 9.0F);

    CHECK_FALSE(projection == hp::float4x4::Identity());
    CHECK(ndcDepth(projection, viewZFor(camera.nearPlane)) == doctest::Approx(1.0F));
    CHECK(ndcDepth(projection, viewZFor(camera.farPlane)) == doctest::Approx(0.0F));
    CHECK(camera.verticalFov * 180.0F / kPi == doctest::Approx(60.0F).epsilon(0.001));
}
