#include <hp/Application.hpp>

#include <hp/Event.hpp>
#include <hp/Log.hpp>
#include <hp/Profiling.hpp>

namespace hp {
namespace {
const LogCategory kLog("app");
} // namespace

Application::Application(ApplicationConfig config) : config_(std::move(config)) {}

Application::~Application() = default;

void Application::dispatch(Event& event) {
    HP_PROFILE_ZONE();
    layers_.dispatch(event);
    if (!event.isConsumed()) {
        onEvent(event);
    }
}

void Application::requestExit(int exitCode) {
    // Only records intent. The loop finishes the frame it is in, so a hook can
    // call this without the ground disappearing underneath it.
    exitCode_ = exitCode;
    running_ = false;
}

int Application::run() {
    HP_PROFILE_ZONE_NAMED("Application::run");
    HP_PROFILE_THREAD("main");

    HP_LOG_INFO(kLog, "starting {}", config_.name);

    if (!config_.headless) {
        WindowConfig windowConfig = config_.window;
        if (windowConfig.title.empty()) {
            windowConfig.title = config_.name;
        }
        window_ = Window::create(windowConfig);
        if (!window_) {
            HP_LOG_ERROR(kLog, "could not create a window; aborting startup");
            return 1;
        }
    }

    running_ = true;
    exitCode_ = 0;

    onStartup();

    while (running_) {
        HP_PROFILE_ZONE_NAMED("frame");

        // Time first, so everything in this frame sees one delta. A system that
        // reads the clock itself mid-frame gets a different answer from the one
        // before it, and the two drift apart -- see T0057.
        clock_.tick();
        const double delta = clock_.delta();

        {
            HP_PROFILE_ZONE_NAMED("poll");
            if (window_) {
                const WindowEvents events = window_->pumpEvents([this](Event& e) { dispatch(e); });
                if (events.resized) {
                    // Delivered as an event so layers can react, and as a hook
                    // so an application need not add a layer just to resize.
                    WindowResizeEvent resize(events.width, events.height);
                    dispatch(resize);
                    onResize(events.width, events.height);
                }
                if (events.closeRequested) {
                    WindowCloseEvent close;
                    dispatch(close);
                    if (!close.isConsumed()) {
                        // Unconsumed means nobody objected -- a layer wanting
                        // "really quit?" consumes it and asks.
                        HP_LOG_INFO(kLog, "window close requested");
                        requestExit(0);
                    }
                }
            }
        }

        {
            HP_PROFILE_ZONE_NAMED("update");
            // Bottom-up: the world simulates before the interface over it.
            layers_.update(delta);
            onUpdate(delta);
        }

        {
            HP_PROFILE_ZONE_NAMED("render");
            layers_.render();
            onRender();
        }

        {
            // T0025 presents here, and T0110 decides how -- present mode and
            // pacing are an unowned default today (Diligent picks IMMEDIATE),
            // which is exactly what that ticket exists to fix.
            HP_PROFILE_ZONE_NAMED("present");
        }

        HP_PROFILE_FRAME();

        if (config_.exitAfterFrames && clock_.frame() >= *config_.exitAfterFrames) {
            HP_LOG_DEBUG(kLog, "frame budget reached ({}), stopping", *config_.exitAfterFrames);
            running_ = false;
        }
    }

    // Before anything is torn down: the app still holds everything it created.
    onShutdown();

    // Then layers, top-down, while the window still exists -- a layer holding a
    // GPU resource must release it before the surface it was made against
    // (T0025).
    layers_.clear();

    // And only then the window. Layers detach in onShutdown, so anything
    // holding a swap chain or a GPU resource is gone before the surface it was
    // created against -- the ordering T0025 will depend on.
    window_.reset();

    HP_LOG_INFO(kLog, "{} ran {} frame(s) in {:.3f}s, exit {}", config_.name, clock_.frame(),
                clock_.unscaledElapsed(), exitCode_);
    logFlush();

    return exitCode_;
}

} // namespace hp
