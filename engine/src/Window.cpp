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

    SDL_Window* sdlWindow =
        SDL_CreateWindow(config.title.c_str(), config.width, config.height, flags);
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
