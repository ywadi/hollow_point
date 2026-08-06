// The scene renderer against a real device (T0028).
//
// Bucket: gpu. **The thing this exists to prove is that the pipeline states
// build at all**, because the whole reason `SceneRenderer` drives Diligent's
// `PBR_Renderer` rather than its `GLTF_PBR_Renderer` is that the latter
// hardcodes `COMPARISON_FUNC_LESS` and the engine needs
// `COMPARISON_FUNC_GREATER_EQUAL` (T0130). That substitution compiles either
// way; only a device says whether it works.
//
// Skips cleanly with no device, like the rest of this bucket -- and for the same
// reason `all` builds this and never runs it: the skip path handles a device the
// engine can refuse, not a driver that takes the process down.

#include <doctest/doctest.h>

#include <hp/Assets.hpp>
#include <hp/DrawSubmission.hpp>
#include <hp/FrameTargets.hpp>
#include <hp/Log.hpp>
#include <hp/Render.hpp>
#include <hp/Scene.hpp>
#include <hp/SceneRenderer.hpp>
#include <hp/SceneView.hpp>
#include <hp/Window.hpp>

#include <memory>
#include <string>

namespace {

struct Device {
    std::unique_ptr<hp::Window> window;
    std::unique_ptr<hp::RenderLayer> render;

    [[nodiscard]] bool ok() const { return render && render->ready(); }
};

Device bringUp(hp::RenderBackend backend) {
    static const bool sink = [] {
        hp::logAddConsoleSink();
        return true;
    }();
    (void)sink;

    hp::WindowConfig windowConfig;
    windowConfig.title = "hp scene renderer test";
    windowConfig.width = 320;
    windowConfig.height = 240;
    windowConfig.resizable = true;

    Device device;
    device.window = hp::Window::create(windowConfig);
    if (!device.window) {
        return device;
    }

    hp::RenderConfig renderConfig;
    renderConfig.backend = backend;
    renderConfig.vsync = false;
    device.render = std::make_unique<hp::RenderLayer>(*device.window, renderConfig);
    device.render->onAttach();
    return device;
}

void tearDown(Device& device) {
    if (device.render) {
        device.render->onDetach();
        device.render.reset();
    }
    device.window.reset();
}

void exerciseSceneRenderer(hp::RenderBackend backend, const char* backendName) {
    Device device = bringUp(backend);
    if (!device.ok()) {
        MESSAGE("no " << std::string(backendName)
                      << " device available -- skipping (this is expected on CI)");
        tearDown(device);
        return;
    }
    MESSAGE("device up on " << std::string(backendName));

    hp::SceneRenderer renderer;

    SUBCASE("shaders and pipeline states build with reverse-Z") {
        // **This is the measurement the ticket turned on.** `PBR_Renderer`
        // compiles its shaders and creates its pipeline states here, against the
        // depth comparison the engine requires. If reverse-Z were unacceptable
        // to the backend, or the substituted setup were wrong, it fails here
        // rather than producing a wrong image.
        const bool created = renderer.create(device.render->device(), device.render->context(),
                                             hp::TargetFormat::Colour, hp::TargetFormat::Depth);
        REQUIRE(created);
        CHECK(renderer.valid());
    }

    SUBCASE("an HDR colour target is equally acceptable") {
        // The formats are baked into the pipeline state, so the pair that T0096
        // will actually want must be known to work before it gets there.
        hp::SceneRenderer hdr;
        CHECK(hdr.create(device.render->device(), device.render->context(),
                         hp::TargetFormat::ColourHDR, hp::TargetFormat::Depth));
    }

    SUBCASE("release is idempotent and leaves the renderer invalid") {
        REQUIRE(renderer.create(device.render->device(), device.render->context(),
                                hp::TargetFormat::Colour, hp::TargetFormat::Depth));
        renderer.release();
        CHECK_FALSE(renderer.valid());
        renderer.release();
        CHECK_FALSE(renderer.valid());
    }

    SUBCASE("an empty draw list submits nothing and does not touch the device") {
        REQUIRE(renderer.create(device.render->device(), device.render->context(),
                                hp::TargetFormat::Colour, hp::TargetFormat::Depth));

        hp::AssetPool pool;
        hp::ResolvedView view;
        view.viewportWidth = 320;
        view.viewportHeight = 240;

        hp::DrawSubmitStats stats;
        const std::size_t drawn =
            renderer.render(device.render->context(), hp::DrawList{}, view, pool, hp::LightList{}, &stats);
        CHECK(drawn == 0);
        CHECK(stats.submitted == 0);
        CHECK(stats.missingMesh == 0);
    }

    SUBCASE("an item whose mesh is not in the pool is counted, not drawn, and not fatal") {
        // The no-placeholder decision, exercised: a missing *mesh* gets nothing
        // drawn and no substitute geometry, because inventing a cube would put
        // something in the world no artist authored. The marker belongs in
        // T0061's debug draw.
        REQUIRE(renderer.create(device.render->device(), device.render->context(),
                                hp::TargetFormat::Colour, hp::TargetFormat::Depth));

        hp::AssetPool pool;
        hp::ResolvedView view;
        view.viewportWidth = 320;
        view.viewportHeight = 240;

        hp::DrawList list;
        hp::DrawItem item;
        item.mesh = hp::Guid(0xABCDEF01);
        item.world = hp::float4x4::Identity();
        list.push_back(item);

        hp::DrawSubmitStats stats;
        const std::size_t drawn =
            renderer.render(device.render->context(), list, view, pool, hp::LightList{}, &stats);
        CHECK(drawn == 0);
        CHECK(stats.submitted == 0);
        CHECK(stats.missingMesh == 1);
    }

    SUBCASE("rendering through an uncreated renderer is a no-op rather than a crash") {
        // A renderer whose `create` failed -- no device, unsupported format --
        // must not take the frame down. The caller checks `valid()`, but the
        // guard exists because one caller eventually will not.
        hp::SceneRenderer empty;
        hp::AssetPool pool;
        hp::ResolvedView view;
        CHECK(empty.render(device.render->context(), hp::DrawList{}, view, pool) == 0);
    }

    renderer.release();
    tearDown(device);
}

} // namespace

