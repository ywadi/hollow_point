#include <hp/Camera.hpp>

#include <hp/HandednessConvention.hpp>

#include <cmath>

namespace hp {
namespace {

constexpr float kPi = 3.14159265358979323846F;

/// Whether a lens can produce a projection at all.
///
/// Checked once, here, rather than trusted. Every one of these produces a
/// division by zero or a NaN, and a NaN in a projection matrix does not fail
/// where it is made -- it multiplies into world transforms and presents as
/// geometry that silently stopped being drawn.
bool usable(const Camera& camera, float aspect) {
    if (!(aspect > 0.0F)) {
        return false;
    }
    if (!(camera.nearPlane > 0.0F) || !(camera.farPlane > camera.nearPlane)) {
        return false;
    }
    if (camera.orthographic) {
        return camera.orthographicSize > 0.0F;
    }
    return camera.verticalFov > 0.0F && camera.verticalFov < kPi;
}

/// Turns Diligent's left-handed projection into a right-handed one.
///
/// **Exactly `diag(1, 1, -1, 1) * M`, which is a negation of view-space Z
/// applied before the projection** -- so in the engine's row-vector,
/// left-to-right convention (`hp/Math.hpp`) it is a negation of the matrix's
/// third *row*, and nothing else. `_31` and `_32` are zero in both the
/// perspective and the orthographic form, so the two elements below are the
/// whole of it.
///
/// **Measured before the conversion was swept, because "does a right-handed
/// projection compose with reverse-Z" is a decision and not an implementation
/// detail** (T0165.1). It composes exactly, and `camera_test.cpp` pins each
/// claim:
///
/// - `_11` and `_22` are untouched, so this is **not** a screen-space mirror:
///   world +X still lands on screen right and +Y on screen top. The mirror is
///   in which direction the camera *looks*, which is the point.
/// - `_43` is untouched and `_33`/`_34` flip together, so the near/far
///   endpoints land exactly where the left-handed form put them with the sign
///   of view-space Z flipped: **near -> 1, far -> 0**, reverse-Z intact.
/// - The orthographic form needs no special case: its `_34` is already zero,
///   so negating it is a no-op and the `_33` negation is all that happens.
/// - The 4x4 determinant changes sign. That *is* the mirror, it is the only
///   thing that changes, and `WindingConvention.hpp` counts it.
void makeRightHanded(float4x4& m) {
    m._33 = -m._33;
    m._34 = -m._34;
}

} // namespace

float verticalFovFromFocalLength(float focalLengthMm, float sensorHeightMm) {
    if (!(focalLengthMm > 0.0F) || !(sensorHeightMm > 0.0F)) {
        return 0.0F;
    }
    return 2.0F * std::atan(sensorHeightMm / (2.0F * focalLengthMm));
}

float focalLengthFromVerticalFov(float verticalFov, float sensorHeightMm) {
    if (!(verticalFov > 0.0F) || verticalFov >= kPi || !(sensorHeightMm > 0.0F)) {
        return 0.0F;
    }
    return sensorHeightMm / (2.0F * std::tan(verticalFov / 2.0F));
}

float horizontalFovFromVertical(float verticalFov, float aspect) {
    if (!(verticalFov > 0.0F) || verticalFov >= kPi || !(aspect > 0.0F)) {
        return 0.0F;
    }
    return 2.0F * std::atan(std::tan(verticalFov / 2.0F) * aspect);
}

float exposureMultiplierFromEv100(float ev100) {
    // The standard photometric relation: the exposure that maps a scene of the
    // given EV100 to 1.0. The 1.2 is the ISO 2720 reflected-light calibration
    // constant, which is why the number is not a round one.
    return 1.0F / (1.2F * std::pow(2.0F, ev100));
}

float4x4 projectionMatrix(const Camera& camera, float aspect) {
    if (!usable(camera, aspect)) {
        return float4x4::Identity();
    }

    // [0, 1] clip space, which Vulkan -- the only backend, D29 -- guarantees
    // by specification. `false` is Diligent's spelling of it
    // (`NegativeOneToOne`), and the fast suite pins the resulting matrix
    // byte-for-byte against Diligent's own helper so this cannot drift.
    constexpr bool kNegativeOneToOneZ = false;

    // Reverse-Z by swapping near and far, which is exact rather than a trick.
    // Diligent's SetNearFarClipPlanes solves for a mapping that sends its
    // `zNear` argument to clip 0 and its `zFar` argument to clip 1; handing it
    // the planes the other way round therefore sends the real near plane to 1
    // and the real far plane to 0. It holds for the orthographic form too,
    // which uses a different pair of expressions -- see the reverse-Z cases in
    // the fast tests, which assert the endpoints rather than the coefficients.
    const float clipNear = kReverseZ ? camera.farPlane : camera.nearPlane;
    const float clipFar = kReverseZ ? camera.nearPlane : camera.farPlane;

    // **Diligent's helpers are the left-handed forms, and building on them is
    // deliberate.** Their headers say so (`BasicMath.hpp:1835`, "Left-handed
    // projection"), so the engine takes their algebra -- including the
    // reverse-Z swap above, which is theirs -- and applies one mirror on top,
    // rather than hand-rolling a projection and owning every coefficient. The
    // fast suite still pins the result against their helper byte for byte,
    // now with `makeRightHanded` applied to the expectation, so an upstream
    // change to the algebra is still caught.
    float4x4 projection =
        camera.orthographic
            ? float4x4::Ortho(camera.orthographicSize * 2.0F * aspect,
                              camera.orthographicSize * 2.0F, clipNear, clipFar,
                              kNegativeOneToOneZ)
            : float4x4::Projection(camera.verticalFov, aspect, clipNear, clipFar,
                                   kNegativeOneToOneZ);

    if (kRightHandedCameraSpace) {
        makeRightHanded(projection);
    }
    return projection;
}

} // namespace hp
