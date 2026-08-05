// Rendering a scene into an offscreen target (T0028.4/28.5).
//
// **Why offscreen rather than straight to the swap chain**: the editor viewport
// is an ImGui image of this texture (T0033), and the runtime stretches the same
// texture full-window (T0042). Both apps then share this code unchanged, and the
// editor's existence stops being something the renderer has to know about.
//
// This owns the target and the camera resolve; it does **not** own the event.
// `render` returns the texture and the caller publishes a `FrameRenderedEvent`,
// because dispatch belongs to `Application` and an engine class reaching for the
// layer stack to announce itself would invert that.
#pragma once

#include <hp/Api.hpp>
#include <hp/Camera.hpp>
#include <hp/DrawSubmission.hpp>
#include <hp/FrameTargets.hpp>
#include <hp/SceneRenderer.hpp>

#include <cstdint>
#include <memory>

namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
struct ITextureView;
} // namespace Diligent

namespace hp {

class Scene;
class AssetPool;

/// What one frame of scene rendering did.
struct SceneViewStats {
    /// Entities the parse step considered.
    std::size_t considered = 0;

    /// Draw items submitted to the device.
    std::size_t submitted = 0;

    /// Items whose mesh was not loaded. Drawn as nothing, deliberately.
    std::size_t missingMesh = 0;

    /// Whether a camera was found for the requested view slot.
    ///
    /// **False is not an error and not a crash** — it is a scene nobody has put
    /// a camera in yet, which is the normal state of a scene being built. The
    /// frame clears and publishes nothing.
    bool hadCamera = false;
};

/// Renders a scene into its own colour and depth targets.
///
/// One of these per view. A second view — a portal, a security monitor, a
/// thumbnail — is a second instance with its own targets and its own view slot,
/// which is what T0120.2 asks for and why nothing here is a singleton.
class HP_API SceneView {
public:
    /// Constructs an empty view holding no GPU resources.
    SceneView();

    /// Releases the targets and the renderer.
    ~SceneView();

    /// Not copyable: it owns GPU objects.
    SceneView(const SceneView&) = delete;

    /// Not copyable; see the copy constructor.
    /// @returns nothing -- deleted.
    SceneView& operator=(const SceneView&) = delete;

    /// Moves the view.
    /// @param other the view to move from.
    SceneView(SceneView&& other) noexcept;

    /// Moves the view, releasing whatever this one held.
    /// @param other the view to move from.
    /// @returns this view.
    SceneView& operator=(SceneView&& other) noexcept;

    /// Creates the targets and the renderer.
    /// @param device the device to create on. Must not be null.
    /// @param context the immediate context, for one-off setup. Must not be null.
    /// @param width initial width in pixels. Clamped to at least 1.
    /// @param height initial height in pixels. Clamped to at least 1.
    /// @param colour the colour target's format. `ColourHDR` is what T0096 will
    ///        want; `Colour` is what composites directly to a swap chain today.
    /// @returns whether everything came up. False leaves this empty, never
    ///          half-created.
    bool create(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context, int width,
                int height, TargetFormat colour = TargetFormat::Colour);

    /// Resizes the targets. A no-op when the size is unchanged, so it is safe to
    /// call every frame.
    /// @param width new width in pixels.
    /// @param height new height in pixels.
    /// @returns whether the targets are usable afterwards.
    bool resize(int width, int height);

    /// Releases everything. Safe to call more than once.
    /// @returns nothing.
    void release();

    /// @returns whether `create` succeeded and this view can render.
    [[nodiscard]] bool valid() const;

    /// Renders one frame of the scene.
    ///
    /// Binds the targets, clears them — colour to `clearColour`, depth to
    /// `kDepthClearValue`, which is 0 under reverse-Z — resolves the camera for
    /// `viewSlot`, and submits every drawable entity.
    /// @param context the immediate context. Must not be null.
    /// @param scene the scene to draw.
    /// @param pool where mesh GUIDs resolve.
    /// @param clip the device's clip-space convention, from
    ///        `RenderLayer::clipSpace()`.
    /// @param viewSlot which camera slot to resolve. Slot 0 is the world.
    /// @param stats optional; filled with what the frame did.
    /// @returns the colour target to display, or **nullptr when nothing was
    ///          published** — no camera, or the view could not be built. A null
    ///          return is the signal not to emit a `FrameRenderedEvent`.
    Diligent::ITextureView* render(Diligent::IDeviceContext* context, Scene& scene,
                                   const AssetPool& pool, ClipSpace clip,
                                   std::uint8_t viewSlot = 0, SceneViewStats* stats = nullptr);

    /// @returns the colour target's shader-resource view, or nullptr. **Valid
    ///          for the current frame only** — a resize recreates it.
    [[nodiscard]] Diligent::ITextureView* colour() const;

    /// @returns the current target width in pixels, or 0.
    [[nodiscard]] int width() const;

    /// @returns the current target height in pixels, or 0.
    [[nodiscard]] int height() const;

    /// Colour the target is cleared to, as linear RGBA.
    ///
    /// Not black by default: a black frame and a broken frame look identical,
    /// and "did anything run?" is the first question asked of a viewport that
    /// shows nothing.
    /// @param r red.
    /// @param g green.
    /// @param b blue.
    /// @param a alpha.
    /// @returns nothing.
    void setClearColour(float r, float g, float b, float a);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hp
