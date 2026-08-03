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
#include <hp/Event.hpp>
#include <hp/Layer.hpp>
#include <hp/Time.hpp>
#include <hp/Window.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace hp {

struct ApplicationConfig {
    std::string name = "HollowPoint";

    /// The window to open. Its title defaults to `name` when left empty.
    WindowConfig window;

    /// Run without a window. For tests and for any future headless tool -- an
    /// application that cannot run headless is one that cannot be tested
    /// without a display, which is how a test suite ends up needing a desktop.
    bool headless = false;

    /// Stop after this many frames.
    ///
    /// Now that there is a window, closing it is the normal exit condition and
    /// this is what it was always going to become: a test affordance, and the
    /// way a headless run terminates at all.
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
    ///
    /// @param exitCode value `run()` returns, and therefore the process exit
    ///        code. Defaults to 0.
    void requestExit(int exitCode = 0);

    /// Sends an event top-down through the layers, then to `onEvent` if nothing
    /// consumed it. Public so a test or a tool can inject one.
    ///
    /// @param event the event to deliver.
    void dispatch(Event& event);

    const ApplicationConfig& config() const { return config_; }

    /// The game clock. Scaled and pausable -- see T0057. The editor will own a
    /// second, unscaled clock when it needs one.
    Clock& clock() { return clock_; }

    const Clock& clock() const { return clock_; }

    std::uint64_t frame() const { return clock_.frame(); }

    /// Null in a headless run.
    Window* window() { return window_.get(); }

    const Window* window() const { return window_.get(); }

    /// The layer stack. Layers are where features live; `Application` stays
    /// thin on purpose (T0014).
    LayerStack& layers() { return layers_; }

    const LayerStack& layers() const { return layers_; }

protected:
    /// Called once, after the engine is up and before the first frame.
    virtual void onStartup() {}

    /// Called once per frame with the scaled delta in seconds.
    virtual void onUpdate(double deltaSeconds) { (void)deltaSeconds; }

    /// Called once per frame, after update.
    virtual void onRender() {}

    /// The window was resized. `width` and `height` are in *pixels*, which is
    /// what a swap chain is sized in -- on a scaled display they differ from
    /// the logical size.
    /// @param width new width in pixels.
    /// @param height new height in pixels.
    virtual void onResize(int width, int height) {
        (void)width;
        (void)height;
    }

    /// An event the layer stack did not consume.
    ///
    /// Layers see everything first; this is the fallback for an application
    /// that wants whatever nothing claimed.
    ///
    /// @param event the unconsumed event.
    virtual void onEvent(Event& event) { (void)event; }

    /// Called once, before the engine is torn down.
    ///
    /// Ordering matters and is easy to get wrong later: this runs while
    /// everything the app might hold is still alive. Layers must detach before
    /// the render device goes away, or GPU resources outlive their device --
    /// the loop is structured so that stays true when T0017 and T0025 land.
    virtual void onShutdown() {}

private:
    ApplicationConfig config_;
    std::unique_ptr<Window> window_;
    LayerStack layers_;
    Clock clock_;
    bool running_ = false;
    int exitCode_ = 0;
};

/// Defined by each application, not by the engine.
///
/// @param argc argument count, straight from `main`.
/// @param argv argument vector, straight from `main`. Passed through so an app
///        can parse its own command line without the engine imposing a scheme.
/// @returns the concrete application. Returning null makes the process exit
///        with code 1 without running a frame.
std::unique_ptr<Application> createApplication(int argc, char** argv);

} // namespace hp
