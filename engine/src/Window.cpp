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

    SDL_Window* sdlWindow =
        SDL_CreateWindow(config.title.c_str(), config.width, config.height, flags);
    if (sdlWindow == nullptr) {
        HP_LOG_ERROR(kLog, "SDL_CreateWindow failed: {}", SDL_GetError());
        releaseSdl();
        return nullptr;
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

WindowEvents Window::pumpEvents() {
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
