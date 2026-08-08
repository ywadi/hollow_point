// The render layer: device, context and swap chain (T0025, T0110, D15, D16).
//
// **Amended by D22 (2026-08-05).** This header used to say no Diligent type
// could appear in it at all. It now forward-declares three, and hands them out,
// because a gameplay-authored render layer (T0027, T0094) cannot draw anything
// without the device context. That is sound rather than a concession: Diligent's
// interfaces are pure-virtual, so a module calls through these pointers with no
// Diligent library linked, while device *creation* stays impossible for it —
// the factories are free functions in libraries linked PRIVATE, so a module that
// tries fails to link. Gameplay may use what it is handed and may never make one.
//
// The forward declarations are what keep the original concern addressed: `engine/CMakeLists.txt` links Diligent PRIVATE, and the moment a
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
#include <hp/DepthConvention.hpp>
#include <hp/Layer.hpp>
#include <hp/Window.hpp>

#include <cstdint>
#include <memory>
#include <cstddef>
#include <string>

namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
struct ITexture;
struct ISwapChain;
} // namespace Diligent

namespace hp {

/// Which graphics backend to run on.
///
/// **Vulkan is the only backend (D29).** OpenGL was removed — a small studio
/// cannot support two backends, the second one was worth low single digits of
/// players, and keeping it pinned the engine's shaders to the subset both
/// compilers accept (D28). No Direct3D either, and that is deliberate rather
/// than missing: zig targets Windows through the MinGW ABI, MinGW has no
/// `atlbase.h`, and DiligentCore gates D3D11/D3D12 on ATL.
enum class RenderBackend : std::uint8_t {
    /// The engine's default backend. **This means Vulkan, with no fallback**:
    /// it used to mean "try Vulkan, fall back to OpenGL", and D29 removed the
    /// fallback. A machine without Vulkan does not start the renderer, and
    /// says so in the log.
    Default,
    Vulkan,
};

/// How the render layer comes up.
struct RenderConfig {
    /// Which backend to use. `Default` and `Vulkan` are the same device today;
    /// `Default` is what to write unless a bug report needs the request pinned.
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

    /// Create a Dear ImGui context and draw it over the presented frame
    /// (T0032.2).
    ///
    /// **Off by default, and the default is the interesting half.** ImGui is
    /// linked into every binary here whether anyone asked or not — `DiligentFX`
    /// links `Diligent-Imgui` PUBLIC and its post-process components call
    /// `ImGui::` for their own settings panels (**D6**) — so "is ImGui
    /// available" was never the question. What this decides is whether a
    /// *context* exists and whether the platform backend eats the mouse, and a
    /// shipped game that never draws a debug panel should pay neither.
    ///
    /// It lives on the render layer rather than in an app because the two things
    /// the vendored backend needs — the `SDL_Window*` and the device — are both
    /// here, and because the UI has to draw between the present blit and
    /// `Present`, which is a sequence only this layer can order.
    bool ui = false;
};

/// What a second binary needs in order to draw into the engine's ImGui context
/// (T0032.2).
///
/// **This exists because Dear ImGui is statically linked into more than one
/// module in this process, and that is not an accident to be fixed.** The engine
/// is a shared library (**D12**) linking ImGui privately; an app that wants to
/// draw panels needs the ImGui *API*, so it links ImGui too and ends up with a
/// second copy of `GImGui` and of the allocator globals. Sharing the context is
/// Dear ImGui's own documented answer to exactly this (`SetCurrentContext` plus
/// `SetAllocatorFunctions`), and it is sound here because both copies are
/// compiled from the same source with the same `IMGUI_USER_CONFIG`, so the
/// structures they disagree about are none.
///
/// The alternative — the app owning the context — is not available: the platform
/// backend is SDL's, SDL3 is linked statically and privately into the engine,
/// and a second copy of SDL in one process has its own event queue and has never
/// heard of this window. See `Window::platformWindow`.
///
/// Deliberately untyped. Naming `ImGuiContext` here would put ImGui on the
/// include path of every consumer of this header, including gameplay modules,
/// to describe four values.
struct UiBinding {
    /// The `ImGuiContext*`. Null when no context exists.
    void* context = nullptr;

    /// ImGui's allocation function, as `SetAllocatorFunctions` wants it.
    void* (*alloc)(std::size_t, void*) = nullptr;

    /// ImGui's deallocation function.
    void (*release)(void*, void*) = nullptr;

    /// The user data both were registered with.
    void* userData = nullptr;

    /// @returns whether this describes a usable context.
    [[nodiscard]] bool valid() const { return context != nullptr && alloc != nullptr; }
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

    /// Opens the UI frame, when there is one (T0032.2).
    ///
    /// Late update rather than render, because layers render in push order and
    /// this one is pushed last: a frame opened in `onRender` would open after
    /// every panel had already tried to draw into it.
    /// @param deltaSeconds the frame delta. Unused; ImGui takes its own from the
    ///        platform backend.
    void onLateUpdate(double deltaSeconds) override;

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

