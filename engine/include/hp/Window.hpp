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

#include <cstdint>
#include <memory>
#include <string>

namespace hp {

struct WindowConfig {
    std::string title = "HollowPoint";
    int width = 1280;
    int height = 720;
    bool resizable = true;
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
    /// Returns null on failure, having logged why.
    static std::unique_ptr<Window> create(const WindowConfig& config);

    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    /// Drains the platform event queue. Call once per frame, before update.
    WindowEvents pumpEvents();

    int width() const;
    int height() const;

    const std::string& title() const { return title_; }

    NativeWindowHandles nativeHandles() const;

private:
    Window() = default;

    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string title_;
};

} // namespace hp
