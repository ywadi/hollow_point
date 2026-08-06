// The platform window (T0015, D16).
//
// SDL3 owns window creation, the event queue and input. Diligent attaches to
// whatever native handle this exposes -- `LinuxNativeWindow` takes an X11
// `WindowId` and `Display*`, Win32 takes an `HWND` -- so the renderer (T0025)
// never needs to know a window came from SDL.
//
// This deliberately exposes *less* than SDL does. The engine's surface is the
// part the rest of the engine needs; reaching SDL directly from gameplay would
// make the platform layer impossible to change and would leak a C API across
// the module boundary.
#pragma once

#include <hp/Api.hpp>
#include <hp/Event.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hp {

/// How a window occupies the screen (T0129).
///
/// **Exclusive fullscreen is deliberately absent**, and the reason is recorded
/// rather than left as an omission: it changes the *display's* state rather
/// than the window's, which is what makes alt-tab, multi-monitor and
/// crash-recovery harder with it. What it buys — a true immediate present path
/// on some drivers — is a latency optimisation nobody here has measured a need
/// for. See T0129 for what evidence would reopen it.
enum class DisplayMode : std::uint8_t {
    /// A normal window, sized by the config or by the user dragging it.
    Windowed,
    /// Fills the current display, no decorations, no mode change. The desktop
    /// resolution is used as-is, so alt-tab is instant and nothing is restored
    /// on a crash.
    BorderlessFullscreen,
};

/// A display the window could be placed on.
struct DisplayInfo {
    /// Index to pass as `WindowConfig::displayIndex`. Not stable across
    /// hotplug, so it is a selection made now rather than an identity to store.
    int index = 0;
    /// Human-readable name, for a settings UI.
    std::string name;
    /// Desktop resolution in pixels.
    int width = 0;
    int height = 0;
    /// Content scale — 1.0 at 100%, 2.0 on a doubled display. Logical and pixel
    /// sizes differ by this, which matters because the window already asks for
    /// `SDL_WINDOW_HIGH_PIXEL_DENSITY`.
    float scale = 1.0F;
};

struct WindowConfig {
    std::string title = "HollowPoint";
    int width = 1280;
    int height = 720;
    bool resizable = true;

    /// Which display mode to open in (T0129).
    ///
    /// Changeable afterwards too — `Window::setDisplayMode`. It is here as well
    /// because opening directly into fullscreen avoids a visible windowed flash
    /// at startup.
    ///
    /// (An `openGLContext` flag used to sit beside this one, because a GL
    /// context is an SDL *creation* flag and forced the backend choice to
    /// precede the window. D29 removed the OpenGL backend, and with it the only
    /// window-creation decision the renderer ever imposed.)
    DisplayMode displayMode = DisplayMode::Windowed;

    /// Index of the display to open on, from `Window::displays()`. Out of range
    /// falls back to the primary display rather than failing — a monitor
    /// remembered from a previous session may simply not be plugged in now, and
    /// refusing to start is the wrong response to that.
    int displayIndex = 0;
};

/// Native handles, in the shape Diligent's `NativeWindow` structs want.
///
/// Deliberately opaque `void*`/`uint64_t` rather than including any platform
/// header: this header is included by engine code that has no business seeing
/// `Xlib.h` or `windows.h`, and `windows.h` in particular poisons a translation
/// unit with macros like `min`, `max` and `CreateWindow`.
struct NativeWindowHandles {
    void* windowHandle = nullptr;  ///< HWND on Windows.
    void* displayHandle = nullptr; ///< Display* on X11.
    std::uint64_t windowId = 0;    ///< X11 Window id.
};

/// What happened since the last pump. Deliberately a small value type rather
/// than a callback: T0018 owns the real event system, and a callback API here
/// would be the wrong thing to migrate away from later.
struct WindowEvents {
    bool closeRequested = false;
    bool resized = false;
    int width = 0;
    int height = 0;
};

class HP_API Window {
public:
    /// @param config size, title and resizability of the window to open.
    /// @returns the window, or null on failure -- having already logged why, so
    ///        a caller that only needs to abort does not have to ask.
    static std::unique_ptr<Window> create(const WindowConfig& config);

    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    /// Drains the platform event queue. Call once per frame, before update.
    ///
    /// @param onEvent called for each input event as it is translated, so it can
    ///        be dispatched immediately. Passed as a callback rather than
    ///        returned as a queue because an event is a message passing
    ///        through, not an object with a lifetime -- queueing would mean
    ///        heap-allocating polymorphic events every frame to keep them alive
    ///        past this call, for no benefit.
    /// @returns close and resize state, which the loop acts on directly.
    WindowEvents pumpEvents(const std::function<void(Event&)>& onEvent = {});

    int width() const;
    int height() const;

    const std::string& title() const { return title_; }

    NativeWindowHandles nativeHandles() const;

    /// @returns the current display mode.
    [[nodiscard]] DisplayMode displayMode() const;

    /// Switches between windowed and borderless fullscreen at run time (129.2).
    ///
    /// The swap chain follows automatically: SDL emits a resize, which reaches
    /// the render layer through the normal event path (25.3). Nothing here
    /// touches the device.
    ///
    /// @param mode the mode to switch to. Setting the current mode is a no-op.
    /// @returns whether the switch succeeded; on failure the previous mode is
    ///          retained and the reason is logged.
    bool setDisplayMode(DisplayMode mode);

    /// Changes the windowed size at run time (129.3).
    ///
    /// Ignored while in fullscreen, where the size is the display's — silently,
    /// because a settings UI applying a saved resolution before restoring
    /// windowed mode is normal rather than an error.
    ///
    /// @param width new width in logical units.
    /// @param height new height in logical units.
    /// @returns whether the size was applied.
    bool setSize(int width, int height);

    /// @returns the content scale of the display this window is on — 1.0 at
    ///          100%, 2.0 on a doubled display. This is why `width()` (logical)
    ///          and the swap chain's size (pixels) are not the same number.
    [[nodiscard]] float displayScale() const;

    /// @returns every display currently attached, in SDL's order. The first is
    ///          the primary. Empty only if the video subsystem is unavailable.
    [[nodiscard]] static std::vector<DisplayInfo> displays();

private:
    Window() = default;

    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string title_;
};

} // namespace hp
