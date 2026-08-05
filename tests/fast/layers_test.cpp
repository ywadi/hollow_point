// Object layers and the masks that filter them (T0085).
//
// Bucket: fast. A bitmask needs no device, and the filter it drives lives in
// `parseScene`, which was deliberately written free of the device, the pool and
// the camera so it could be tested exactly here.

#include <doctest/doctest.h>

#include <hp/DrawSubmission.hpp>
#include <hp/Layers.hpp>
#include <hp/Scene.hpp>

namespace {

/// A drawable entity on the given layers.
hp::Entity makeDrawable(hp::Scene& scene, const char* name, hp::LayerMask layers) {
    hp::Entity entity = scene.create(name);
    hp::MeshRenderer renderer;
    renderer.mesh = hp::Guid::generate();
    renderer.layers = layers;
    entity.add<hp::MeshRenderer>(renderer);
    return entity;
}

} // namespace

TEST_CASE("a default mask matches nothing and all() matches everything") {
    // Empty by default rather than full: a mask is a filter, and a
    // default-constructed filter that silently passed everything would make
    // "I forgot to set this" indistinguishable from "I meant everything".
    CHECK(hp::LayerMask{}.empty());
    CHECK_FALSE(hp::LayerMask::all().empty());
    CHECK(hp::LayerMask::none().empty());
    CHECK(hp::LayerMask::all().intersects(hp::LayerMask::layer(0)));
    CHECK(hp::LayerMask::all().intersects(hp::LayerMask::layer(31)));
}

TEST_CASE("a layer index becomes exactly one bit") {
    CHECK(hp::LayerMask::layer(0).bits == 1U);
    CHECK(hp::LayerMask::layer(1).bits == 2U);
    CHECK(hp::LayerMask::layer(31).bits == 0x80000000U);
    CHECK(hp::LayerMask::layer(5).has(5));
    CHECK_FALSE(hp::LayerMask::layer(5).has(6));
}

TEST_CASE("an out-of-range layer is empty, not wrapped") {
    // **The bug this prevents is not a crash, it is aliasing.** A shift by 32 is
    // undefined behaviour, and on most hardware it wraps -- so layer 32 would
    // silently become layer 0, and an object would be lit by a light that was
    // explicitly told to exclude it.
    CHECK(hp::LayerMask::layer(32).empty());
    CHECK(hp::LayerMask::layer(-1).empty());
    CHECK(hp::LayerMask::layer(1000).empty());
    CHECK_FALSE(hp::LayerMask::all().has(32));
}

TEST_CASE("intersects is the one test the hot loops run") {
    hp::LayerMask viewer;
    viewer.add(1);
    viewer.add(3);

    CHECK(viewer.intersects(hp::LayerMask::layer(1)));
    CHECK(viewer.intersects(hp::LayerMask::layer(3)));
    CHECK_FALSE(viewer.intersects(hp::LayerMask::layer(2)));
    // An object on several layers matches if *any* of them is in the mask.
    hp::LayerMask onTwo;
    onTwo.add(2);
    onTwo.add(3);
    CHECK(viewer.intersects(onTwo));
    // Nothing intersects an empty mask, in either direction.
    CHECK_FALSE(viewer.intersects(hp::LayerMask::none()));
    CHECK_FALSE(hp::LayerMask::none().intersects(hp::LayerMask::all()));
}

TEST_CASE("add and remove are idempotent and ignore out-of-range layers") {
    hp::LayerMask mask;
    mask.add(4);
    mask.add(4);
    CHECK(mask.bits == hp::LayerMask::layer(4).bits);
    mask.remove(4);
    CHECK(mask.empty());
    mask.remove(4);
    CHECK(mask.empty());

    mask.add(99);
    CHECK(mask.empty());
}

TEST_CASE("a renderer defaults to layer 0 and a camera to every layer") {
    // The pairing that makes the common case work with no configuration: an
    // object nobody assigned is visible to a camera nobody configured.
    const hp::MeshRenderer renderer;
    const hp::Camera camera;
    CHECK(renderer.layers == hp::defaultObjectLayers());
    CHECK(camera.cullingMask == hp::LayerMask::all());
    CHECK(camera.cullingMask.intersects(renderer.layers));
}

