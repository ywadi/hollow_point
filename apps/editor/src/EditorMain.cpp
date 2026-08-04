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
#include <hp/ModuleHost.hpp>
#include <hp/Paths.hpp>
#include <hp/Render.hpp>

#include <hp/EntryPoint.hpp>

#include <filesystem>
#include <memory>
#include <string>

namespace {

const hp::LogCategory kLog("editor");

#if defined(_WIN32)
#define HP_PATH_SEP "\\"
#else
#define HP_PATH_SEP "/"
#endif

class Editor final : public hp::Application {
public:
    Editor() : hp::Application(makeConfig()) {}

private:
    static hp::ApplicationConfig makeConfig() {
        hp::ApplicationConfig config;
        config.name = "HollowPoint Editor";
        config.window.title = "HollowPoint Editor";
        config.window.width = 1280;
        config.window.height = 720;
        return config;
    }

    void onStartup() override {
        hp::engineRegisterConsumer("editor");
        HP_LOG_INFO(kLog, "engine {}, {} instance(s), {} consumer(s)", hp::engineVersion(),
                    hp::engineInstanceCount(), hp::engineConsumerCount());

        // T0048: the module the editor and the runtime both host. Until
        // this existed, 'loaded at runtime by editor and runtime' was true
        // of the engine's capability and false of the binaries.
        loadSampleModule();

        // The render layer owns the device (T0025). The editor runs until its
        // window is closed, so this is where a device is actually visible.
        if (window() != nullptr) {
            auto* render = static_cast<hp::RenderLayer*>(
                layers().push(std::make_unique<hp::RenderLayer>(*window())));
            // Deliberately not black: a black window and a broken window look
            // identical, and "did it clear?" is the only question this layer
            // can currently answer.
            render->setClearColour(0.16F, 0.22F, 0.34F, 1.0F);
        }
    }

    /// Loads the sample gameplay module, if it is where one of the layouts puts
    /// it.
    ///
    /// Absence is **not** an error. An app with no gameplay module is a
    /// legitimate state today and will stay legitimate for a shipped tool, so
    /// this logs what happened and carries on. A refusal — a stale build id, a
    /// library that is not a module — is different and is already reported by
    /// the loader with what to rebuild.
    void loadSampleModule() {
        const std::string dir = hp::executableDirectory();
        // The layouts this binary can find itself in, in order:
        //   beside the exe        -- dist on Windows, and any co-located export
        //   ../lib               -- dist on Linux, which stages shared objects there
        //   ../../samples/...    -- the build tree
        // Composed at run time rather than baked by CMake: a configure-time path
        // is a *host* path, which a Windows binary cannot open (a lesson the
        // boundary suite already paid for twice).
        const std::string candidates[] = {
            dir + HP_PATH_SEP + HP_SANDBOX_MODULE_NAME,
            dir + HP_PATH_SEP ".." HP_PATH_SEP "lib" HP_PATH_SEP + HP_SANDBOX_MODULE_NAME,
            dir +
                HP_PATH_SEP ".." HP_PATH_SEP ".." HP_PATH_SEP "samples" HP_PATH_SEP
                            "sandbox" HP_PATH_SEP +
                HP_SANDBOX_MODULE_NAME,
        };

        for (const std::string& candidate : candidates) {
            if (!std::filesystem::exists(candidate)) {
                continue;
            }
            const hp::ModuleLoadResult result = modules().load(candidate);
            if (result.ok) {
                return; // the loader already logged it
            }
            // Found and refused: report and stop. Trying the next candidate
            // would silently run an older copy of the same module, which is the
            // confusion the build id exists to prevent.
            HP_LOG_ERROR(kLog, "gameplay module refused: {}", result.message);
            return;
        }
        HP_LOG_INFO(kLog, "no gameplay module found ({}); continuing without one",
                    HP_SANDBOX_MODULE_NAME);
    }

    void onUpdate(double deltaSeconds) override {
        HP_LOG_TRACE(kLog, "frame {} dt={:.6f}s", frame(), deltaSeconds);
    }

    void onResize(int width, int height) override {
        HP_LOG_INFO(kLog, "resized to {}x{}", width, height);
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
