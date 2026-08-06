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
#include <hp/Light.hpp>
#include <hp/Render.hpp>
#include <hp/ShaderCook.hpp>
#include <hp/Vfs.hpp>

#include <hp/EntryPoint.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace {

const hp::LogCategory kLog("editor");

#if defined(_WIN32)
#define HP_PATH_SEP "\\"
#else
#define HP_PATH_SEP "/"
#endif

/// Reads `--backend=vulkan` from the command line (25.2).
///
/// Vulkan is the only backend (D29), so the flag pins the request for a bug
/// report rather than selecting anything. An unrecognised value is reported
/// and ignored rather than being fatal -- a typo in a debugging flag should
/// not stop the program starting.
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
        HP_LOG_WARN(kLog, "unknown --backend={} (expected vulkan); using the default", value);
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

    /// @returns the asset pool the scene's GUIDs resolve against.
    hp::AssetPool& assets() { return assets_; }

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
        Diligent::ITextureView* colour = view_.render(render_.context(), scene_, assets_, 0, &stats);
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
        : hp::Application(makeConfig()), backend_(backend) {}

private:
    hp::RenderConfig renderConfig() const {
        hp::RenderConfig config;
        config.backend = backend_;
        return config;
    }

    hp::RenderBackend backend_ = hp::RenderBackend::Default;

    static hp::ApplicationConfig makeConfig() {
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
        return config;
    }

    void onStartup() override {
        hp::engineRegisterConsumer("editor");
        HP_LOG_INFO(kLog, "engine {}, {} instance(s), {} consumer(s)", hp::engineVersion(),
                    hp::engineInstanceCount(), hp::engineConsumerCount());

        // The render layer owns the device (T0025). The editor runs until its
        // window is closed, so this is where a device is actually visible.
        if (window() == nullptr) {
            // Headless: no device and no scene, so a module gets neither. Load
            // them anyway — the boundary is still worth crossing, and a module
            // that only registers types works perfectly well here.
            loadGameplayModules();
            return;
        }
        {
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

            // **What this application is running, declared once** (T0062.11).
            // Two things read it: the frame's transform phases, and every
            // gameplay module at every entry point. Before this existed a
            // module was handed nothing at all, so no gameplay code of any
            // shape could be written — which is what T0157 found and what
            // T0062 records.
            hp::ModuleServices services;
            services.scene = &scene_->scene();
            services.assets = &scene_->assets();
            if (render.ready()) {
                services.device = render.device();
                services.deviceContext = render.context();
            }
            setServices(services);

            // **After the device exists, and that is the change T0157 forced.**
            // The modules used to load first, which was fine while the only one
            // registered reflected types. A module that loads its own content
            // needs a device to load it *onto*, and the device does not exist
            // until the render layer is pushed — so loading a module before
            // that point hands it a null device and the sample renders nothing.
            loadGameplayModules();

            // The throwaway quad, now a **fallback**. A gameplay module that
            // built a scene is the interesting case and this must not draw a
            // stray quad through the middle of it; a build with no sample
            // module still wants something on screen rather than a flat clear
            // colour. T0033's viewport panel replaces both.
            if (scene_->scene().size() == 0) {
                populateDemoScene(render);
            }
        }
    }

    /// Puts something in the scene so the window shows more than a clear colour.
    ///
    /// **A throwaway, and labelled as one**, exactly like the `SceneLayer` it
    /// fills: T0033's viewport panel replaces this, and T0042's runtime will
    /// load real content through a project (T0024). It exists because the
    /// renderer was complete end to end for a whole phase while showing nothing,
    /// which makes every rendering change unreviewable by eye.
    ///
    /// The mesh is **generated at startup into a temp directory** rather than
    /// committed, for two reasons: there is no content pipeline yet, and a
    /// binary asset in the repository would be the first one — a decision worth
    /// making deliberately on T0024, not as a side effect of wanting a picture.
    ///
    /// @param render the render layer owning the device.
    /// @returns nothing.
    void populateDemoScene(hp::RenderLayer& render) {
        if (scene_ == nullptr || !render.ready()) {
            return;
        }

        const std::filesystem::path root =
            std::filesystem::temp_directory_path() / "hp_editor_demo";
        std::error_code ec;
        std::filesystem::create_directories(root / "models", ec);

        // A quad facing the camera. Rough and non-metallic, because a smooth
        // metal surface reflects nothing without an environment map (T0087) and
        // would look exactly like a bug.
        const float vertices[] = {
            -1.5F, -1.5F, 4.0F, 0.0F, 0.0F, -1.0F,
             1.5F, -1.5F, 4.0F, 0.0F, 0.0F, -1.0F,
             1.5F,  1.5F, 4.0F, 0.0F, 0.0F, -1.0F,
            -1.5F,  1.5F, 4.0F, 0.0F, 0.0F, -1.0F,
        };
        const std::uint16_t indices[] = {0, 1, 2, 0, 2, 3};
        {
            std::ofstream bin(root / "models" / "quad.bin", std::ios::binary);
            bin.write(reinterpret_cast<const char*>(vertices), sizeof vertices);
            bin.write(reinterpret_cast<const char*>(indices), sizeof indices);
        }
        {
            std::ofstream gltf(root / "models" / "quad.gltf", std::ios::binary);
            gltf << R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],)"
                 << R"("nodes":[{"mesh":0}],"meshes":[{"primitives":[{)"
                 << R"("attributes":{"POSITION":0,"NORMAL":1},"indices":2,"material":0}]}],)"
                 << R"("materials":[{"doubleSided":true,"pbrMetallicRoughness":{)"
                 << R"("baseColorFactor":[0.85,0.35,0.25,1.0],"metallicFactor":0.0,)"
                 << R"("roughnessFactor":0.6}}],)"
                 << R"("buffers":[{"uri":"quad.bin","byteLength":108}],)"
                 << R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":96,"byteStride":24},)"
                 << R"({"buffer":0,"byteOffset":96,"byteLength":12}],)"
                 << R"("accessors":[{"bufferView":0,"byteOffset":0,"componentType":5126,)"
                 << R"("count":4,"type":"VEC3","min":[-1.5,-1.5,4.0],"max":[1.5,1.5,4.0]},)"
                 << R"({"bufferView":0,"byteOffset":12,"componentType":5126,"count":4,)"
                 << R"("type":"VEC3"},{"bufferView":1,"byteOffset":0,"componentType":5123,)"
                 << R"("count":6,"type":"SCALAR"}]})";
        }

        if (!hp::Vfs::init(nullptr) || !hp::Vfs::mount(root.string())) {
            HP_LOG_WARN(kLog, "could not mount the demo content; the viewport stays empty");
            return;
        }

        // **Whoever mounts content is who loads its cooked shaders** (T0142.7).
        // There is no mount notification to hang this on, and inventing one
        // would be T0058's job done badly -- so the pattern is "mount, then
        // load", and this is the live example of it. The editor always has a
        // compiler, so it normally finds nothing and carries on; a shipped
        // runtime (T0042) finds everything and is the case that matters.
        (void)hp::loadCookedShaders();

        auto mesh = hp::loadMesh(render.device(), render.context(), "models/quad.gltf");
        if (!mesh || !mesh->valid()) {
            HP_LOG_WARN(kLog, "demo mesh did not load; the viewport stays empty");
            return;
        }

        hp::Scene& scene = scene_->scene();
        const hp::Guid meshGuid = hp::Guid::generate();
        scene_->assets().store<hp::MeshAsset>(meshGuid, mesh);

        scene.create("Camera").add<hp::Camera>(hp::Camera{});

        hp::Entity quad = scene.create("Quad");
        hp::MeshRenderer renderer;
        renderer.mesh = meshGuid;
        quad.add<hp::MeshRenderer>(renderer);

        // **Angled, and that is not decoration.** An identity transform points a
        // light straight down -Z, exactly anti-parallel to the camera's view.
        // The half-vector `normalize(L + V)` then collapses to zero at the
        // centre of the quad and the specular term with it, producing a dark
        // radial smudge that looks like a renderer bug and is not — it is the
        // one lighting arrangement guaranteed to look wrong. Rotating the lamp
        // off-axis puts the highlight somewhere sensible and shows the surface
        // actually being shaded.
        hp::Entity sun = scene.create("Sun");
        hp::Light light;
        light.type = hp::LightType::Directional;
        light.intensity = 3.0F;
        sun.add<hp::Light>(light);

        hp::Transform sunPlacement;
        // Yaw ~35 degrees and pitch ~25 down, as a quaternion from Euler angles.
        sunPlacement.rotation = hp::Quaternion::RotationFromAxisAngle(
                                    hp::float3{0.0F, 1.0F, 0.0F}, 0.6F)
                                * hp::Quaternion::RotationFromAxisAngle(
                                    hp::float3{1.0F, 0.0F, 0.0F}, 0.45F);
        scene.setLocalTransform(sun, sunPlacement);

        scene.propagateTransforms();
        HP_LOG_INFO(kLog, "demo scene ready: a lit quad.");
    }

    /// Loads the sample gameplay modules this build knows about.
    ///
    /// **Two names in a list, and that is the honest shape of it today.** A
    /// project decides which modules an application hosts (T0024); until one
    /// exists, the alternative to naming them here is a hard-coded single
    /// module, which is what this was.
    void loadGameplayModules() {
        loadModule(HP_SANDBOX_MODULE_NAME, "sandbox");
        loadModule(HP_ROCKCUBE_MODULE_NAME, "rockcube");
    }

    /// Loads one gameplay module, if it is where one of the layouts puts it.
    ///
    /// Absence is **not** an error. An app with no gameplay module is a
    /// legitimate state today and will stay legitimate for a shipped tool, so
    /// this logs what happened and carries on. A refusal — a stale build id, a
    /// library that is not a module — is different and is already reported by
    /// the loader with what to rebuild.
    ///
    /// @param fileName the module's file name, as CMake reported it.
    /// @param sampleDir the directory under `samples/` the build writes it to.
    /// @returns nothing.
    void loadModule(const std::string& fileName, const std::string& sampleDir) {
        const std::string dir = hp::executableDirectory();
        // The layouts this binary can find itself in, in order:
        //   beside the exe        -- dist on Windows, and any co-located export
        //   ../lib               -- dist on Linux, which stages shared objects there
        //   ../../samples/...    -- the build tree
        // Composed at run time rather than baked by CMake: a configure-time path
        // is a *host* path, which a Windows binary cannot open (a lesson the
        // boundary suite already paid for twice).
        const std::string candidates[] = {
            dir + HP_PATH_SEP + fileName,
            dir + HP_PATH_SEP ".." HP_PATH_SEP "lib" HP_PATH_SEP + fileName,
            dir + HP_PATH_SEP ".." HP_PATH_SEP ".." HP_PATH_SEP "samples" HP_PATH_SEP + sampleDir +
                HP_PATH_SEP + fileName,
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
        HP_LOG_INFO(kLog, "no gameplay module found ({}); continuing without one", fileName);
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
