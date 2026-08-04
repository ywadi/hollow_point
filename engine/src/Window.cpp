#include <hp/Window.hpp>

#include <hp/Log.hpp>
#include <hp/Profiling.hpp>

#include <SDL3/SDL.h>

namespace hp {
namespace {
const LogCategory kLog("window");

/// SDL is initialised once for the process and shut down when the last window
/// goes. A refcount rather than an init-on-first-use static, because SDL_Quit
/// at static-destruction time runs after main returns, which is a bad place to
/// be tearing down a display connection.
int g_sdlRefs = 0;

bool acquireSdl() {
    if (g_sdlRefs == 0) {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
            HP_LOG_ERROR(kLog, "SDL_Init failed: {}", SDL_GetError());
            return false;
        }
        HP_LOG_INFO(kLog, "SDL {}.{}.{}, video driver '{}'", SDL_MAJOR_VERSION, SDL_MINOR_VERSION,
                    SDL_MICRO_VERSION, SDL_GetCurrentVideoDriver());
    }
    ++g_sdlRefs;
    return true;
}

void releaseSdl() {
    if (--g_sdlRefs == 0) {
        SDL_Quit();
    }
}
} // namespace

struct Window::Impl {
    SDL_Window* window = nullptr;
};

std::unique_ptr<Window> Window::create(const WindowConfig& config) {
    HP_PROFILE_ZONE();

    if (!acquireSdl()) {
        return nullptr;
    }

    SDL_WindowFlags flags = 0;
    if (config.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    // High-DPI by default. Opting *out* is the unusual choice, and a window that
    // is quietly blurry on a scaled display is the kind of thing nobody
    // attributes to a missing flag.
    flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (config.openGLContext) {
        // Must be a creation flag: SDL cannot add GL capability to an existing
        // window, so the backend decision precedes the window (T0025.2).
        flags |= SDL_WINDOW_OPENGL;
    }

    if (config.displayMode == DisplayMode::BorderlessFullscreen) {
        // Set at creation as well as being switchable later: opening straight
        // into fullscreen avoids a visible windowed flash at startup, which is
        // the kind of thing that reads as a bug rather than a frame of latency.
        flags |= SDL_WINDOW_FULLSCREEN;
    }

    // Place it on the requested display by creating with a position on that
    // display's desktop rect. SDL_CreateWindow takes no display id, and
    // SDL_WINDOW_FULLSCREEN follows whichever display the window is on.
    SDL_Window* sdlWindow = nullptr;
    {
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, config.title.c_str());
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, config.width);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, config.height);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER,
                              static_cast<Sint64>(flags));

        int displayCount = 0;
        SDL_DisplayID* ids = SDL_GetDisplays(&displayCount);
        if (ids != nullptr) {
            // Out of range falls back to the primary rather than failing: a
            // monitor remembered from a previous session may simply not be
            // plugged in now, and refusing to start is the wrong response.
            const int wanted = (config.displayIndex >= 0 && config.displayIndex < displayCount)
                                   ? config.displayIndex
                                   : 0;
            if (wanted != config.displayIndex) {
                HP_LOG_WARN(kLog, "display {} not present ({} attached); using the primary",
                            config.displayIndex, displayCount);
            }
            SDL_Rect bounds{};
            if (SDL_GetDisplayBounds(ids[wanted], &bounds)) {
                SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER,
                                      bounds.x + (bounds.w - config.width) / 2);
                SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER,
                                      bounds.y + (bounds.h - config.height) / 2);
            }
            SDL_free(ids);
        }
        sdlWindow = SDL_CreateWindowWithProperties(props);
        SDL_DestroyProperties(props);
    }
    if (sdlWindow == nullptr) {
        HP_LOG_ERROR(kLog, "SDL_CreateWindow failed: {}", SDL_GetError());
        releaseSdl();
        return nullptr;
    }

    if (config.openGLContext) {
        // Diligent's GL backend attaches to whatever context is current, so one
        // has to exist *and* be current before the device is created. Held for
        // the window's lifetime; SDL destroys it with the window.
        if (SDL_GL_CreateContext(sdlWindow) == nullptr) {
            HP_LOG_ERROR(kLog, "SDL_GL_CreateContext failed: {}", SDL_GetError());
            SDL_DestroyWindow(sdlWindow);
            releaseSdl();
            return nullptr;
        }
        HP_LOG_INFO(kLog, "OpenGL context created and made current");
    }

    // Not make_unique: the constructor is private, deliberately, so a Window can
    // only come from create() and cannot exist in a half-built state.
    std::unique_ptr<Window> window(new Window());
    window->impl_ = std::make_unique<Impl>();
    window->impl_->window = sdlWindow;
    window->title_ = config.title;

    HP_LOG_INFO(kLog, "window '{}' {}x{}", config.title, config.width, config.height);
    return window;
}

Window::~Window() {
    if (impl_ && impl_->window != nullptr) {
        SDL_DestroyWindow(impl_->window);
        releaseSdl();
    }
}

