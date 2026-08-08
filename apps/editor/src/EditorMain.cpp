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

#include <hp/CameraSystem.hpp>
#include <hp/EntryPoint.hpp>

#include "EditorCamera.hpp"
#include "EditorLayer.hpp"

#include <algorithm>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
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

/// Reads the model to open from the command line (T0172.4).
///
/// `hp_editor <file.glb>` — a positional argument rather than a drop target,
/// because it is the smaller of the two and it is the one that composes: it
/// works over ssh, from a script, and from a test, and a file manager's
/// "open with" hands a path to argv anyway. A drop target is additive later
/// and needs T0032's panel framework to have somewhere to drop *onto*.
///
/// @param argc argument count.
/// @param argv arguments.
/// @returns the path, or an empty string when none was given. Anything starting
///          with `-` is a flag and is skipped.
std::string modelFromArgs(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (!arg.empty() && arg.front() != '-') {
            return arg;
        }
    }
    return {};
}

/// Reads `--frames=N`, which quits after N frames.
///
/// **Exists so this application can be run without being left running.** A GUI
/// on someone's desktop that nobody closed is the failure mode; a bounded run is
/// how a change here gets checked at all, by a person or by a script, and
/// `ApplicationConfig::exitAfterFrames` was already the mechanism with no way to
/// reach it from the command line.
///
/// @param argc argument count.
/// @param argv arguments.
/// @returns the frame count, or nothing to run until the window closes.
std::optional<std::uint64_t> framesFromArgs(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("--frames=", 0) != 0) {
            continue;
        }
        try {
            const unsigned long long value = std::stoull(arg.substr(std::strlen("--frames=")));
            return static_cast<std::uint64_t>(value);
        } catch (const std::exception&) {
            HP_LOG_WARN(kLog, "unreadable {}; running until the window closes", arg);
        }
    }
    return std::nullopt;
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

    /// @returns the viewport camera, so startup can frame what it opened.
    hped::EditorCamera& camera() { return camera_; }

    /// Renders at the size a viewport panel asked for, rather than the window's
    /// (T0033).
    /// @param width requested width in pixels; zero or less means "unchanged".
    /// @param height requested height in pixels.
    /// @returns nothing.
    void setRequestedSize(int width, int height) {
        requestedWidth_ = width;
        requestedHeight_ = height;
    }

    /// Whether the camera should act on input this frame.
    ///
    /// **False whenever the pointer is not over the viewport panel.** This is
    /// the gap T0172 recorded: while the viewport was the whole window the
    /// camera could read input unconditionally, and the moment a panel exists
    /// that becomes "dragging a slider flies the view".
    /// @param enabled whether to drive the camera.
    /// @returns nothing.
    void setCameraInputEnabled(bool enabled) { cameraInput_ = enabled; }

    /// Whether to blit the scene straight to the swap chain.
    ///
    /// **Off once a viewport panel draws it**, or the scene appears twice — once
    /// full-window from the engine's dev present path and once inside the panel
    /// — with the UI over the top of the first. The blit was always labelled a
    /// throwaway for exactly this moment (T0028).
    /// @param present whether to keep the dev present path.
    /// @returns nothing.
    void setPresentToScreen(bool present) { presentToScreen_ = present; }

    /// Tells the camera what a re-frame (the F key) should recentre on.
    /// @param centre the subject's centre in world space.
    /// @param radius the subject's bounding radius.
    /// @returns nothing.
    void setFrameSubject(const hp::float3& centre, float radius) {
        subjectCentre_ = centre;
        subjectRadius_ = radius;
    }

    /// **The camera writes its transform at phase 8, not phase 4**, and that is
    /// the whole reason this is `onLateUpdate` (T0100). Phase 9 propagates what
    /// late update moved, so the frame rendered immediately afterwards sees this
    /// camera's position rather than the previous one's — the "followers read
    /// final transforms" rule in `hp/Layer.hpp`, which the editor camera obeys
    /// for exactly the reason stated there: doing it in `onUpdate` renders one
    /// frame stale, which profiles as nothing and looks like judder.
    void onLateUpdate(double deltaSeconds) override {
        if (input_.takeFrameRequest() && subjectRadius_ > 0.0F) {
            camera_.frame(subjectCentre_, subjectRadius_, framingFov());
        }
        if (cameraInput_) {
            camera_.update(input_, static_cast<float>(deltaSeconds));
        }
        // Ended every frame regardless, so a wheel notch or a key edge that
        // arrived while the pointer was over a panel is discarded rather than
        // being applied later, all at once, when it returns to the viewport.
        input_.endFrame();

        // **Drive the scene's own camera entity rather than an editor-only view
        // path.** `SceneView::render` resolves a camera out of the scene
        // (T0081.2), so an editor-only camera would mean teaching the renderer
        // about the editor — the coupling the whole app/engine split exists to
        // avoid. T0172's notes record the obligation this leaves behind:
        // whatever saves a scene must not write this entity.
        const std::optional<hp::Entity> resolved = hp::resolveCamera(scene_, 0);
        if (!resolved) {
            return;
        }
        scene_.setLocalTransform(*resolved, camera_.transform());
    }

    void onEvent(hp::Event& event) override {
        // Not consumed: the editor camera is the bottom of the stack, and
        // T0032's panel framework will sit above it and take what it needs
        // first.
        input_.onEvent(event);
    }

    void onRender() override {
        if (!render_.ready()) {
            return;
        }
        // The viewport panel's size when there is one, the swap chain's when
        // there is not. Both are in pixels.
        const int width = requestedWidth_ > 0 ? requestedWidth_ : render_.swapChainWidth();
        const int height = requestedHeight_ > 0 ? requestedHeight_ : render_.swapChainHeight();

        if (!view_.valid()) {
            if (!view_.create(render_.device(), render_.context(), width, height)) {
                // Said why already. Do not retry every frame -- a device that
                // cannot make a render target will not start being able to.
                failed_ = true;
                return;
            }
        }
        view_.resize(width, height);

        hp::SceneViewStats stats;
        // The application clock's scaled elapsed time (T0159.5) — what makes
        // `HpSurfaceInput::Time` non-zero in the editor, so time-driven
        // shaders animate here and not only in tests.
        Diligent::ITextureView* colour = view_.render(render_.context(), scene_, assets_, 0,
                                                      &stats, app_.clock().elapsed());
        if (colour == nullptr) {
            render_.setPresentSource(nullptr);
            return;
        }

        render_.setPresentSource(presentToScreen_ ? view_.colourTexture() : nullptr);

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
    /// @returns the vertical field of view to frame against — the scene
    ///          camera's own lens where there is one, so framing agrees with
    ///          what will actually be rendered rather than with a guess.
    float framingFov() {
        std::optional<hp::Entity> resolved = hp::resolveCamera(scene_, 0);
        if (resolved && resolved->has<hp::Camera>()) {
            return resolved->get<hp::Camera>().verticalFov;
        }
        return hp::Camera{}.verticalFov;
    }

    hp::Application& app_;
    hp::RenderLayer& render_;
    hp::Scene scene_;
    hp::AssetPool assets_;
    hp::SceneView view_;
    hped::EditorInputController input_;
    hped::EditorCamera camera_;
    int requestedWidth_ = 0;
    int requestedHeight_ = 0;
    bool cameraInput_ = true;
    bool presentToScreen_ = true;
    hp::float3 subjectCentre_{0.0F, 0.0F, 0.0F};
    float subjectRadius_ = 0.0F;
    bool failed_ = false;
};

