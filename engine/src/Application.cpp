#include <hp/Application.hpp>

#include <hp/Log.hpp>
#include <hp/Profiling.hpp>

namespace hp {
namespace {
const LogCategory kLog("app");
} // namespace

Application::Application(ApplicationConfig config) : config_(std::move(config)) {}

Application::~Application() = default;

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
            // T0015 pumps the OS/SDL event queue here. Nothing to poll yet, but
            // the zone exists so the shape of the frame is visible in a profile
            // from the first capture rather than appearing later.
            HP_PROFILE_ZONE_NAMED("poll");
        }

        {
            HP_PROFILE_ZONE_NAMED("update");
            // T0017's LayerStack is updated here, before the app's own hook.
            onUpdate(delta);
        }

        {
            HP_PROFILE_ZONE_NAMED("render");
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

    HP_LOG_INFO(kLog, "{} ran {} frame(s) in {:.3f}s, exit {}", config_.name, clock_.frame(),
                clock_.unscaledElapsed(), exitCode_);
    logFlush();

    return exitCode_;
}

} // namespace hp
