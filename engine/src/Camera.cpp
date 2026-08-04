#include <hp/Camera.hpp>

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

float4x4 projectionMatrix(const Camera& camera, float aspect, ClipSpace clip) {
    if (!usable(camera, aspect)) {
        return float4x4::Identity();
    }

    const bool negativeOneToOneZ = clip.negativeOneToOneZ();

    // Reverse-Z by swapping near and far, which is exact rather than a trick.
    // Diligent's SetNearFarClipPlanes solves for a mapping that sends its
    // `zNear` argument to clip 0 and its `zFar` argument to clip 1; handing it
    // the planes the other way round therefore sends the real near plane to 1
    // and the real far plane to 0. It holds for the orthographic form too,
    // which uses a different pair of expressions -- see the reverse-Z cases in
    // the fast tests, which assert the endpoints rather than the coefficients.
    const float clipNear = kReverseZ ? camera.farPlane : camera.nearPlane;
    const float clipFar = kReverseZ ? camera.nearPlane : camera.farPlane;

    if (camera.orthographic) {
        const float height = camera.orthographicSize * 2.0F;
        return float4x4::Ortho(height * aspect, height, clipNear, clipFar, negativeOneToOneZ);
    }

    return float4x4::Projection(camera.verticalFov, aspect, clipNear, clipFar, negativeOneToOneZ);
}

} // namespace hp
