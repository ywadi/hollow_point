// The runtime (T0013, T0014).
//
// The second engine consumer, and the one that ships. It exists this early
// precisely so the engine cannot quietly grow a dependency on editor concerns:
// a rule with only one consumer is not enforced by anything, and the export
// pipeline in Phase 8 is where that debt would otherwise come due.
//
// It also proves 14.6 -- the same entry-point pattern serves both programs.
#include <hp/Application.hpp>
#include <hp/Engine.hpp>
#include <hp/Log.hpp>

#include <hp/EntryPoint.hpp>

#include <memory>

namespace {

const hp::LogCategory kLog("runtime");

class Runtime final : public hp::Application {
public:
    Runtime() : hp::Application(makeConfig()) {}

private:
    static hp::ApplicationConfig makeConfig() {
        hp::ApplicationConfig config;
        config.name = "HollowPoint Runtime";
        // Temporary: there is no window to close yet (T0015), so without a
        // frame budget the loop would never end.
        config.exitAfterFrames = 3;
        return config;
    }

    void onStartup() override {
        hp::engineRegisterConsumer("runtime");
        HP_LOG_INFO(kLog, "engine {}, {} instance(s), {} consumer(s)", hp::engineVersion(),
                    hp::engineInstanceCount(), hp::engineConsumerCount());
    }

    void onUpdate(double deltaSeconds) override {
        HP_LOG_DEBUG(kLog, "frame {} dt={:.6f}s", frame(), deltaSeconds);
    }

    void onShutdown() override { HP_LOG_INFO(kLog, "runtime shutting down"); }
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
    return std::make_unique<Runtime>();
}
