// Lights: gathering, placement, and surviving a save (T0079).
//
// Bucket: fast. What shading *looks like* needs a device and lives in
// tests/gpu/lit_surface_test.cpp. What is here is everything that decides
// whether the right lights, pointing the right way, reach the renderer at all —
// and whether they still exist after a scene round-trip.

#include <doctest/doctest.h>

#include <hp/Light.hpp>
#include <hp/Reflect.hpp>
#include <hp/Scene.hpp>
#include <hp/Serialize.hpp>
#include <hp/Yaml.hpp>

#include <cmath>

namespace {

hp::Entity makeLight(hp::Scene& scene, const char* name, hp::LightType type) {
    hp::Entity entity = scene.create(name);
    hp::Light light;
    light.type = type;
    entity.add<hp::Light>(light);
    return entity;
}

} // namespace

TEST_CASE("an empty scene has no lights, which is not an error") {
    hp::Scene scene;
    scene.propagateTransforms();
    CHECK(hp::gatherLights(scene).empty());
}

TEST_CASE("gathering finds enabled lights and skips disabled ones") {
    hp::Scene scene;
    makeLight(scene, "sun", hp::LightType::Directional);
    hp::Entity off = makeLight(scene, "off", hp::LightType::Point);
    off.get<hp::Light>().enabled = false;
    scene.propagateTransforms();

    const hp::LightList lights = hp::gatherLights(scene);
    REQUIRE(lights.size() == 1);
    CHECK(lights[0].light.type == hp::LightType::Directional);
}

TEST_CASE("gathering filters on WorldTransform, which every entity has from birth") {
    // Written expecting a light with no propagation to be skipped, and measured
    // otherwise: `Scene::create` gives an entity its `WorldTransform`
    // immediately, so the filter never excludes one in practice. Kept as the
    // record of what the filter actually does -- it guards against a light on a
    // raw entt entity created behind the scene's back, not against forgetting to
    // propagate.
    hp::Scene scene;
    makeLight(scene, "sun", hp::LightType::Directional);
    CHECK(hp::gatherLights(scene).size() == 1);

    // Propagation still matters for *where* the light is; it just is not what
    // decides whether it is seen at all.
    scene.propagateTransforms();
    CHECK(hp::gatherLights(scene).size() == 1);
}

TEST_CASE("the light cap is honoured") {
    hp::Scene scene;
    for (int i = 0; i < 20; ++i) {
        makeLight(scene, "lamp", hp::LightType::Point);
    }
    scene.propagateTransforms();

    CHECK(hp::gatherLights(scene, 4).size() == 4);
    CHECK(hp::gatherLights(scene).size() == hp::kMaxLights);
    // Zero is meaningful rather than a bad argument: it is how a caller asks for
    // an unlit pass without emptying the scene.
    CHECK(hp::gatherLights(scene, 0).empty());
}

// --- placement, shared with T0093's projectors -------------------------------

TEST_CASE("an identity transform points down negative Z") {
    // **glTF's KHR_lights_punctual convention.** Getting this backwards lights
    // the world from behind, which reads as the light not working rather than as
    // a sign error -- so it is pinned here rather than left to a GPU test.
    const hp::ResolvedPlacement placement = hp::resolvePlacement(hp::float4x4::Identity());
    CHECK(placement.direction.x == doctest::Approx(0.0F));
    CHECK(placement.direction.y == doctest::Approx(0.0F));
    CHECK(placement.direction.z == doctest::Approx(-1.0F));
    CHECK(placement.position.x == doctest::Approx(0.0F));
}

TEST_CASE("position comes from the fourth row, not a column") {
    // The engine is row-major and multiplies left to right, so reading a column
    // here would silently transpose every light in the scene.
    const hp::float4x4 world = hp::float4x4::Translation(3.0F, -4.0F, 5.0F);
    const hp::ResolvedPlacement placement = hp::resolvePlacement(world);
    CHECK(placement.position.x == doctest::Approx(3.0F));
    CHECK(placement.position.y == doctest::Approx(-4.0F));
    CHECK(placement.position.z == doctest::Approx(5.0F));
}

TEST_CASE("a scaled transform still yields a unit direction") {
    // Scale must not leak into the direction: a light on a scaled parent would
    // otherwise get a longer or shorter vector, which the shader reads as a
    // brighter or dimmer one.
    const hp::float4x4 world = hp::float4x4::Scale(5.0F, 5.0F, 5.0F);
    const hp::ResolvedPlacement placement = hp::resolvePlacement(world);
    const float length = std::sqrt(placement.direction.x * placement.direction.x
                                   + placement.direction.y * placement.direction.y
                                   + placement.direction.z * placement.direction.z);
    CHECK(length == doctest::Approx(1.0F));
}

