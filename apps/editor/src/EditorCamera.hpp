// The editor's viewport camera (T0172).
//
// **This file writes no camera.** D40 is binding: DiligentEngine ships
// `FirstPersonCamera` and `TrackballCamera` in `DiligentSamples/SampleBase/`,
// and what this engine adds is the *seam* — hp's SDL3-shaped events on one
// side, Diligent's `InputControllerBase` state on the other, and a conversion
// from Diligent's camera state into an `hp::Transform` that the scene's camera
// entity carries. Rotation maths, mouse-delta handling, pitch clamping and the
// position integration are all upstream's and are called, not reimplemented.
//
// ---
//
// **Why `FirstPersonCamera` drives both modes, including orbit.**
//
// `TrackballCamera` is the closer fit for orbit and it is what the owner's own
// reference uses. It was **not** used, and the reason is measurable rather than
// aesthetic: it exposes its result only as a composed quaternion built in
// Diligent's **left-handed** camera convention (`m_PrimaryRotation`, computed
// inside `Update` and unreachable except through `GetRotation()`), so consuming
// it means converting a *second* convention with a second set of signs. This
// engine is right-handed (T0165) and every sign at this boundary is a mirror
// waiting to happen — the trap `WindingConvention.hpp` and
// `HandednessConvention.hpp` both exist to describe. `FirstPersonCamera` hands
// out yaw, pitch and position as **scalars**, which converts exactly once and is
// the only conversion in this file.
//
// Orbit is then what orbit is: the same yaw and pitch, with the camera placed at
// `pivot - forward * distance` instead of wherever the fly integration left it.
// Nothing about the rotation differs between the two modes, which is precisely
// why one camera can serve both.
//
// ---
//
// **The mirror, and where it lands** (T0165). Diligent's camera space is
// left-handed: its camera looks down **+Z** and its "right" axis points at what
// this engine calls *left*. hp is right-handed and looks down **−Z**;
// `hp::projectionMatrix` absorbs the mirror for rendering, by negating the third
// row of Diligent's left-handed form.
//
// At *this* seam the same mirror surfaces three times, and all three are handled
// here rather than being spread across call sites. Each was measured against
// Diligent's own `BasicMath.hpp` matrices before it was written down:
//
//  1. `hp_yaw = pi - diligent_yaw`, `hp_pitch = diligent_pitch` makes hp's
//     forward (`-Z` of the entity's world rotation) equal Diligent's
//     `GetWorldAhead()` **exactly**, for every yaw and pitch — so "W" moves
//     toward what the viewport shows. The resulting basis has determinant
//     **+1**: this is a rotation, not a reflection, and `isProperRotation` pins
//     that at run time, because a reflection here renders as backwards geometry
//     rather than as an error and nobody can see it by looking.
//  2. Diligent's `MoveRight` travels along its own camera-right, whose dot
//     product with hp's screen-right is **−1** at every yaw. So `D` binds to
//     `InputKeys::MoveLeft` and `A` to `MoveRight`. Reading that as a typo is
//     the trap; it is the mirror.
//  3. The horizontal mouse delta would otherwise turn the view the wrong way,
//     so it is negated. It is negated rather than the mapping in (1) being
//     flipped, because (1) is what keeps "forward" honest and (2) follows from
//     it. `SetHandness` is not the answer: it flips yaw *and* pitch together, so
//     it fixes one and breaks the other; the same is true of a negative
//     `SetRotationSpeed`.
//
// **(3) is done by pre-adjusting `m_LastMouseState`, and that is not the obvious
// way round.** The obvious way is to report a mirrored cursor position, and it
// is wrong on Windows: `InputControllerWin32::GetMouseState()` *hides* the base
// method and re-reads the real cursor with `GetCursorPos`/`ScreenToClient`, so
// whatever position this seam supplies is discarded before either camera sees
// it, and the mirror silently stops applying — on one target only, which is the
// worst possible shape for a bug like this. Since `Update` computes its delta as
// `now - m_LastMouseState`, setting that previous position to `2 * now - prev`
// yields exactly `-(now - prev)` whatever the source of `now`. Same code, same
// result, both targets.
#pragma once

#include <hp/Event.hpp>
#include <hp/Math.hpp>
#include <hp/Scene.hpp>

#include "FirstPersonCamera.hpp"
#include "InputController.hpp"

namespace hped {

/// hp's events, in the shape Diligent's cameras read.
///
/// Derives from `Diligent::InputController` — the platform typedef — rather than
/// from `InputControllerBase`, because that is the parameter type
/// `FirstPersonCamera::Update` takes. The platform member functions
/// (`HandleXEvent`, `HandleNativeMessage`) are **never called**: this engine's
/// window is SDL3's (T0015) and the platform controller's own event source is
/// X11 or the Win32 message loop. Only the protected state it inherits is
/// filled, which is all either camera reads.
class EditorInputController final : public Diligent::InputController {
public:
    /// Feeds one hp event in. Call from `ILayer::onEvent`.
    ///
    /// Does **not** consume: the editor camera is the bottom of the stack today,
    /// and a panel framework (T0032) will sit above it and consume first.
    /// @param event the event to read.
    /// @returns nothing.
    void onEvent(const hp::Event& event);

