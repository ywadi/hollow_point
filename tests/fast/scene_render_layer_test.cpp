// The scene render layer's device-free behaviour (T0027.3, T0027.4).
//
// Bucket: fast, so there is no device and nothing here draws. What that leaves
// is still worth asserting, because it is where the mistakes are cheap to make
// and expensive to see: the configuration `configureAsHud` applies, and the
// guards that keep an uncreated or unpointed layer from taking a frame down.
//
// **Whether the composite is actually correct is not answerable here** and is
// not attempted. That needs pixels, and it lives in tests/gpu/.

#include <doctest/doctest.h>

#include <hp/Assets.hpp>
#include <hp/Scene.hpp>
#include <hp/SceneRenderLayer.hpp>

#include <string>

TEST_CASE("a fresh layer is a world layer that draws nothing") {
    hp::SceneRenderLayer layer;

    CHECK(std::string(layer.name()) == "scene");
    CHECK(layer.viewSlot == 0);
    CHECK(layer.order == 0);
    // Depth on, because the default layer is the world.
    CHECK(layer.useDepth);
    CHECK_FALSE(layer.valid());
    CHECK(layer.scene() == nullptr);
}

TEST_CASE("a layer is named for its profiling zone") {
    hp::SceneRenderLayer layer("world");
    CHECK(std::string(layer.name()) == "world");

    // A null name would otherwise reach `HP_PROFILE_ZONE_NAMED` and the log.
    hp::SceneRenderLayer unnamed(nullptr);
    CHECK(std::string(unnamed.name()) == "scene");
}

TEST_CASE("configureAsHud sets the three things that make 2D-over-3D work") {
    hp::SceneRenderLayer hud("hud");
    hp::configureAsHud(hud);

    // **The one that matters.** A HUD that depth-tests against the world
    // vanishes behind whatever geometry is near the camera, and that reads as
    // flickering UI rather than as a depth bug.
    CHECK_FALSE(hud.useDepth);
    // Clearing colour would erase the world this is drawn over.
    CHECK(hud.clear == hp::LayerClear::None);
    // Its own slot, so it resolves its own camera without the world knowing.
    CHECK(hud.viewSlot == 1);
    CHECK(hud.order == 100);
}

TEST_CASE("configureAsHud takes a slot and an order for a second overlay") {
    hp::SceneRenderLayer overlay("debug");
    hp::configureAsHud(overlay, 2, 200);

    CHECK(overlay.viewSlot == 2);
    CHECK(overlay.order == 200);
    CHECK_FALSE(overlay.useDepth);
}

TEST_CASE("rendering an uncreated layer is a no-op rather than a crash") {
    hp::SceneRenderLayer layer;
    hp::Scene scene;
    hp::AssetPool pool;
    layer.setScene(&scene, &pool);

    // No device, no context, no targets -- which is what a layer sees if it is
    // added to a stack before `create` succeeds.
    hp::RenderPassContext pass;
    pass.width = 320;
    pass.height = 240;
    layer.onRenderLayer(pass);

    CHECK_FALSE(layer.lastFrameHadCamera);
    CHECK(layer.lastFrame.submitted == 0);
}

TEST_CASE("a layer with no scene renders nothing and says so") {
    hp::SceneRenderLayer layer;
    CHECK(layer.scene() == nullptr);

    hp::RenderPassContext pass;
    pass.width = 320;
    pass.height = 240;
    layer.onRenderLayer(pass);
    CHECK_FALSE(layer.lastFrameHadCamera);

    // Nulls are how a layer is parked without removing it from the stack, so
    // setting them back must be accepted rather than treated as an error.
    hp::Scene scene;
    hp::AssetPool pool;
    layer.setScene(&scene, &pool);
    CHECK(layer.scene() == &scene);
    layer.setScene(nullptr, nullptr);
    CHECK(layer.scene() == nullptr);
}

TEST_CASE("release is idempotent and leaves the layer invalid") {
    hp::SceneRenderLayer layer;
    layer.release();
    CHECK_FALSE(layer.valid());
    layer.release();
    CHECK_FALSE(layer.valid());
}