namespace {

/// SDL scancode to our layout-independent KeyCode.
///
/// Scancode, not keycode: a binding on "W" must stay where W sits on a US
/// layout even on AZERTY. SDL's *keycode* is the layout-mapped character, which
/// is what TextInputEvent is for.
KeyCode translateKey(SDL_Scancode code) {
    switch (code) {
    case SDL_SCANCODE_ESCAPE:
        return KeyCode::Escape;
    case SDL_SCANCODE_SPACE:
        return KeyCode::Space;
    case SDL_SCANCODE_RETURN:
        return KeyCode::Enter;
    case SDL_SCANCODE_TAB:
        return KeyCode::Tab;
    case SDL_SCANCODE_BACKSPACE:
        return KeyCode::Backspace;
    case SDL_SCANCODE_LEFT:
        return KeyCode::Left;
    case SDL_SCANCODE_RIGHT:
        return KeyCode::Right;
    case SDL_SCANCODE_UP:
        return KeyCode::Up;
    case SDL_SCANCODE_DOWN:
        return KeyCode::Down;
    default:
        break;
    }
    if (code >= SDL_SCANCODE_A && code <= SDL_SCANCODE_Z) {
        return static_cast<KeyCode>(static_cast<int>(KeyCode::A) + (code - SDL_SCANCODE_A));
    }
    if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9) {
        return static_cast<KeyCode>(static_cast<int>(KeyCode::Num1) + (code - SDL_SCANCODE_1));
    }
    if (code == SDL_SCANCODE_0) {
        return KeyCode::Num0;
    }
    if (code >= SDL_SCANCODE_F1 && code <= SDL_SCANCODE_F12) {
        return static_cast<KeyCode>(static_cast<int>(KeyCode::F1) + (code - SDL_SCANCODE_F1));
    }
    return KeyCode::Unknown;
}

KeyModifiers translateModifiers(SDL_Keymod mod) {
    KeyModifiers mods;
    mods.shift = (mod & SDL_KMOD_SHIFT) != 0;
    mods.control = (mod & SDL_KMOD_CTRL) != 0;
    mods.alt = (mod & SDL_KMOD_ALT) != 0;
    mods.super = (mod & SDL_KMOD_GUI) != 0;
    return mods;
}

MouseButton translateButton(Uint8 button) {
    switch (button) {
    case SDL_BUTTON_LEFT:
        return MouseButton::Left;
    case SDL_BUTTON_RIGHT:
        return MouseButton::Right;
    case SDL_BUTTON_MIDDLE:
        return MouseButton::Middle;
    case SDL_BUTTON_X1:
        return MouseButton::X1;
    case SDL_BUTTON_X2:
        return MouseButton::X2;
    default:
        return MouseButton::Unknown;
    }
}

} // namespace