    /// Ends the frame: clears the wheel delta and the was-down edges.
    ///
    /// Separate from `Update` because the wheel is an *accumulator* — several
    /// scroll events can arrive in one frame — and clearing it inside the camera
    /// would lose the ones that arrived after it ran.
    /// @returns nothing.
    void endFrame();

    /// @returns pan movement in pixels accumulated this frame, non-zero only
    ///          while the middle button is held.
    [[nodiscard]] hp::float2 panDelta() const { return pan_; }

    /// @returns the wheel delta accumulated this frame, in notches.
    [[nodiscard]] float wheelDelta() const { return m_MouseState.WheelDelta; }

    /// @returns whether the user is dragging to look or orbit — right button, or
    ///          Alt with the left button, which is what every editor in this
    ///          class does and leaves the plain left button free for T0173's
    ///          picking.
    [[nodiscard]] bool looking() const { return looking_; }

    /// @returns whether Alt is held, which selects orbit over fly.
    [[nodiscard]] bool orbiting() const { return alt_ && leftDown_; }

    /// @returns whether the user asked to re-frame the subject (the F key), and
    ///          clears the request.
    [[nodiscard]] bool takeFrameRequest();

private:
    void setKey(Diligent::InputKeys key, bool down);

    hp::float2 pan_{0.0F, 0.0F};
    bool alt_ = false;
    bool leftDown_ = false;
    bool rightDown_ = false;
    bool middleDown_ = false;
    bool looking_ = false;
    bool frameRequested_ = false;
};

/// The editor's view of the scene: fly with the right button, orbit with Alt.
///
/// Holds no GPU resources and knows nothing about the renderer — which is the
/// property that made T0172 safe to do while T0170 rewrites the draw path.
class EditorCamera {
public:
    EditorCamera();

    /// Advances the camera by one frame.
    /// @param input this frame's input, already fed from the event stream.
    /// @param deltaSeconds the frame delta, for speed-independent movement.
    /// @returns nothing.
    void update(EditorInputController& input, float deltaSeconds);

    /// Frames a sphere: puts the subject in view at a sensible distance and
    /// makes it the orbit pivot.
    /// @param centre the subject's centre, in world space.
    /// @param radius the subject's bounding radius. Clamped to something
    ///        positive, so a degenerate model still produces a usable view.
    /// @param verticalFovRadians the lens the view will be rendered with.
    /// @returns nothing.
    void frame(const hp::float3& centre, float radius, float verticalFovRadians);

    /// @returns the transform to write onto the scene's camera entity.
    ///
    /// **This is the one conversion in the file** — see the header comment for
    /// the derivation and the three places the mirror surfaces.
    [[nodiscard]] hp::Transform transform() const;

    /// Aims the camera along a world-space direction, leaving it where it is.
    ///
    /// Used to adopt whatever camera the scene already had, so opening the
    /// editor on a gameplay module's scene does not snap the view somewhere
    /// else on the first frame.
    /// @param direction the direction to look along. A zero-length direction is
    ///        ignored rather than producing a NaN orientation.
    /// @returns nothing.
    void lookAlong(const hp::float3& direction);

    /// Places the camera without changing where it is aimed.
    /// @param position the world-space position.
    /// @returns nothing.
    void setPosition(const hp::float3& position) { camera_.SetPos(position); }

    /// @returns the camera's world-space forward, which is its own **−Z**
    ///          (T0165).
    [[nodiscard]] hp::float3 forward() const;

    /// @returns the camera's world position.
    [[nodiscard]] hp::float3 position() const;

    /// @returns metres per second at the current fly speed.
    [[nodiscard]] float moveSpeed() const { return moveSpeed_; }

    /// @returns the distance from the orbit pivot, which is what a framing
    ///          caller needs to size the clip planes against.
    [[nodiscard]] float distance() const { return distance_; }

private:
    /// Reaches three protected members `FirstPersonCamera` offers no accessor
    /// for: `m_fYawAngle` and `m_fPitchAngle`, which the conversion needs, and
    /// `m_LastMouseState`, which is where item 3 of the mirror is applied. Adds
    /// no camera logic — every line of that is still upstream's.
    class Camera final : public Diligent::FirstPersonCamera {
    public:
        [[nodiscard]] float yaw() const { return m_fYawAngle; }
        [[nodiscard]] float pitch() const { return m_fPitchAngle; }

        /// Runs upstream's update with the horizontal mouse delta negated.
        /// @param controller this frame's input.
        /// @param deltaSeconds the frame delta.
        /// @returns nothing.
        void update(Diligent::InputController& controller, float deltaSeconds);
    };

    Camera camera_;

    /// What orbit turns around, and what a re-frame recentres on.
    hp::float3 pivot_{0.0F, 0.0F, 0.0F};

    /// Distance from `pivot_`, held across a mode switch so orbiting after
    /// flying does not jump.
    float distance_ = 5.0F;

    /// Metres per second, scaled by the wheel while flying.
    float moveSpeed_ = 3.0F;
};

/// Asserts that a rotation matrix is a rotation and not a reflection.
///
/// **T0165 forbids reintroducing a mirror**, and a mirror at this seam does not
/// look like a bug — it looks like backwards geometry or an inverted normal map,
/// which is exactly how the winding trap was misdiagnosed once already. This
/// makes it a log line at startup instead.
/// @param rotation a rotation quaternion.
/// @returns whether the matrix it produces has a positive determinant.
[[nodiscard]] bool isProperRotation(const hp::Quaternion& rotation);

} // namespace hped
