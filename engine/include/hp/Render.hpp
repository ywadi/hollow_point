// The render layer: device, context and swap chain (T0025, T0110, D15, D16).
//
// **No Diligent type appears in this header, and that is a rule rather than a
// preference.** `engine/CMakeLists.txt` links Diligent PRIVATE, and the moment a
// public header names one of its types every consumer — the editor, the
// runtime, every gameplay module — inherits Diligent's include path. That is not
// a build error; it silently widens the dependency surface, which is exactly
// the failure T0013's 13.3 was written to prevent. Everything Diligent lives
// behind the pimpl.
//
// Two things here belong to T0110 rather than T0025, and they are here because
// the API they drive puts them here:
//
//   * **Vsync is a per-call argument**, not a creation parameter —
//     `ISwapChain::Present(Uint32 SyncInterval)`. So it is held as state and
//     applied every present, which is what makes a runtime toggle possible at
//     all.
//   * **Toggling it recreates the swap chain.** Diligent forces
//     `VK_ERROR_OUT_OF_DATE_KHR` when the flag differs from the previous
//     present, so vsync changes and window resizes are the *same* recreate path.
//
// What vsync actually selects is not a free choice: Diligent derives the present
// mode from a boolean. On means FIFO_RELAXED then FIFO; off means MAILBOX, then
// IMMEDIATE, then FIFO. **FIFO_RELAXED can tear on a late frame** — Diligent's
// own comment says so — so "vsync on" is not a no-tearing guarantee here.
#pragma once

#include <hp/Api.hpp>
#include <hp/Layer.hpp>
#include <hp/Window.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace hp {

/// Which graphics backend to run on.
///
/// No Direct3D, and that is deliberate rather than missing: zig targets Windows
/// through the MinGW ABI, MinGW has no `atlbase.h`, and DiligentCore gates
/// D3D11/D3D12 on ATL. Both targets get Vulkan and OpenGL.
enum class RenderBackend : std::uint8_t {
    /// Pick the best available — Vulkan, falling back to OpenGL.
    Default,
    Vulkan,
    OpenGL,
};

/// How the render layer comes up.
struct RenderConfig {
    /// Which backend to use. `Default` tries Vulkan then OpenGL.
    RenderBackend backend = RenderBackend::Default;

    /// Wait for vertical blank when presenting (T0110).
    ///
    /// Applied per present, so it can be changed at run time — at the cost of a
    /// swap-chain recreate, which the loader does for you.
    bool vsync = true;

    /// Swap-chain buffer count.
    ///
    /// Two by default, which is Diligent's default and is what a FIFO present
    /// path wants. Three is what makes MAILBOX worth having (a spare buffer to
    /// replace while one is queued), so a low-latency uncapped configuration
    /// should raise it. Named here rather than left to the default so the
    /// choice is visible when someone tunes latency.
    std::uint32_t bufferCount = 2;

    /// Enable the backend's validation/debug layers.
    ///
    /// Defaults to on in a debug build. They are how device creation and
    /// shutdown ordering tell you they are wrong; without them both fail
    /// silently and much later.
    bool validation =
#if defined(NDEBUG)
        false;
#else
        true;
#endif
};

/// Owns the graphics device, the immediate context and the swap chain.
///
/// A layer, so it sits in the stack like everything else and gets `onDetach`
/// before teardown — which is where every other layer must have released its
/// GPU resources, because after this one detaches the device is gone (25.4).
class HP_API RenderLayer final : public ILayer {
public:
    /// Constructs the layer. Creates nothing until `onAttach`, so a layer can
    /// be built before there is a device to build.
    ///
    /// @param window the window to present into. Must outlive the layer, which
    ///        `Application` guarantees: it owns both, and tears the stack down
    ///        before the window. Handed in rather than fetched because `ILayer`
    ///        has no back-pointer to the application — layers are decoupled on
    ///        purpose (T0017), and a render layer is not the reason to change
    ///        that.
    /// @param config backend, vsync, buffer count and validation.
    explicit RenderLayer(Window& window, RenderConfig config = {});

    /// Releases the device if `onDetach` did not.
    ~RenderLayer() override;

    /// Not copyable: a copy would hold the same device and release it twice.
    RenderLayer(const RenderLayer&) = delete;

    /// Not copyable; see the copy constructor.
    /// @returns nothing -- deleted.
    RenderLayer& operator=(const RenderLayer&) = delete;

    /// Creates the device, context and swap chain against the application's
    /// window. Logs and leaves the layer inert on failure rather than throwing —
    /// an exception here would cross into the layer stack, and the conventions
    /// say engine code does not throw.
    void onAttach() override;

    /// Releases the swap chain, context and device, in that order.
    void onDetach() override;

    /// Clears the back buffer and presents (frame phases 10 and 11).
    void onRender() override;

    /// Resizes the swap chain when the window resizes (25.3).
    ///
    /// Taken from the event rather than polled, so the swap chain follows the
    /// window without `Application` having to know a render layer exists.
    /// @param event the event to inspect; only window resizes are acted on, and
    ///        none is consumed — other layers need to see a resize too.
    void onEvent(Event& event) override;

    /// @returns whether a device came up. Everything else is safe to call when
    ///          this is false; it simply does nothing.
    [[nodiscard]] bool ready() const;

    /// @returns the backend actually in use, which may differ from the one
    ///          requested when `Default` was asked for or a fallback happened.
    [[nodiscard]] RenderBackend backend() const;

    /// @returns a human-readable adapter description, for logs and an about box.
    ///          Empty when no device came up.
    [[nodiscard]] std::string adapterDescription() const;

    /// Resizes the swap chain. Call on a window resize; a no-op when the size
    /// is unchanged or the layer is inert.
    /// @param width new width in pixels.
    /// @param height new height in pixels.
    void resize(int width, int height);

    /// @returns the swap chain's current width in pixels, or 0 when inert.
    ///
    /// The *swap chain's* size, not the window's. They are supposed to track
    /// each other and the interesting failures are exactly when they do not —
    /// a window that resized while the chain did not renders undefined
    /// contents, which looks like a stale image rather than an error.
    [[nodiscard]] int swapChainWidth() const;

    /// @returns the swap chain's current height in pixels, or 0 when inert.
    [[nodiscard]] int swapChainHeight() const;

    /// @returns whether presents currently wait for vertical blank.
    [[nodiscard]] bool vsync() const;

    /// Turns vsync on or off at run time (T0110.1).
    ///
    /// Costs a swap-chain recreate on the next present, because Diligent
    /// derives the present mode from this and rebuilds when it changes. The
    /// device survives.
    /// @param enabled whether to wait for vertical blank.
    void setVsync(bool enabled);

    /// Colour the back buffer is cleared to, as linear RGBA in [0, 1].
    ///
    /// A temporary affordance: until there is anything to draw, the clear
    /// colour is the only evidence the device is alive, and a test needs to be
    /// able to change it. T0027's render stack owns clearing properly.
    /// @param r red.
    /// @param g green.
    /// @param b blue.
    /// @param a alpha.
    void setClearColour(float r, float g, float b, float a);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hp
