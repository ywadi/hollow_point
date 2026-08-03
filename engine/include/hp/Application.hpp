// The application and its frame loop (T0014).
//
// The engine is a *library*, so it cannot own `main()`. Instead it owns the
// loop and exposes two things a program provides: a subclass of `Application`,
// and a `createApplication()` factory. `<hp/EntryPoint.hpp>` supplies the
// `main` that ties them together, so an app becomes an executable by including
// one header.
//
// That indirection is the whole point of T0013 -- the editor and the runtime are
// two different programs over one engine, and the engine knows about neither.
//
// **Deliberately thin.** The reference this project is learning from let this
// class become a dumping ground and had to be refactored around a layer stack
// afterwards. `Application` owns the window, the layer stack and the loop --
// nothing else. Everything else is a layer. If a feature is being added *here*,
// it is almost certainly a layer instead (T0017).
#pragma once

#include <hp/Api.hpp>
#include <hp/Time.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace hp {

struct ApplicationConfig {
    std::string name = "HollowPoint";

    /// Stop after this many frames. **Scaffolding**, and it should go away.
    ///
    /// There is no window yet (T0015), so nothing can be closed and the loop
    /// would otherwise never end. It also gives tests a way to run a real loop
    /// deterministically rather than racing a timer. When T0015 lands, the
    /// window's close event becomes the exit condition and this stays only as a
    /// test affordance.
    std::optional<std::uint64_t> exitAfterFrames;
};

class HP_API Application {
public:
    explicit Application(ApplicationConfig config = {});
    virtual ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    /// Runs until something asks to stop. Returns the process exit code.
    int run();

    /// Asks the loop to finish after the current frame. Safe to call from a
    /// frame hook; the loop is not torn down underneath the caller.
    void requestExit(int exitCode = 0);

    const ApplicationConfig& config() const { return config_; }

    /// The game clock. Scaled and pausable -- see T0057. The editor will own a
    /// second, unscaled clock when it needs one.
    Clock& clock() { return clock_; }

    const Clock& clock() const { return clock_; }

    std::uint64_t frame() const { return clock_.frame(); }

protected:
    /// Called once, after the engine is up and before the first frame.
    virtual void onStartup() {}

    /// Called once per frame with the scaled delta in seconds.
    virtual void onUpdate(double deltaSeconds) { (void)deltaSeconds; }

    /// Called once per frame, after update.
    virtual void onRender() {}

    /// Called once, before the engine is torn down.
    ///
    /// Ordering matters and is easy to get wrong later: this runs while
    /// everything the app might hold is still alive. Layers must detach before
    /// the render device goes away, or GPU resources outlive their device --
    /// the loop is structured so that stays true when T0017 and T0025 land.
    virtual void onShutdown() {}

private:
    ApplicationConfig config_;
    Clock clock_;
    bool running_ = false;
    int exitCode_ = 0;
};

/// Defined by each application, not by the engine.
///
/// Returns the concrete `Application`. `argc`/`argv` are passed through so an
/// app can parse its own command line without the engine imposing a scheme.
std::unique_ptr<Application> createApplication(int argc, char** argv);

} // namespace hp