TEST_CASE("a degenerate transform yields a default facing, never a NaN") {
    // **A NaN here propagates into the shading of every object**, not just the
    // one broken entity, because the light array is frame-wide. Worth a branch.
    const hp::float4x4 world = hp::float4x4::Scale(0.0F, 0.0F, 0.0F);
    const hp::ResolvedPlacement placement = hp::resolvePlacement(world);
    CHECK_FALSE(std::isnan(placement.direction.x));
    CHECK_FALSE(std::isnan(placement.direction.z));
    CHECK(placement.direction.z == doctest::Approx(-1.0F));
}

TEST_CASE("a light inherits its parent's transform") {
    // The whole reason position and direction are not stored on the component:
    // a lamp on a swinging arm works with no special case (T0101).
    hp::Scene scene;
    hp::Entity rig = scene.create("rig");
    hp::Transform placement;
    placement.position = hp::float3{10.0F, 0.0F, 0.0F};
    scene.setLocalTransform(rig, placement);

    hp::Entity lamp = makeLight(scene, "lamp", hp::LightType::Point);
    REQUIRE(scene.setParent(lamp, rig));
    scene.propagateTransforms();

    const hp::LightList lights = hp::gatherLights(scene);
    REQUIRE(lights.size() == 1);
    CHECK(lights[0].position.x == doctest::Approx(10.0F));
}

// --- serialization (79.1's other half) ---------------------------------------

TEST_CASE("a light survives a YAML round trip") {
    // **Both, and in this order.** `adoptMetaContext` points this translation
    // unit at the engine's `entt::meta` context; without it the registry looks
    // empty here and `writeProperties` returns false with no diagnostic, which
    // reads exactly like a broken serializer.
    hp::adoptMetaContext();
    hp::registerCoreComponents();
    // Until this landed, `Light` was not registered with `entt::meta` at all, so
    // a light could be authored and could not be saved -- a component that looks
    // finished and silently loses its data.
    hp::Light original;
    original.type = hp::LightType::Spot;
    original.colour = hp::float3{0.9F, 0.4F, 0.1F};
    original.intensity = 12.5F;
    original.range = 33.0F;
    original.innerConeAngle = 0.3F;
    original.outerConeAngle = 0.8F;
    original.enabled = false;

    hp::YamlDocument doc;
    REQUIRE(hp::writeProperties(doc.root(), entt::forward_as_meta(original)));

    hp::Light restored;
    REQUIRE(hp::readProperties(doc.root(), entt::forward_as_meta(restored)));

    CHECK(restored.type == hp::LightType::Spot);
    CHECK(restored.colour.r == doctest::Approx(0.9F));
    CHECK(restored.intensity == doctest::Approx(12.5F));
    CHECK(restored.range == doctest::Approx(33.0F));
    CHECK(restored.innerConeAngle == doctest::Approx(0.3F));
    CHECK(restored.outerConeAngle == doctest::Approx(0.8F));
    CHECK_FALSE(restored.enabled);
}

TEST_CASE("a renderer's object layers survive a round trip") {
    // **Both, and in this order.** `adoptMetaContext` points this translation
    // unit at the engine's `entt::meta` context; without it the registry looks
    // empty here and `writeProperties` returns false with no diagnostic, which
    // reads exactly like a broken serializer.
    hp::adoptMetaContext();
    hp::registerCoreComponents();
    // Added with the field in T0085 and missed at the time: without it a scene
    // reloads with everything back on the default layer, visible to cameras that
    // were explicitly told to exclude it.
    hp::MeshRenderer original;
    original.layers = hp::LayerMask::layer(6);

    hp::YamlDocument doc;
    REQUIRE(hp::writeProperties(doc.root(), entt::forward_as_meta(original)));

    hp::MeshRenderer restored;
    REQUIRE(hp::readProperties(doc.root(), entt::forward_as_meta(restored)));
    CHECK(restored.layers == hp::LayerMask::layer(6));
}

TEST_CASE("an out-of-range light type is refused, not cast blindly") {
    // **Both, and in this order.** `adoptMetaContext` points this translation
    // unit at the engine's `entt::meta` context; without it the registry looks
    // empty here and `writeProperties` returns false with no diagnostic, which
    // reads exactly like a broken serializer.
    hp::adoptMetaContext();
    hp::registerCoreComponents();
    // The switch that reads `LightType` has no default case by design, so a file
    // from a build with more light types must fail loudly here rather than
    // produce an enum value nothing handles.
    const auto doc = hp::YamlDocument::parse("type: 99\n");
    REQUIRE(doc);
    hp::Light restored;
    const hp::LightType before = restored.type;
    // `readProperties` does not fail the whole document over one unreadable
    // leaf -- measured, not assumed -- so the guarantee worth pinning is the
    // narrower and more important one: **no out-of-range enum ever reaches the
    // component.** The switch that reads `LightType` has no default case by
    // design, so a value of 99 would be undefined behaviour downstream rather
    // than a visibly wrong light.
    (void)hp::readProperties(const_cast<hp::YamlDocument&>(*doc).root(),
                             entt::forward_as_meta(restored));
    CHECK(restored.type == before);
    CHECK(static_cast<int>(restored.type) <= static_cast<int>(hp::LightType::Spot));
}