class Editor final : public hp::Application {
public:
    /// @param backend the render backend the command line asked for.
    /// @param modelPath a model to open, or empty to show whatever a gameplay
    ///        module built.
    /// @param frames quit after this many frames, or nothing to run until the
    ///        window closes.
    Editor(hp::RenderBackend backend, std::string modelPath,
           std::optional<std::uint64_t> frames)
        : hp::Application(makeConfig(frames)), backend_(backend),
          modelPath_(std::move(modelPath)) {}

private:
    std::string modelPath_;

    hp::RenderConfig renderConfig() const {
        hp::RenderConfig config;
        config.backend = backend_;
        // The editor is the reason `RenderConfig::ui` exists (T0032.2). It is
        // off by default because a shipped game that never draws a debug panel
        // should not pay for a context or for a platform backend that eats the
        // mouse — see the field's own note.
        config.ui = true;
        return config;
    }

    hp::RenderBackend backend_ = hp::RenderBackend::Default;

    static hp::ApplicationConfig makeConfig(std::optional<std::uint64_t> frames) {
        hp::ApplicationConfig config;
        config.exitAfterFrames = frames;
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

            // **Between the scene and the render layer, and both sides matter**
            // (T0032.1). Layers render in push order, so this draws after the
            // scene has published its frame — which is what lets the viewport
            // panel show *this* frame rather than the last one — and before the
            // render layer, which submits the UI's draw data between the present
            // blit and `Present`. Events run the other way, top-down, so the
            // editor sees input before the scene does, which is the correct
            // order for a UI over a world.
            editor_ = static_cast<hped::EditorLayer*>(
                layers().push(std::make_unique<hped::EditorLayer>(*this, render)));

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

            // **A model named on the command line means the sample gameplay
            // modules stay unloaded**, and that is the honest reading of the
            // request: the owner asked to look at *this* file, and `rockcube`
            // builds a scene of its own that would sit in front of it.
            // `hp::Scene` has no `clear()`, and adding one to undo a load this
            // app chose to perform would be the wrong end of the problem.
            // T0024's project decides which modules an app hosts; until it
            // exists, "not these ones" is a decision this app is allowed to
            // make, and it says so out loud.
            //
            // **After the device exists, and that is the change T0157 forced.**
            // The modules used to load first, which was fine while the only one
            // registered reflected types. A module that loads its own content
            // needs a device to load it *onto*, and the device does not exist
            // until the render layer is pushed — so loading a module before
            // that point hands it a null device and the sample renders nothing.
            bool opened = false;
            if (modelPath_.empty()) {
                loadGameplayModules();
            } else {
                HP_LOG_INFO(kLog,
                            "opening {}; the sample gameplay modules stay unloaded, so "
                            "nothing else puts content in the scene",
                            modelPath_);
                opened = openModel(render, modelPath_);
            }

            // The throwaway quad, now a **fallback**. A gameplay module that
            // built a scene is the interesting case and this must not draw a
            // stray quad through the middle of it; a build with no sample
            // module still wants something on screen rather than a flat clear
            // colour. T0033's viewport panel replaces both.
            if (!opened && scene_->scene().size() == 0) {
                populateDemoScene(render);
            }

            // Only when we did not frame something ourselves: `openModel` has
            // already aimed the camera at what it loaded, and adopting the
            // camera entity it just created would throw that away.
            if (!opened) {
                adoptSceneCamera();
            }

            reportCamera();

            if (editor_ != nullptr && scene_ != nullptr) {
                editor_->setSubject(&scene_->scene(), &scene_->assets(), &scene_->camera());
                // The viewport panel draws the scene now, so the engine's
                // full-window dev blit stops (T0028 always called it a
                // throwaway for this moment).
                scene_->setPresentToScreen(false);
            }
        }
    }

    /// Carries what the panels asked for back to the scene layer.
    ///
    /// **After the layers' own late update, and that ordering is the point.**
    /// The panels ran during the previous frame's render, so what they asked for
    /// is read here and applied to the frame about to be drawn — one frame of
    /// latency on a resize, which is invisible, against a half-resized frame if
    /// it were applied mid-render.
    /// @param deltaSeconds the scaled frame delta.
    /// @returns nothing.
    void onLateUpdate(double deltaSeconds) override {
        (void)deltaSeconds;
        if (editor_ == nullptr || scene_ == nullptr) {
            return;
        }
        const hped::EditorContext& ui = editor_->context();
        scene_->setRequestedSize(ui.requestedWidth, ui.requestedHeight);
        scene_->setCameraInputEnabled(ui.viewportFocused);
    }

    /// Says where the viewport camera is and which way it faces, once, at
    /// startup.
    ///
    /// **This is the check a person cannot make by looking**, which is the whole
    /// difficulty of a camera: a mirrored basis renders as backwards geometry or
    /// an inverted normal map, never as an error, and that misdiagnosis has
    /// already happened once here over winding. So the determinant is asserted
    /// rather than assumed, and the forward vector is printed so a bounded run
    /// (`--frames=N`) answers "is it pointing at the content" without a screen.
    /// @returns nothing.
    void reportCamera() {
        if (scene_ == nullptr) {
            return;
        }
        const hp::Transform view = scene_->camera().transform();
        const hp::float3 forward = scene_->camera().forward();
        HP_LOG_INFO(kLog,
                    "viewport camera at ({:.3f}, {:.3f}, {:.3f}) looking "
                    "({:.3f}, {:.3f}, {:.3f}) -- right-drag to look, WASD/QE to fly, "
                    "Alt+left-drag to orbit, middle-drag to pan, wheel to zoom, F to frame",
                    view.position.x, view.position.y, view.position.z, forward.x, forward.y,
                    forward.z);
        if (!hped::isProperRotation(view.rotation)) {
            HP_LOG_ERROR(kLog,
                         "the viewport camera's basis is mirrored -- T0165 forbids this, and "
                         "it will render as backwards geometry rather than as an error");
        }
    }

    /// Starts the viewport camera where the scene's camera already is.
    ///
    /// Without this, opening the editor on a gameplay module's scene snaps the
    /// view somewhere else on the first frame — the module framed its content
    /// deliberately and the editor would throw that away before anyone saw it.
    /// @returns nothing.
    void adoptSceneCamera() {
        if (scene_ == nullptr) {
            return;
        }
        std::optional<hp::Entity> resolved = hp::resolveCamera(scene_->scene(), 0);
        if (!resolved || !resolved->valid()) {
            HP_LOG_INFO(kLog, "no camera in the scene; the viewport starts at the origin");
            return;
        }
        scene_->scene().propagateTransforms();
        const hp::float4x4& world = scene_->scene().worldTransform(*resolved);

        // The camera looks down its own **−Z** (T0165), and `world` is a
        // row-vector local-to-world matrix, so forward is the negated third row.
        scene_->camera().setPosition(hp::float3{world._41, world._42, world._43});
        scene_->camera().lookAlong(hp::float3{-world._31, -world._32, -world._33});
    }

    /// Opens a glTF/GLB from the host filesystem and builds a scene around it.
    ///
    /// **Through the VFS like everything else** (**D13**): the file's directory
    /// is mounted and the model is loaded by its name inside that mount, so the
    /// loader's callbacks for `.bin` buffers and images resolve exactly as they
    /// would inside a pack. Nothing here reads the host filesystem directly, and
    /// that is what makes "open a loose file" and "open a file in a pack" the
    /// same code path rather than two.
    ///
    /// @param render the render layer owning the device.
    /// @param path the host path given on the command line.
    /// @returns whether a model was loaded and put in the scene.
    bool openModel(hp::RenderLayer& render, const std::string& path) {
        if (scene_ == nullptr || !render.ready()) {
            return false;
        }
        std::error_code ec;
        const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
        if (ec || !std::filesystem::exists(absolute)) {
            HP_LOG_ERROR(kLog, "no such model: {}", path);
            return false;
        }
        const std::filesystem::path directory = absolute.parent_path();
        const std::string file = absolute.filename().string();

        if (!hp::Vfs::init(nullptr) || !hp::Vfs::mount(directory.string())) {
            HP_LOG_ERROR(kLog, "could not mount {}", directory.string());
            return false;
        }
        // Mount, then load the cooked shaders for what was mounted — the pattern
        // `populateDemoScene` documents (T0142.7). There is no mount
        // notification to hang it on.
        (void)hp::loadCookedShaders();

        auto mesh = hp::loadMesh(render.device(), render.context(), file);
        if (!mesh || !mesh->valid()) {
            HP_LOG_ERROR(kLog, "{} did not load as a model", file);
            return false;
        }

        hp::Scene& scene = scene_->scene();
        const hp::Guid meshGuid = hp::Guid::generate();
        scene_->assets().store<hp::MeshAsset>(meshGuid, mesh);

        hp::Entity cameraEntity = scene.create("Camera");
        cameraEntity.add<hp::Camera>(hp::Camera{});

        hp::Entity subject = scene.create(absolute.stem().string());
        hp::MeshRenderer renderer;
        renderer.mesh = meshGuid;
        subject.add<hp::MeshRenderer>(renderer);

        // A key light off-axis, for `populateDemoScene`'s reason: a head-on
        // light makes a curved surface and a flat one look the same, so a
        // frontal key would hide exactly what someone opened the model to see.
        hp::Entity sun = scene.create("Sun");
        hp::Light light;
        light.type = hp::LightType::Directional;
        light.intensity = 3.0F;
        sun.add<hp::Light>(light);
        hp::Transform sunPlacement;
        sunPlacement.rotation =
            hp::Quaternion::RotationFromAxisAngle(hp::float3{0.0F, 1.0F, 0.0F}, 0.6F)
            * hp::Quaternion::RotationFromAxisAngle(hp::float3{1.0F, 0.0F, 0.0F}, 0.45F);
        scene.setLocalTransform(sun, sunPlacement);
        scene.propagateTransforms();

        // Frame it. **Bounds come from the engine, not from here** — computing
        // them in the app would mean linking `Diligent-AssetLoader` a second
        // time into a process that already has it (see `MeshAsset::boundingSphere`).
        const std::optional<hp::BoundingSphere> bounds = mesh->boundingSphere();
        if (bounds) {
            const hp::float3 centre{bounds->centreX, bounds->centreY, bounds->centreZ};
            scene_->setFrameSubject(centre, bounds->radius);
            scene_->camera().frame(centre, bounds->radius, hp::Camera{}.verticalFov);

            // **The clip planes have to follow the subject, and the default
            // ones do not.** `hp::Camera` defaults to 0.1 and 1000, which is a
            // sensible *game* lens and wrong for an asset viewer: the Aston
            // Martin is authored in centimetres, so its bounding radius is 566
            // and the framing distance 1472 — the whole car sits beyond the far
            // plane and the viewport renders an empty frame with nothing
            // anywhere reporting a problem. Sizing them off the subject is what
            // makes "open any file" mean any file rather than any file roughly
            // one unit across.
            //
            // A wide near/far ratio is cheap here specifically because depth is
            // **reverse-Z** (`hp/DepthConvention.hpp`): the precision that a
            // conventional depth buffer would lose across four orders of
            // magnitude is exactly what reverse-Z keeps.
            hp::Camera& lens = cameraEntity.get<hp::Camera>();
            const float distance = scene_->camera().distance();
            lens.nearPlane = std::max(bounds->radius * 1e-3F, 1e-3F);
            lens.farPlane = std::max((distance + bounds->radius) * 4.0F, 100.0F);

            HP_LOG_INFO(kLog,
                        "opened {} -- {} mesh(es), {} material(s); radius {:.3f} about "
                        "({:.3f}, {:.3f}, {:.3f})",
                        file, mesh->meshCount(), mesh->materialCount(), bounds->radius,
                        centre.x, centre.y, centre.z);
            HP_LOG_INFO(kLog, "clip planes sized to the subject: near {:.4f}, far {:.1f}",
                        lens.nearPlane, lens.farPlane);
        } else {
            HP_LOG_WARN(kLog, "opened {} but it has no geometry to frame", file);
        }
        return true;
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
        //
        // **At z = -4 with a +Z normal since T0165**: the camera looks down its
        // own -Z (`hp::kRightHandedCameraSpace`), so "in front" is negative Z
        // and "toward the camera" is +Z.
        //
        // **The winding was wrong before this and is right now, by accident of
        // the mirror.** `{0, 1, 2, 0, 2, 3}` over BL, BR, TR, TL winds toward
        // +Z; against the old -Z normal that was the pre-T0152 inconsistency,
        // surviving only because the demo material is double-sided. Against a
        // +Z normal it is correct, and `cross(v1 - v0, v2 - v0)` is the check:
        // `(3,0,0) x (3,3,0) = (0,0,9)`.
        const float vertices[] = {
            -1.5F, -1.5F, -4.0F, 0.0F, 0.0F, 1.0F,
             1.5F, -1.5F, -4.0F, 0.0F, 0.0F, 1.0F,
             1.5F,  1.5F, -4.0F, 0.0F, 0.0F, 1.0F,
            -1.5F,  1.5F, -4.0F, 0.0F, 0.0F, 1.0F,
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
                 << R"("count":4,"type":"VEC3","min":[-1.5,-1.5,-4.0],"max":[1.5,1.5,-4.0]},)"
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

        // **Angled, and that is not decoration.** An identity transform points
        // a light straight down -Z, which since T0165 is the same way the
        // camera looks — so the lamp is now *co*-parallel with the view rather
        // than anti-parallel, and the pathological case this comment used to
        // describe (`normalize(L + V)` collapsing to zero, a dark radial smudge
        // that reads as a renderer bug) can no longer happen from an identity
        // transform. What replaces it is a flat frontal key: correct, and
        // completely uninformative about whether shading works at all, because
        // a head-on light makes a curved surface and a flat one look the same.
        // The lamp is still rotated off-axis, now to show the surface being
        // shaded rather than to avoid a degenerate half-vector.
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
    hped::EditorLayer* editor_ = nullptr;
};

} // namespace

std::unique_ptr<hp::Application> hp::createApplication(int argc, char** argv) {

    // Before constructing the application, not in onStartup(): the engine logs
    // as it starts up, and a sink installed later misses those lines. Logging
    // that only begins once the thing being logged is already running is the
    // least useful kind.
    hp::logAddConsoleSink();
    return std::make_unique<Editor>(backendFromArgs(argc, argv), modelFromArgs(argc, argv),
                                    framesFromArgs(argc, argv));
}