    /// @returns the backend actually in use. `Vulkan` once a device is up;
    ///          `Default` only while no device has been created.
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

    /// @returns whether a Dear ImGui context exists — `RenderConfig::ui` was set
    ///          and the device came up.
    [[nodiscard]] bool uiReady() const;

    /// @returns what another module needs to draw into this context, or an
    ///          empty binding when there is none. See `UiBinding`.
    [[nodiscard]] UiBinding uiBinding() const;

    /// Points ImGui's layout file at `path` (T0032.5).
    ///
    /// **Someone has to**: `ImGuiImplDiligent` sets `io.IniFilename = nullptr`
    /// in its constructor, so a context created through it never persists
    /// anything until told where to. The string is stored here because ImGui
    /// keeps the pointer rather than the characters, and a caller's temporary
    /// is exactly the kind of dangling that shows up as a layout that saves to
    /// a garbage path once in a while.
    ///
    /// @param path a host filesystem path, or empty to stop persisting. The
    ///        directory must already exist; ImGui does not create one.
    /// @returns nothing.
    void setUiLayoutPath(std::string path);

    /// Feeds a platform event to the UI backend and reports what the UI wants.
    ///
    /// @returns whether the UI is currently capturing the mouse or the keyboard,
    ///          which a layer below should treat as "this input was not for me".
    ///          Always false when there is no context.
    [[nodiscard]] bool uiWantsInput() const;

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

    /// @returns the graphics device, or nullptr when the layer is inert.
    ///
    /// Handed out so passes and gameplay-authored layers can create resources
    /// and issue draws (D22). The engine owns it and destroys it in `onDetach`;
    /// **anything holding this pointer must release its GPU resources by then**,
    /// which is the whole reason `RenderLayer` is a layer and gets `onDetach`
    /// before teardown (25.4).
    [[nodiscard]] Diligent::IRenderDevice* device() const;

    /// @returns the immediate context, or nullptr when the layer is inert.
    ///
    /// **Valid for the frame, not beyond**, and not thread-safe: it is the
    /// immediate context, so it belongs to whichever thread runs the frame.
    [[nodiscard]] Diligent::IDeviceContext* context() const;

    /// @returns the swap chain, or nullptr when the layer is inert. Its back
    ///          buffer is where a composited frame ultimately lands.
    [[nodiscard]] Diligent::ISwapChain* swapChain() const;

    /// @returns the device's texture-space convention (`yToV`), or the default
    ///          when the layer is inert — which is deliberately not a usable
    ///          value; see `ClipSpace`.
    ///
    /// **Read this rather than assuming it.** A render-to-texture pass that
    /// hardcodes the flip is one device report away from sampling upside down;
    /// see `ClipSpace` and T0130.3.
    [[nodiscard]] ClipSpace clipSpace() const;

    /// Copies a texture onto the back buffer just before presenting.
    ///
    /// **A development path, and deliberately a crude one.** Phase 4 otherwise
    /// has no on-screen output at all: the two real consumers of a rendered
    /// frame are the editor viewport (T0033, Phase 6) and the runtime (T0042,
    /// Phase 8), so every render ticket before those would be unverifiable by
    /// eye. This is what makes a frame visible in the meantime, and T0033
    /// replaces it rather than building on it.
    ///
    /// It is a **sampled fullscreen pass** (T0137), not a copy, so the source
    /// need not match the back buffer in format or size. It used to be a
    /// `CopyTexture`, which requires an exact format match — and never had one
    /// on Vulkan, whose surface here is BGRA against the scene target's RGBA, so
    /// the engine's own default backend showed a clear colour and looked broken.
    ///
    /// @param texture the texture to present, or nullptr to stop. Must be
    ///        created with `BIND_SHADER_RESOURCE`. Not retained beyond the next
    ///        frame's use — the caller owns it, and a caller that releases it
    ///        must clear this first.
    /// @returns nothing.
    void setPresentSource(Diligent::ITexture* texture);

    /// Draws `source` over the whole of `destination`, sampling it.
    ///
    /// **The engine's fullscreen-pass primitive**, exposed because it is not
    /// only the present path's: T0096's tonemapping is a fullscreen pass over an
    /// HDR target and T0120's render-to-texture needs the same draw, and a
    /// second implementation of it is how two of them come to disagree about the
    /// V flip. Pipelines are cached per destination format.
    ///
    /// Handles format conversion, colour space and a size difference in
    /// hardware. Binds `destination` as the only render target, with no
    /// depth-stencil view, and leaves it bound.
    ///
    /// @param source the texture to sample. Must have `BIND_SHADER_RESOURCE`.
    /// @param destination the render-target view to fill.
    /// @returns whether the draw was issued. False means the pipeline could not
    ///          be created or the source has no shader-resource view, and the
    ///          reason is logged.
    bool blitTexture(Diligent::ITexture* source, Diligent::ITextureView* destination);

private:
    /// Builds the ImGui context once the device and swap chain exist.
    /// @returns nothing.
    void createUi();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hp