TEST_CASE("the scene renderer builds and submits on the default backend"
          * doctest::test_suite("gpu")) {
    exerciseSceneRenderer(hp::RenderBackend::Default, "default");
}

// --- the offscreen target and the published frame (28.4, 28.5) ---------------

namespace {

void exerciseSceneView(hp::RenderBackend backend, const char* backendName) {
    Device device = bringUp(backend);
    if (!device.ok()) {
        MESSAGE("no " << std::string(backendName)
                      << " device available -- skipping (this is expected on CI)");
        tearDown(device);
        return;
    }

    hp::SceneView sceneView;
    REQUIRE(sceneView.create(device.render->device(), device.render->context(), 320, 240));
    REQUIRE(sceneView.valid());
    CHECK(sceneView.width() == 320);
    CHECK(sceneView.height() == 240);

    hp::AssetPool pool;

    SUBCASE("a scene with no camera renders nothing and says so, rather than crashing") {
        // One of the ticket's "Done when" clauses, and the one most likely to be
        // met with a crash: an empty scene is the normal state of a scene being
        // built, not an error.
        hp::Scene scene;
        scene.propagateTransforms();

        hp::SceneViewStats stats;
        Diligent::ITextureView* published =
            sceneView.render(device.render->context(), scene, pool, 0, &stats);

        CHECK_FALSE(stats.hadCamera);
        CHECK(stats.submitted == 0);
        // Still published: the frame was cleared, and showing the clear colour is
        // what distinguishes "no camera" from "the renderer froze".
        CHECK(published != nullptr);
    }

    SUBCASE("a camera with no drawable entities publishes a cleared frame") {
        hp::Scene scene;
        hp::Entity cameraEntity = scene.create("camera");
        cameraEntity.add<hp::Camera>(hp::Camera{});
        scene.propagateTransforms();

        hp::SceneViewStats stats;
        Diligent::ITextureView* published =
            sceneView.render(device.render->context(), scene, pool, 0, &stats);

        CHECK(stats.hadCamera);
        CHECK(stats.considered == 0);
        CHECK(stats.submitted == 0);
        CHECK(published != nullptr);
    }

    SUBCASE("an entity whose mesh is not loaded is counted, and the frame still publishes") {
        hp::Scene scene;
        hp::Entity cameraEntity = scene.create("camera");
        cameraEntity.add<hp::Camera>(hp::Camera{});

        hp::Entity drawable = scene.create("mesh");
        hp::MeshRenderer renderer;
        renderer.mesh = hp::Guid(0x1234);
        drawable.add<hp::MeshRenderer>(renderer);
        scene.propagateTransforms();

        hp::SceneViewStats stats;
        Diligent::ITextureView* published =
            sceneView.render(device.render->context(), scene, pool, 0, &stats);

        CHECK(stats.hadCamera);
        CHECK(stats.considered == 1);
        CHECK(stats.submitted == 0);
        CHECK(stats.missingMesh == 1);
        CHECK(published != nullptr);
    }

    SUBCASE("resizing the view resizes the target, and is a no-op when unchanged") {
        REQUIRE(sceneView.resize(640, 480));
        CHECK(sceneView.width() == 640);
        CHECK(sceneView.height() == 480);
        // Safe to call every frame -- FrameTargets debounces, which is what lets
        // callers skip a guard each of them would get slightly wrong.
        REQUIRE(sceneView.resize(640, 480));
        CHECK(sceneView.width() == 640);

        hp::Scene scene;
        scene.propagateTransforms();
        CHECK(sceneView.render(device.render->context(), scene, pool) != nullptr);
    }

    SUBCASE("the published texture is a shader resource, so a viewport can sample it") {
        // 28.5's whole purpose: the texture handed to a listener must be usable
        // as an ImGui image (T0033) or a full-screen blit (T0042). A render
        // target view that is not also a shader resource would pass every test
        // above and be useless to both.
        hp::Scene scene;
        scene.propagateTransforms();
        Diligent::ITextureView* published =
            sceneView.render(device.render->context(), scene, pool);
        REQUIRE(published != nullptr);
        CHECK(published == sceneView.colour());
    }

    sceneView.release();
    CHECK_FALSE(sceneView.valid());
    tearDown(device);
}

} // namespace

TEST_CASE("the scene view publishes an offscreen frame on the default backend"
          * doctest::test_suite("gpu")) {
    exerciseSceneView(hp::RenderBackend::Default, "default");
}
