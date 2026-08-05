// The editor (T0013, T0014).
//
// A consumer of the engine, never part of it: on export the editor disappears
// and the runtime ships, so anything the game needs at run time lives in the
// engine library rather than here.
//
// The editor is also a *module host* -- it loads the gameplay module so the
// inspector can show game-defined types (T0032, T0035). Not built yet.
#include <hp/Application.hpp>
#include <hp/Assets.hpp>
#include <hp/Event.hpp>
#include <hp/Scene.hpp>
#include <hp/SceneView.hpp>

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

/// Reads `--backend=vulkan|opengl` from the command line (25.2).
///
/// Parsed before the window is created, and that ordering is forced rather than
/// stylistic: an OpenGL context is an SDL *creation* flag, so the backend has to
/// be known before there is a window at all (see `WindowConfig::openGLContext`).
/// An unrecognised value is reported and ignored rather than being fatal --
/// a typo in a debugging flag should not stop the program starting.
hp::RenderBackend backendFromArgs(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("--backend=", 0) != 0) {
            continue;
        }
        const std::string value = arg.substr(std::string("--backend=").size());
        if (value == "vulkan" || value == "vk") {
            return hp::RenderBackend::Vulkan;
        }
        if (value == "opengl" || value == "gl") {
            return hp::RenderBackend::OpenGL;
        }
        HP_LOG_WARN(kLog, "unknown --backend={} (expected vulkan or opengl); using the default",
                    value);
    }
    return hp::RenderBackend::Default;
}

/// Renders the editor's scene into an offscreen target and publishes it.
///
/// **Pushed *before* the render layer, and that ordering is the whole trick.**
/// `LayerStack::render` runs layers in push order, and `hp::RenderLayer` clears,
/// blits and presents in one call — so anything that must appear on screen has
/// to have drawn before it runs. The device does not exist until the render
/// layer attaches, which happens at push time, so this creates its resources
/// lazily on the first frame rather than in `onAttach`.
///
/// It publishes a `FrameRenderedEvent` and *also* hands the texture to the
/// render layer's dev present path. Those are not redundant: the event is the
/// real interface that T0033's viewport panel will consume, and the blit is the
/// throwaway that makes Phase 4 visible before that panel exists.
class SceneLayer final : public hp::ILayer {
public:
    /// @param app the application, for dispatching the frame event.
    /// @param render the render layer that owns the device. Must outlive this.
    SceneLayer(hp::Application& app, hp::RenderLayer& render) : app_(app), render_(render) {}

    /// @returns the scene, so the editor can populate it.
    hp::Scene& scene() { return scene_; }

    void onRender() override {
        if (!render_.ready()) {
            return;
        }
        const int width = render_.swapChainWidth();
        const int height = render_.swapChainHeight();

        if (!view_.valid()) {
            if (!view_.create(render_.device(), render_.context(), width, height)) {
                // Said why already. Do not retry every frame -- a device that
                // cannot make a render target will not start being able to.
                failed_ = true;
                return;
            }
        }
        // Matching the swap chain exactly is what lets the dev blit be a plain
        // copy with no scaling (T0028's present path).
        view_.resize(width, height);

        hp::SceneViewStats stats;
        Diligent::ITextureView* colour = view_.render(render_.context(), scene_, assets_,
                                                      render_.clipSpace(), 0, &stats);
        if (colour == nullptr) {
            render_.setPresentSource(nullptr);
            return;
        }

        render_.setPresentSource(view_.colourTexture());

        // The real interface. Nothing listens yet -- T0033's viewport panel is
        // the first consumer -- but publishing from the start is what stops the
        // viewport being written against a renderer pointer later.
        hp::FrameRenderedEvent frame(colour, view_.width(), view_.height());
        app_.dispatch(frame);
    }

    void onDetach() override {
        render_.setPresentSource(nullptr);
        view_.release();
    }

    /// @returns a stable name for logs.
    const char* name() const { return "scene"; }

private:
    hp::Application& app_;
    hp::RenderLayer& render_;
    hp::Scene scene_;
    hp::AssetPool assets_;
    hp::SceneView view_;
    bool failed_ = false;
};

class Editor final : public hp::Application {
public:
    explicit Editor(hp::RenderBackend backend)
        : hp::Application(makeConfig(backend)), backend_(backend) {}

private:
    hp::RenderConfig renderConfig() const {
        hp::RenderConfig config;
        config.backend = backend_;
        return config;
    }

    hp::RenderBackend backend_ = hp::RenderBackend::Default;

    static hp::ApplicationConfig makeConfig(hp::RenderBackend backend) {
        hp::ApplicationConfig config;
        config.name = "HollowPoint Editor";
        config.window.title = "HollowPoint Editor";
        config.window.width = 1280;
        config.window.height = 720;
        // T0110: the editor does not render uncapped. Vsync alone is not the
        // answer -- it does nothing for a hidden or minimised window, and a
        // driver may ignore it -- so the cap is what actually stops an editor
        // flattening a laptop battery while nobody is looking at it. 60 while
        // focused is well above what editing needs; 10 in the background is
        // enough to stay responsive to a click that brings it back.
        config.frameRateCap = 60;
        config.backgroundFrameRateCap = 10;
        // The GL context must exist before the window does, so the
        // backend choice reaches all the way back to here.
        config.window.openGLContext = (backend == hp::RenderBackend::OpenGL);
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
            // **Order matters, and it is the opposite of the obvious one.**
            // `LayerStack::render` walks layers in push order, and
            // `hp::RenderLayer` clears, blits and presents in a single
            // `onRender`. So the scene must be pushed *first* to draw first --
            // pushed after, it would render into a frame already presented and
            // nothing would ever appear.
            //
            // The render layer is therefore constructed before it is pushed, so
            // the scene layer can hold a reference to it. Constructing does not
            // create a device; `push` attaches, and that is when the device
            // appears -- which is why the scene layer builds its targets lazily
            // on the first frame rather than in `onAttach`.
            auto renderOwned = std::make_unique<hp::RenderLayer>(*window(), renderConfig());
            hp::RenderLayer& render = *renderOwned;

            scene_ = static_cast<SceneLayer*>(
                layers().push(std::make_unique<SceneLayer>(*this, render)));
            layers().push(std::move(renderOwned));

            // Deliberately not black: a black window and a broken window look
            // identical, and "did it clear?" is the only question this layer
            // can currently answer.
            render.setClearColour(0.16F, 0.22F, 0.34F, 1.0F);
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

private:
    SceneLayer* scene_ = nullptr;
};

} // namespace

std::unique_ptr<hp::Application> hp::createApplication(int argc, char** argv) {

    // Before constructing the application, not in onStartup(): the engine logs
    // as it starts up, and a sink installed later misses those lines. Logging
    // that only begins once the thing being logged is already running is the
    // least useful kind.
    hp::logAddConsoleSink();
    return std::make_unique<Editor>(backendFromArgs(argc, argv));
}
