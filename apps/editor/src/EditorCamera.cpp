// The seam described in EditorCamera.hpp. Read that first — every sign in this
// file is justified there, and each one was measured against Diligent's own
// `BasicMath.hpp` rather than reasoned about.
#include "EditorCamera.hpp"

#include <hp/HandednessConvention.hpp>

#include <algorithm>
#include <cmath>

namespace hped {
namespace {

/// How much one wheel notch scales the fly speed, and the orbit distance.
constexpr float kWheelScale = 1.15F;

constexpr float kMinMoveSpeed = 0.05F;
constexpr float kMaxMoveSpeed = 500.0F;
constexpr float kMinDistance = 0.01F;
constexpr float kMaxDistance = 10000.0F;

/// Pixels of middle-drag per world unit of pan, at a distance of one unit.
constexpr float kPanPerPixel = 0.0025F;

/// Spelled locally rather than taken from Diligent's `PI_F`, so the one
/// conversion in this file does not depend on a constant from the library whose
/// convention it is converting *away* from.
constexpr float kPi = 3.14159265358979323846F;

} // namespace

// --- EditorInputController ---------------------------------------------------

void EditorInputController::setKey(Diligent::InputKeys key, bool down) {
    Diligent::INPUT_KEY_STATE_FLAGS& state = m_Keys[static_cast<std::size_t>(key)];
    if (down) {
        state = static_cast<Diligent::INPUT_KEY_STATE_FLAGS>(
            state | Diligent::INPUT_KEY_STATE_FLAG_KEY_IS_DOWN);
    } else {
        state = static_cast<Diligent::INPUT_KEY_STATE_FLAGS>(
            (state & ~Diligent::INPUT_KEY_STATE_FLAG_KEY_IS_DOWN)
            | Diligent::INPUT_KEY_STATE_FLAG_KEY_WAS_DOWN);
    }
}

void EditorInputController::onEvent(const hp::Event& event) {
    switch (event.type()) {
    case hp::EventType::KeyPressed:
    case hp::EventType::KeyReleased: {
        const auto& key = static_cast<const hp::KeyEvent&>(event);
        const bool down = key.pressed();
        alt_ = key.modifiers().alt;
        switch (key.key()) {
        case hp::KeyCode::W:
            setKey(Diligent::InputKeys::MoveForward, down);
            break;
        case hp::KeyCode::S:
            setKey(Diligent::InputKeys::MoveBackward, down);
            break;
        // **A and D are crossed, and it is not a typo** — item 2 of the mirror
        // in the header. Diligent's `MoveRight` travels along its own
        // camera-right, which points at this engine's *left* (measured: the dot
        // product with hp's screen-right is -1 at every yaw).
        case hp::KeyCode::D:
            setKey(Diligent::InputKeys::MoveLeft, down);
            break;
        case hp::KeyCode::A:
            setKey(Diligent::InputKeys::MoveRight, down);
            break;
        case hp::KeyCode::E:
            setKey(Diligent::InputKeys::MoveUp, down);
            break;
        case hp::KeyCode::Q:
            setKey(Diligent::InputKeys::MoveDown, down);
            break;
        case hp::KeyCode::F:
            if (down && !key.isRepeat()) {
                frameRequested_ = true;
            }
            break;
        default:
            break;
        }
        setKey(Diligent::InputKeys::ShiftDown, key.modifiers().shift);
        setKey(Diligent::InputKeys::ControlDown, key.modifiers().control);
        setKey(Diligent::InputKeys::AltDown, key.modifiers().alt);
        break;
    }

    case hp::EventType::MouseButtonPressed:
    case hp::EventType::MouseButtonReleased: {
        const auto& button = static_cast<const hp::MouseButtonEvent&>(event);
        const bool down = button.pressed();
        switch (button.button()) {
        case hp::MouseButton::Left:
            leftDown_ = down;
            break;
        case hp::MouseButton::Right:
            rightDown_ = down;
            break;
        case hp::MouseButton::Middle:
            middleDown_ = down;
            break;
        default:
            break;
        }
        // Seeding the position on the press as well as on motion is what stops
        // the first frame of a drag differencing against a stale one and
        // snapping the view. (Windows discards these and re-reads the OS
        // cursor — see the note on `Camera::update`. Harmless: the two agree.)
        m_MouseState.PosX = button.x();
        m_MouseState.PosY = button.y();
        break;
    }

    case hp::EventType::MouseMoved: {
        const auto& moved = static_cast<const hp::MouseMovedEvent&>(event);
        m_MouseState.PosX = moved.x();
        m_MouseState.PosY = moved.y();
        if (middleDown_) {
            pan_.x += moved.deltaX();
            pan_.y += moved.deltaY();
        }
        break;
    }

    case hp::EventType::MouseScrolled: {
        const auto& scrolled = static_cast<const hp::MouseScrolledEvent&>(event);
        m_MouseState.WheelDelta += scrolled.offsetY();
        break;
    }

    case hp::EventType::WindowFocusLost:
        // A window that loses focus while a button is down never receives the
        // release, so the camera would keep turning behind someone's back — the
        // same failure `InputSystem::reset` exists for (T0110).
        leftDown_ = rightDown_ = middleDown_ = alt_ = false;
        for (auto& key : m_Keys) {
            key = Diligent::INPUT_KEY_STATE_FLAG_KEY_NONE;
        }
        break;

    default:
        break;
    }

    // Diligent's cameras rotate on `BUTTON_FLAG_LEFT` and nothing else, so the
    // seam decides which physical gesture that means. Right-drag to look and
    // Alt+left to orbit is what this class of editor does, and it leaves the
    // plain left button free for T0173's picking.
    looking_ = rightDown_ || (alt_ && leftDown_);
    m_MouseState.ButtonFlags = looking_ ? Diligent::MouseState::BUTTON_FLAG_LEFT
                                        : Diligent::MouseState::BUTTON_FLAG_NONE;
}

void EditorInputController::endFrame() {
    pan_ = hp::float2{0.0F, 0.0F};
    ClearState();
}

bool EditorInputController::takeFrameRequest() {
    const bool requested = frameRequested_;
    frameRequested_ = false;
    return requested;
}

// --- EditorCamera ------------------------------------------------------------

void EditorCamera::Camera::update(Diligent::InputController& controller, float deltaSeconds) {
    // **Item 3 of the mirror** (see the header). Upstream computes
    // `MouseDeltaX = now.PosX - m_LastMouseState.PosX`, so writing
    // `2 * now - prev` into the previous position makes that come out as
    // `-(now - prev)` — the negated horizontal delta, exactly, with no
    // dependence on where `now` came from. That last part is the point: on
    // Windows `now` is the OS cursor, because `InputControllerWin32` overrides
    // `GetMouseState()` to re-read it, and any scheme that works by supplying a
    // doctored position is discarded there without a word.
    //
    // `GetMouseState()` is called here rather than reading a cached value for
    // the same reason: on Windows it is what refreshes the position, so this
    // makes `now` the value `Update` is about to see.
    const Diligent::MouseState& now = controller.GetMouseState();
    if (m_LastMouseState.PosX >= 0.0F) {
        // Can go negative when the cursor is near the left edge and moving
        // right quickly, which upstream reads as "no valid previous position"
        // and skips. One frame without rotation, at x < deltaX. Not worth
        // clamping: a clamp would silently halve that frame's turn instead.
        m_LastMouseState.PosX = 2.0F * now.PosX - m_LastMouseState.PosX;
    }
    FirstPersonCamera::Update(controller, deltaSeconds);
}

EditorCamera::EditorCamera() {
    // The reference axes are left at Diligent's default (+X right, +Y up,
    // +Z ahead), which makes `GetReferenceRotiation()` the identity —
    // determinant **+1**, no mirror. Calling `SetReferenceAxes(..., false)` to
    // "make it right-handed" would build a reference matrix with a negated Z
    // column, determinant **−1**, which is exactly the mirror T0165 forbids.
    // The handedness difference is resolved in `transform()`, not here.
    camera_.SetMoveSpeed(moveSpeed_);
    camera_.SetSpeedUpScales(4.0F, 16.0F);
    camera_.SetRotationSpeed(0.004F);
    camera_.SetPos(hp::float3{0.0F, 1.0F, 5.0F});
    // Looking down world −Z, which is where an hp scene puts things in front of
    // a default camera (T0165, and `populateDemoScene`'s quad at z = −4).
    // Diligent's yaw 0 looks down **+Z**, so the default is pi and not zero —
    // getting this wrong opens the editor facing the empty half of the world,
    // which reads as "the renderer drew nothing".
    camera_.SetRotation(kPi, 0.0F);
}

void EditorCamera::lookAlong(const hp::float3& direction) {
    const float lengthSq = Diligent::dot(direction, direction);
    if (lengthSq < 1e-12F) {
        return;
    }
    const hp::float3 dir = Diligent::normalize(direction);

    // **Not `FirstPersonCamera::SetLookAt`, and that is a measured refusal.**
    // Upstream's version computes `yaw = atan2(V.x, V.z)`, which does not invert
    // its own `Update` — feeding it the direction the camera is already looking
    // returns a yaw that points somewhere else for every angle except zero and
    // pi (checked over a 13x9 grid; it reproduces the input direction at two
    // points and misses everywhere else). The signs below do round-trip
    // exactly, over the same grid.
    //
    // hp's forward equals Diligent's ahead by construction (item 1 in the
    // header), so this takes an hp-space direction with no conversion.
    const float yaw = std::atan2(-dir.x, dir.z);
    const float pitch = std::atan2(dir.y, std::hypot(dir.x, dir.z));
    camera_.SetRotation(yaw, pitch);
}

void EditorCamera::update(EditorInputController& input, float deltaSeconds) {
    const float wheel = input.wheelDelta();
    const bool orbiting = input.orbiting();

    if (orbiting) {
        // Zoom moves the camera along the boom rather than changing the lens —
        // a lens change is a different picture, and an editor that "zooms" by
        // narrowing the field of view lies about what the game will look like.
        distance_ = std::clamp(distance_ * std::pow(kWheelScale, -wheel), kMinDistance,
                               kMaxDistance);
    } else {
        moveSpeed_ = std::clamp(moveSpeed_ * std::pow(kWheelScale, wheel), kMinMoveSpeed,
                                kMaxMoveSpeed);
        camera_.SetMoveSpeed(moveSpeed_);
    }

    const hp::float3 before = camera_.GetPos();

    // Upstream does the work: keyboard composition and normalisation, the
    // speed-up scales, the mouse delta, the pitch clamp and the position
    // integration. Nothing here re-implements any of it; `Camera::update` only
    // negates the horizontal delta on the way in.
    camera_.update(input, deltaSeconds);

    // The basis after the rotation this frame applied.
    const hp::float3 fwd = forward();
    const hp::Quaternion rotation = transform().rotation;
    const hp::float4x4 basis = rotation.ToMatrix();
    const hp::float3 right{basis._11, basis._12, basis._13};
    const hp::float3 up{basis._21, basis._22, basis._23};

    // Middle-drag pans, in the plane the viewport shows, scaled by how far away
    // the subject is — so a pan covers the same fraction of the screen whether
    // you are next to the model or across the level from it.
    hp::float3 pan{0.0F, 0.0F, 0.0F};
    const hp::float2 panPixels = input.panDelta();
    if (panPixels.x != 0.0F || panPixels.y != 0.0F) {
        const float scale = kPanPerPixel * std::max(distance_, kMinDistance);
        pan = right * (-panPixels.x * scale) + up * (panPixels.y * scale);
    }

    if (orbiting) {
        // Whatever the keyboard moved the camera by moves the *pivot* instead,
        // so WASD frames a different part of the model rather than breaking the
        // orbit. Then the camera is put back on the boom.
        pivot_ += (camera_.GetPos() - before) + pan;
        camera_.SetPos(pivot_ - fwd * distance_);
    } else {
        camera_.SetPos(camera_.GetPos() + pan);
        // Keep the pivot in front of the camera, so switching to orbit turns
        // around what is being looked at rather than around wherever the pivot
        // was last left.
        pivot_ = camera_.GetPos() + fwd * distance_;
    }
}

void EditorCamera::frame(const hp::float3& centre, float radius,
                         float verticalFovRadians) {
    const float safeRadius = std::max(radius, 1e-3F);
    const float halfFov = std::max(verticalFovRadians * 0.5F, 1e-3F);

    // Fit the bounding sphere in the vertical field of view, with a margin so
    // the subject is not flush against the frame edge.
    pivot_ = centre;
    distance_ = std::clamp(safeRadius / std::sin(halfFov) * 1.3F, kMinDistance, kMaxDistance);

    // A three-quarter view looking slightly down, which is what an asset
    // inspector opens on everywhere — a head-on view makes a curved surface and
    // a flat one look the same.
    //
    // Yaw and pitch here are Diligent's, so they are converted by `transform()`
    // like everything else. `pi` is "looking down world −Z"; +0.6 swings it
    // round, and a **negative** pitch looks down (positive looks up).
    camera_.SetRotation(kPi + 0.6F, -0.30F);
    camera_.SetPos(pivot_ - forward() * distance_);

    // Fly speed scaled to the subject, so opening a 2 cm bolt and a 200 m level
    // both move at a usable rate.
    moveSpeed_ = std::clamp(safeRadius * 1.5F, kMinMoveSpeed, kMaxMoveSpeed);
    camera_.SetMoveSpeed(moveSpeed_);
}

hp::Transform EditorCamera::transform() const {
    static_assert(hp::kRightHandedCameraSpace,
                  "The conversion below turns Diligent's left-handed camera space into hp's "
                  "right-handed one (T0165). If that constant ever goes false, this seam is "
                  "wrong in a way that renders as mirrored geometry rather than as an error.");

    hp::Transform out;
    out.position = camera_.GetPos();

    // Item 1 of the mirror. Measured, not derived by eye: with
    // hp_yaw = pi - yaw and hp_pitch = pitch, hp's forward — the negated third
    // row of this quaternion's matrix — equals Diligent's `GetWorldAhead()`
    // exactly, at every yaw and pitch, and the matrix determinant stays +1.
    //
    // Diligent multiplies quaternions so that `a * b` has the matrix of
    // `mat(b) * mat(a)`; this ordering is the one that reproduces
    // `RotationY(yaw) * RotationX(pitch)` and it is what the sun in
    // `populateDemoScene` uses too.
    const float hpYaw = kPi - camera_.yaw();
    const float hpPitch = camera_.pitch();
    out.rotation = hp::Quaternion::RotationFromAxisAngle(hp::float3{0.0F, 1.0F, 0.0F}, hpYaw)
                   * hp::Quaternion::RotationFromAxisAngle(hp::float3{1.0F, 0.0F, 0.0F}, hpPitch);
    return out;
}

hp::float3 EditorCamera::forward() const {
    const hp::float4x4 basis = transform().rotation.ToMatrix();
    // The camera looks down its own **−Z** (T0165), so forward is the negated
    // third row of a row-vector rotation matrix.
    return hp::float3{-basis._31, -basis._32, -basis._33};
}

hp::float3 EditorCamera::position() const {
    return camera_.GetPos();
}

bool isProperRotation(const hp::Quaternion& rotation) {
    const hp::float4x4 m = rotation.ToMatrix();
    const float determinant = m._11 * (m._22 * m._33 - m._23 * m._32)
                              - m._12 * (m._21 * m._33 - m._23 * m._31)
                              + m._13 * (m._21 * m._32 - m._22 * m._31);
    return determinant > 0.0F;
}

} // namespace hped