WindowEvents Window::pumpEvents(const std::function<void(Event&)>& onEvent) {
    HP_PROFILE_ZONE();

    WindowEvents events;
    events.width = width();
    events.height = height();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            events.closeRequested = true;
            break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            // Per-window, unlike QUIT. With one window they coincide; with an
            // editor that grows tool windows they will not.
            if (event.window.windowID == SDL_GetWindowID(impl_->window)) {
                events.closeRequested = true;
            }
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            // Pixel size, not the logical size: the swap chain is sized in
            // pixels, and on a scaled display the two differ.
            events.resized = true;
            events.width = event.window.data1;
            events.height = event.window.data2;
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            if (onEvent) {
                WindowFocusEvent focus(event.type == SDL_EVENT_WINDOW_FOCUS_GAINED);
                onEvent(focus);
            }
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            if (onEvent) {
                KeyEvent key(translateKey(event.key.scancode), event.type == SDL_EVENT_KEY_DOWN,
                             translateModifiers(event.key.mod), event.key.repeat);
                onEvent(key);
            }
            break;
        case SDL_EVENT_TEXT_INPUT:
            if (onEvent && event.text.text != nullptr) {
                // One event per code point. SDL hands us UTF-8; decoding is
                // deliberately minimal here because anything beyond ASCII is
                // T0117's problem, and guessing at it now would be a second
                // implementation to remove later.
                for (const char* c = event.text.text; *c != '\0'; ++c) {
                    if ((static_cast<unsigned char>(*c) & 0x80U) != 0) {
                        continue; // non-ASCII: left to T0117
                    }
                    TextInputEvent text(static_cast<std::uint32_t>(*c));
                    onEvent(text);
                }
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (onEvent) {
                MouseButtonEvent button(translateButton(event.button.button),
                                        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN, event.button.x,
                                        event.button.y);
                onEvent(button);
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (onEvent) {
                MouseMovedEvent moved(event.motion.x, event.motion.y, event.motion.xrel,
                                      event.motion.yrel);
                onEvent(moved);
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (onEvent) {
                MouseScrolledEvent scrolled(event.wheel.x, event.wheel.y);
                onEvent(scrolled);
            }
            break;
        default:
            break;
        }
    }
    return events;
}

int Window::width() const {
    int w = 0;
    int h = 0;
    if (impl_ && impl_->window != nullptr) {
        SDL_GetWindowSizeInPixels(impl_->window, &w, &h);
    }
    return w;
}

int Window::height() const {
    int w = 0;
    int h = 0;
    if (impl_ && impl_->window != nullptr) {
        SDL_GetWindowSizeInPixels(impl_->window, &w, &h);
    }
    return h;
}

DisplayMode Window::displayMode() const {
    if (!impl_ || impl_->window == nullptr) {
        return DisplayMode::Windowed;
    }
    const SDL_WindowFlags flags = SDL_GetWindowFlags(impl_->window);
    return (flags & SDL_WINDOW_FULLSCREEN) != 0 ? DisplayMode::BorderlessFullscreen
                                                : DisplayMode::Windowed;
}

bool Window::setDisplayMode(DisplayMode mode) {
    if (!impl_ || impl_->window == nullptr) {
        return false;
    }
    if (displayMode() == mode) {
        return true;
    }
    // SDL3 without a mode struct is *borderless* fullscreen: it takes the
    // desktop resolution rather than changing it. That is the whole reason
    // exclusive fullscreen is a separate decision and not a parameter here.
    if (!SDL_SetWindowFullscreen(impl_->window, mode == DisplayMode::BorderlessFullscreen)) {
        HP_LOG_ERROR(kLog, "SDL_SetWindowFullscreen failed: {}", SDL_GetError());
        return false;
    }
    // **The change is asynchronous**, and this cost an hour of confusion before
    // it was noticed: SDL applies a fullscreen transition when the window
    // system next reports it, so immediately after the call `SDL_GetWindowFlags`
    // still describes the *old* state. Without this sync, `displayMode()`
    // returns the previous mode and `width()`/`height()` the previous size --
    // measured, one step behind, every time.
    //
    // SDL_SyncWindow blocks until pending state has been applied, which makes
    // the setter mean what it says. It is also exactly the blocking T0015's
    // threading caveat warns about: this runs on the thread that owns the
    // window and can take longer than a frame.
    SDL_SyncWindow(impl_->window);
    // The swap chain is not touched here. SDL emits a resize, the application
    // pump turns it into a WindowResizeEvent, and the render layer resizes
    // through the path 25.3 already built -- so a mode switch and a dragged
    // window edge are the same code, which is what stops them diverging.
    HP_LOG_INFO(kLog, "display mode -> {}",
                mode == DisplayMode::BorderlessFullscreen ? "borderless fullscreen" : "windowed");
    return true;
}

bool Window::setSize(int width, int height) {
    if (!impl_ || impl_->window == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (displayMode() != DisplayMode::Windowed) {
        // Not an error. A settings UI applying a saved resolution before
        // restoring windowed mode is ordinary, and failing it would make the
        // caller sequence two operations that have no reason to be ordered.
        HP_LOG_DEBUG(kLog, "ignoring setSize while fullscreen");
        return false;
    }
    if (!SDL_SetWindowSize(impl_->window, width, height)) {
        HP_LOG_ERROR(kLog, "SDL_SetWindowSize failed: {}", SDL_GetError());
        return false;
    }
    SDL_SyncWindow(impl_->window); // asynchronous too; see setDisplayMode
    return true;
}

float Window::displayScale() const {
    if (!impl_ || impl_->window == nullptr) {
        return 1.0F;
    }
    const float scale = SDL_GetWindowDisplayScale(impl_->window);
    return scale > 0.0F ? scale : 1.0F;
}

std::vector<DisplayInfo> Window::displays() {
    std::vector<DisplayInfo> out;
    // Enumerable without a window: a settings UI lists monitors before anything
    // has been opened on one. acquireSdl/releaseSdl are refcounted, so this is
    // safe whether or not a window exists.
    if (!acquireSdl()) {
        return out;
    }
    int count = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&count);
    if (ids != nullptr) {
        for (int i = 0; i < count; ++i) {
            DisplayInfo info;
            info.index = i;
            const char* name = SDL_GetDisplayName(ids[i]);
            info.name = name != nullptr ? name : "display";
            if (const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(ids[i])) {
                info.width = mode->w;
                info.height = mode->h;
            }
            const float scale = SDL_GetDisplayContentScale(ids[i]);
            info.scale = scale > 0.0F ? scale : 1.0F;
            out.push_back(std::move(info));
        }
        SDL_free(ids);
    }
    releaseSdl();
    return out;
}

NativeWindowHandles Window::nativeHandles() const {
    NativeWindowHandles handles;
    if (!impl_ || impl_->window == nullptr) {
        return handles;
    }
    SDL_PropertiesID props = SDL_GetWindowProperties(impl_->window);

#if defined(SDL_PLATFORM_WIN32)
    handles.windowHandle =
        SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
    handles.displayHandle =
        SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
    handles.windowId = static_cast<std::uint64_t>(
        SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
#endif
    return handles;
}

} // namespace hp