// --- the filter in parseScene ------------------------------------------------

TEST_CASE("parseScene keeps everything under the default mask") {
    hp::Scene scene;
    makeDrawable(scene, "a", hp::defaultObjectLayers());
    makeDrawable(scene, "b", hp::LayerMask::layer(7));
    scene.propagateTransforms();

    hp::DrawParseStats stats;
    const hp::DrawList list = hp::parseScene(scene, hp::LayerMask::all(), &stats);

    CHECK(list.size() == 2);
    CHECK(stats.culledByLayer == 0);
}

TEST_CASE("parseScene rejects objects outside the camera's mask") {
    hp::Scene scene;
    makeDrawable(scene, "world", hp::LayerMask::layer(0));
    makeDrawable(scene, "hud", hp::LayerMask::layer(1));
    makeDrawable(scene, "alsoHud", hp::LayerMask::layer(1));
    scene.propagateTransforms();

    hp::DrawParseStats worldStats;
    const hp::DrawList worldList =
        hp::parseScene(scene, hp::LayerMask::layer(0), &worldStats);
    CHECK(worldList.size() == 1);
    CHECK(worldStats.considered == 3);
    CHECK(worldStats.culledByLayer == 2);

    hp::DrawParseStats hudStats;
    const hp::DrawList hudList = hp::parseScene(scene, hp::LayerMask::layer(1), &hudStats);
    CHECK(hudList.size() == 2);
    CHECK(hudStats.culledByLayer == 1);
}

TEST_CASE("this is what lets a world layer and a HUD layer share one scene") {
    // **The limitation T0027 hit, now closed.** Its composite test needed two
    // separate `Scene`s because a view slot picks the *camera* and does not
    // filter *objects*, so each layer drew the other's geometry. With the mask
    // honoured, one scene suffices -- which is what the "a HUD is just a camera
    // on another view slot" framing always implied.
    hp::Scene scene;
    makeDrawable(scene, "terrain", hp::LayerMask::layer(0));
    makeDrawable(scene, "healthBar", hp::LayerMask::layer(1));
    scene.propagateTransforms();

    hp::Camera world;
    world.cullingMask = hp::LayerMask::layer(0);
    hp::Camera hud;
    hud.viewSlot = 1;
    hud.cullingMask = hp::LayerMask::layer(1);

    CHECK(hp::parseScene(scene, world.cullingMask).size() == 1);
    CHECK(hp::parseScene(scene, hud.cullingMask).size() == 1);
}

TEST_CASE("an empty mask draws nothing, which is how a camera is blanked") {
    hp::Scene scene;
    makeDrawable(scene, "a", hp::defaultObjectLayers());
    scene.propagateTransforms();

    hp::DrawParseStats stats;
    CHECK(hp::parseScene(scene, hp::LayerMask::none(), &stats).empty());
    CHECK(stats.culledByLayer == 1);
}

TEST_CASE("an object on no layer is invisible to every camera") {
    // A legitimate way to hide something without destroying it, and the mirror
    // of the empty camera mask above.
    hp::Scene scene;
    makeDrawable(scene, "hidden", hp::LayerMask::none());
    scene.propagateTransforms();

    hp::DrawParseStats stats;
    CHECK(hp::parseScene(scene, hp::LayerMask::all(), &stats).empty());
    CHECK(stats.culledByLayer == 1);
}

TEST_CASE("the layer test runs before the mesh check, so culled objects cost nothing") {
    // Ordering is observable through the statistics, and it is the ordering that
    // matters: an object the camera does not render must not be examined
    // further. A culled entity is counted as culled, never as `withoutMesh`.
    hp::Scene scene;
    hp::Entity entity = scene.create("no mesh, wrong layer");
    hp::MeshRenderer renderer;
    renderer.layers = hp::LayerMask::layer(9);
    entity.add<hp::MeshRenderer>(renderer);
    scene.propagateTransforms();

    hp::DrawParseStats stats;
    CHECK(hp::parseScene(scene, hp::LayerMask::layer(0), &stats).empty());
    CHECK(stats.culledByLayer == 1);
    CHECK(stats.withoutMesh == 0);
}
