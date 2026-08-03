// The editor (T0013, T0014).
//
// A consumer of the engine, never part of it: on export the editor disappears
// and the runtime ships, so anything the game needs at run time lives in the
// engine library rather than here.
//
// The editor is also a *module host* -- it loads the gameplay module so the
// inspector can show game-defined types (T0032, T0035). Not built yet.
#include <hp/Application.hpp>
#include <hp/Engine.hpp>
#include <hp/Log.hpp>

#include <hp/EntryPoint.hpp>

#include <memory>

namespace {

const hp::LogCategory kLog("editor");

class Editor final : public hp::Application {
public:
    Editor() : hp::Application(makeConfig()) {}

private:
    static hp::ApplicationConfig makeConfig() {
        hp::ApplicationConfig config;
        config.name = "HollowPoint Editor";
        // Temporary: there is no window to close yet (T0015), so without a
        // frame budget the loop would never end.
        config.exitAfterFrames = 3;
        return config;
    }

    void onStartup() override {
        hp::engineRegisterConsumer("editor");
        HP_LOG_INFO(kLog, "engine {}, {} instance(s), {} consumer(s)", hp::engineVersion(),
                    hp::engineInstanceCount(), hp::engineConsumerCount());
    }

    void onUpdate(double deltaSeconds) override {
        HP_LOG_DEBUG(kLog, "frame {} dt={:.6f}s", frame(), deltaSeconds);
    }

    void onShutdown() override { HP_LOG_INFO(kLog, "editor shutting down"); }
};

} // namespace

std::unique_ptr<hp::Application> hp::createApplication(int argc, char** argv) {
    (void)argc;
    (void)argv;

    // Before constructing the application, not in onStartup(): the engine logs
    // as it starts up, and a sink installed later misses those lines. Logging
    // that only begins once the thing being logged is already running is the
    // least useful kind.
    hp::logAddConsoleSink();
    return std::make_unique<Editor>();
}