// --- per-object selection (79.3) and the illumination mask (79.5) ------------

namespace {

hp::ResolvedLight lampAt(float x, hp::LightType type, float range = 100.0F) {
    hp::ResolvedLight resolved;
    resolved.light.type = type;
    resolved.light.range = range;
    resolved.position = hp::float3{x, 0.0F, 0.0F};
    return resolved;
}

} // namespace

TEST_CASE("selection keeps the nearest lights and drops the rest") {
    hp::LightList all{lampAt(100.0F, hp::LightType::Point), lampAt(1.0F, hp::LightType::Point),
                      lampAt(10.0F, hp::LightType::Point)};
    hp::LightList chosen;

    hp::selectLightsFor(all, hp::float3{0.0F, 0.0F, 0.0F}, hp::LayerMask::all(), 2, chosen);

    REQUIRE(chosen.size() == 2);
    CHECK(chosen[0].position.x == doctest::Approx(1.0F));   // nearest first
    CHECK(chosen[1].position.x == doctest::Approx(10.0F));
}

TEST_CASE("a directional light is never crowded out by a nearer lamp") {
    // **The selection mistake a player notices immediately**: walk past a candle
    // and the sun stops lighting the world. A directional light has no position
    // to be far from, so ranking it by distance is meaningless.
    hp::LightList all{lampAt(0.0F, hp::LightType::Point), lampAt(0.0F, hp::LightType::Directional)};
    hp::LightList chosen;

    hp::selectLightsFor(all, hp::float3{500.0F, 0.0F, 0.0F}, hp::LayerMask::all(), 1, chosen);

    REQUIRE(chosen.size() == 1);
    CHECK(chosen[0].light.type == hp::LightType::Directional);
}

TEST_CASE("a light past its own range does not occupy a slot") {
    // Without this a distant lamp crowds out a reaching one and the object goes
    // dark for no visible reason.
    hp::LightList all{lampAt(50.0F, hp::LightType::Point, 5.0F),
                      lampAt(3.0F, hp::LightType::Point, 100.0F)};
    hp::LightList chosen;

    hp::selectLightsFor(all, hp::float3{0.0F, 0.0F, 0.0F}, hp::LayerMask::all(), 4, chosen);

    REQUIRE(chosen.size() == 1);
    CHECK(chosen[0].position.x == doctest::Approx(3.0F));
}

TEST_CASE("the illumination mask excludes objects a light must not touch") {
    // T0085.4: put the player on its own layer and a light on a mask excluding
    // it, and that light does not affect the player.
    hp::ResolvedLight worldLamp = lampAt(1.0F, hp::LightType::Point);
    worldLamp.light.layers = hp::LayerMask::layer(0);
    hp::ResolvedLight playerLamp = lampAt(2.0F, hp::LightType::Point);
    playerLamp.light.layers = hp::LayerMask::layer(1);

    const hp::LightList all{worldLamp, playerLamp};
    hp::LightList chosen;

    hp::selectLightsFor(all, hp::float3{0.0F, 0.0F, 0.0F}, hp::LayerMask::layer(1), 4, chosen);
    REQUIRE(chosen.size() == 1);
    CHECK(chosen[0].position.x == doctest::Approx(2.0F));

    // And an object on a layer no light reaches is simply unlit -- legitimate,
    // not an error.
    hp::selectLightsFor(all, hp::float3{0.0F, 0.0F, 0.0F}, hp::LayerMask::layer(5), 4, chosen);
    CHECK(chosen.empty());
}

TEST_CASE("selection is bounded by the shader's light limit") {
    hp::LightList all;
    for (int i = 0; i < 40; ++i) {
        all.push_back(lampAt(static_cast<float>(i), hp::LightType::Point));
    }
    hp::LightList chosen;

    // Asking for more than the shader can hold is clamped, not honoured -- the
    // frame buffer is sized for `kMaxLights` and writing past it corrupts
    // whatever follows.
    hp::selectLightsFor(all, hp::float3{}, hp::LayerMask::all(), 1000, chosen);
    CHECK(chosen.size() == hp::kMaxLights);

    hp::selectLightsFor(all, hp::float3{}, hp::LayerMask::all(), 0, chosen);
    CHECK(chosen.empty());
}

TEST_CASE("selection reuses its output buffer without leaking earlier results") {
    // It is called once per drawable object per frame, so the caller passes the
    // same vector every time. A stale entry would light an object with a lamp
    // that was chosen for a different one.
    hp::LightList all{lampAt(1.0F, hp::LightType::Point)};
    hp::LightList chosen;
    hp::selectLightsFor(all, hp::float3{}, hp::LayerMask::all(), 4, chosen);
    REQUIRE(chosen.size() == 1);

    hp::selectLightsFor({}, hp::float3{}, hp::LayerMask::all(), 4, chosen);
    CHECK(chosen.empty());
}
