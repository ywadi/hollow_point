// The application and its frame loop (T0014).
//
// Bucket: integration -- it links the engine shared library and runs real
// loops. Deterministic despite being a loop, because `exitAfterFrames` bounds
// it: a test that ran until a window closed would need a window, and one that
// ran until a timer expired would be flaky.

#include <doctest/doctest.h>

#include <hp/Application.hpp>

#include <vector>

namespace {

/// Records the order and count of every hook the loop calls.
class RecordingApp final : public hp::Application {
public:
    explicit RecordingApp(std::uint64_t frames) : hp::Application(makeConfig(frames)) {}

    std::vector<std::string> calls;
    int updates = 0;
    int renders = 0;
    double lastDelta = -1.0;

private:
    static hp::ApplicationConfig makeConfig(std::uint64_t frames) {
        hp::ApplicationConfig config;
        config.name = "test";
        config.exitAfterFrames = frames;
        return config;
    }

    void onStartup() override { calls.emplace_back("startup"); }

    void onUpdate(double delta) override {
        if (updates == 0) {
            calls.emplace_back("update");
        }
        ++updates;
        lastDelta = delta;
    }

    void onRender() override {
        if (renders == 0) {
            calls.emplace_back("render");
        }
        ++renders;
    }

    void onShutdown() override { calls.emplace_back("shutdown"); }
};

} // namespace

TEST_CASE("the loop runs the requested number of frames and exits cleanly") {
    RecordingApp app(5);
    CHECK(app.run() == 0);
    CHECK(app.frame() == 5);
    CHECK(app.updates == 5);
    CHECK(app.renders == 5);
}

TEST_CASE("hooks are called in the documented order") {
    // startup once, then update before render each frame, then shutdown once.
    // Shutdown running last matters more than it looks: it is where layers
    // detach, and detaching after the render device is gone leaves GPU
    // resources outliving their device (T0025).
    RecordingApp app(2);
    app.run();
    REQUIRE(app.calls.size() == 4);
    CHECK(app.calls[0] == "startup");
    CHECK(app.calls[1] == "update");
    CHECK(app.calls[2] == "render");
    CHECK(app.calls[3] == "shutdown");
}

TEST_CASE("delta time is available to the loop and is sane") {
    RecordingApp app(3);
    app.run();
    CHECK(app.lastDelta >= 0.0);
    CHECK(app.lastDelta <= app.clock().maxDelta());
}

TEST_CASE("requestExit stops the loop and sets the exit code") {
    class ExitingApp final : public hp::Application {
    public:
        ExitingApp() : hp::Application(makeConfig()) {}

        int seen = 0;

    private:
        static hp::ApplicationConfig makeConfig() {
            hp::ApplicationConfig config;
            // Deliberately larger than the frame we exit on, so the test proves
            // requestExit stopped the loop rather than the budget doing it.
            config.exitAfterFrames = 100;
            return config;
        }

        void onUpdate(double) override {
            ++seen;
            if (seen == 4) {
                requestExit(7);
            }
        }
    };

    ExitingApp app;
    CHECK(app.run() == 7);
    CHECK(app.seen == 4);
    CHECK(app.frame() == 4);
}

TEST_CASE("requestExit finishes the current frame rather than cutting it short") {
    // A hook must be able to ask to stop without the loop disappearing
    // underneath it -- render still runs for the frame update asked to end.
    class HalfFrameApp final : public hp::Application {
    public:
        HalfFrameApp() : hp::Application(makeConfig()) {}

        int updates = 0;
        int renders = 0;

    private:
        static hp::ApplicationConfig makeConfig() {
            hp::ApplicationConfig config;
            config.exitAfterFrames = 100;
            return config;
        }

        void onUpdate(double) override {
            ++updates;
            requestExit();
        }

        void onRender() override { ++renders; }
    };

    HalfFrameApp app;
    app.run();
    CHECK(app.updates == 1);
    CHECK(app.renders == 1);
}

TEST_CASE("an application can be run with no frame budget if it exits itself") {
    // The shape every real app will have once there is a window to close.
    class SelfStopping final : public hp::Application {
    public:
        SelfStopping() : hp::Application(hp::ApplicationConfig{}) {}

    private:
        void onUpdate(double) override {
            if (frame() >= 2) {
                requestExit();
            }
        }
    };

    SelfStopping app;
    CHECK(app.config().exitAfterFrames.has_value() == false);
    CHECK(app.run() == 0);
    CHECK(app.frame() == 2);
}
