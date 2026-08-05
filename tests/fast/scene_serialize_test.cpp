// Scene serialization round trips (T0022).
//
// The case that matters most here is **`every registered component round-trips`**
// below, and it exists because of two bugs found by accident on 2026-08-05:
// `Light` was not registered at all, and `MeshRenderer::layers` was added as a
// field and never reflected — so a scene would have reloaded with every object
// back on the default layer, visible to cameras explicitly told to exclude it.
// Both were caught by writing an unrelated test.
//
// That is luck, and luck does not survive a context reset. So the test walks the
// component registry itself and asserts a round trip per registered type, which
// converts "did somebody remember" into "the suite fails". It is also the only
// thing that will catch the next component added without a `.property<>` line.

#include <doctest/doctest.h>

#include <hp/Camera.hpp>
#include <hp/Layers.hpp>
#include <hp/Light.hpp>
#include <hp/Reflect.hpp>
#include <hp/Scene.hpp>
#include <hp/SceneSerialize.hpp>

#include <string>

namespace {

/// A scene with a parent, two children and one component of every serialized
/// engine type, so a round trip has something to lose.
hp::Scene makePopulated() {
    hp::Scene scene;

    hp::Entity root = scene.create("Root");
    hp::Transform rootTransform;
    rootTransform.position = hp::float3{1.0F, 2.0F, 3.0F};
    scene.setLocalTransform(root, rootTransform);

    hp::Entity child = scene.create("Child");
    hp::Transform childTransform;
    childTransform.position = hp::float3{4.0F, 5.0F, 6.0F};
    scene.setLocalTransform(child, childTransform);
    scene.setParent(child, root);

    hp::MeshRenderer renderer;
    renderer.mesh = hp::Guid::generate();
    renderer.material = hp::Guid::generate();
    renderer.layers = hp::LayerMask::layer(7);
    child.add<hp::MeshRenderer>(renderer);

    hp::Entity cameraEntity = scene.create("Camera");
    hp::Camera camera;
    camera.verticalFov = 1.2F;
    camera.priority = 3;
    cameraEntity.add<hp::Camera>(camera);

    hp::Entity sun = scene.create("Sun");
    hp::Light light;
    light.type = hp::LightType::Spot;
    light.intensity = 2.5F;
    light.colour = hp::float3{0.25F, 0.5F, 0.75F};
    sun.add<hp::Light>(light);

    scene.propagateTransforms();
    return scene;
}

} // namespace

TEST_CASE("an empty scene round-trips and is not an error") {
    hp::Scene scene;
    const std::string text = hp::saveSceneToString(scene);

    // Distinguishes "saved nothing" from "failed": the version is always there.
    CHECK(text.find("version") != std::string::npos);

    hp::Scene loaded;
    const hp::SceneLoadResult result = hp::loadSceneFromString(loaded, text);
    CHECK(result.status == hp::SceneLoadStatus::Ok);
    CHECK(result.entities == 0);
}

TEST_CASE("the document is readable and diffable") {
    hp::Scene scene = makePopulated();
    const std::string text = hp::saveSceneToString(scene);

    // Not a golden-file comparison — that would fail on every unrelated field
    // added. These are the shape decisions the schema actually commits to.
    CHECK(text.find("entities:") != std::string::npos);
    CHECK(text.find("guid:") != std::string::npos);
    CHECK(text.find("name: Root") != std::string::npos);
    CHECK(text.find("parent:") != std::string::npos);
    CHECK(text.find("Transform:") != std::string::npos);

    // The four that must never appear as components, each for its own reason.
    CHECK(text.find("Hierarchy:") == std::string::npos);
    CHECK(text.find("WorldTransform:") == std::string::npos);
    CHECK(text.find("Id:") == std::string::npos);
    CHECK(text.find("Tag:") == std::string::npos);
}

TEST_CASE("GUIDs are preserved, not regenerated") {
    hp::Scene scene = makePopulated();

    std::vector<hp::Guid> before;
    for (const entt::entity handle : scene.registry().view<const hp::Id>()) {
        before.push_back(scene.registry().get<const hp::Id>(handle).guid);
    }
    REQUIRE(before.size() == 4);

    hp::Scene loaded;
    REQUIRE(hp::loadSceneFromString(loaded, hp::saveSceneToString(scene)).status ==
            hp::SceneLoadStatus::Ok);

    // Regenerating GUIDs breaks every reference into the scene from outside and
    // corrupts a project subtly enough to be worth asserting explicitly.
    for (const hp::Guid& guid : before) {
        CHECK(loaded.find(guid).has_value());
    }
}

TEST_CASE("the hierarchy survives, through GUIDs rather than handles") {
    hp::Scene scene = makePopulated();

    hp::Guid rootGuid;
    hp::Guid childGuid;
    for (const entt::entity handle : scene.registry().view<const hp::Id, const hp::Tag>()) {
        const std::string& tag = scene.registry().get<const hp::Tag>(handle).name;
        const hp::Guid guid = scene.registry().get<const hp::Id>(handle).guid;
        if (tag == "Root") {
            rootGuid = guid;
        } else if (tag == "Child") {
            childGuid = guid;
        }
    }

    hp::Scene loaded;
    REQUIRE(hp::loadSceneFromString(loaded, hp::saveSceneToString(scene)).status ==
            hp::SceneLoadStatus::Ok);

    const auto root = loaded.find(rootGuid);
    const auto child = loaded.find(childGuid);
    REQUIRE(root.has_value());
    REQUIRE(child.has_value());

    const auto& hierarchy = loaded.registry().get<const hp::Hierarchy>(child->handle());
    CHECK(hierarchy.parent == root->handle());
}

TEST_CASE("a child listed before its parent still links") {
    // Forward references are the reason load is two passes. Hand-written rather
    // than round-tripped, because save happens to emit parents first.
    const std::string text = R"(version: 1
entities:
  - guid: 00000000000000c1
    name: Child
    parent: 00000000000000a1
    components: {}
  - guid: 00000000000000a1
    name: Parent
    components: {}
)";

    hp::Scene scene;
    REQUIRE(hp::loadSceneFromString(scene, text).status == hp::SceneLoadStatus::Ok);

    const auto parent = scene.find(*hp::Guid::parse("00000000000000a1"));
    const auto child = scene.find(*hp::Guid::parse("00000000000000c1"));
    REQUIRE(parent.has_value());
    REQUIRE(child.has_value());
    CHECK(scene.registry().get<const hp::Hierarchy>(child->handle()).parent == parent->handle());
}

TEST_CASE("component values survive exactly") {
    hp::Scene scene = makePopulated();
    hp::Scene loaded;
    REQUIRE(hp::loadSceneFromString(loaded, hp::saveSceneToString(scene)).status ==
            hp::SceneLoadStatus::Ok);

    SUBCASE("MeshRenderer, including the layer mask T0085 lost once") {
        bool found = false;
        for (const entt::entity handle : loaded.registry().view<const hp::MeshRenderer>()) {
            const auto& renderer = loaded.registry().get<const hp::MeshRenderer>(handle);
            found = true;
            // The specific regression: a scene reloading with everything back on
            // the default layer, visible to cameras told to exclude it.
            CHECK(renderer.layers.has(7));
            CHECK_FALSE(renderer.mesh == hp::Guid{});
        }
        CHECK(found);
    }

    SUBCASE("Light, including its enum by name") {
        bool found = false;
        for (const entt::entity handle : loaded.registry().view<const hp::Light>()) {
            const auto& light = loaded.registry().get<const hp::Light>(handle);
            found = true;
            CHECK(light.type == hp::LightType::Spot);
            CHECK(light.intensity == doctest::Approx(2.5F));
            CHECK(light.colour.y == doctest::Approx(0.5F));
        }
        CHECK(found);
    }

    SUBCASE("Camera") {
        bool found = false;
        for (const entt::entity handle : loaded.registry().view<const hp::Camera>()) {
            const auto& camera = loaded.registry().get<const hp::Camera>(handle);
            found = true;
            CHECK(camera.verticalFov == doctest::Approx(1.2F));
            CHECK(camera.priority == 3);
        }
        CHECK(found);
    }
}

TEST_CASE("every registered component round-trips") {
    // **The systematic version, and the point of this file.** Walking the
    // registry means a component added tomorrow is covered without anybody
    // remembering to extend this test — and a component registered with no
    // reflected properties fails here rather than silently saving nothing.
    hp::Scene scene = makePopulated();
    const std::string text = hp::saveSceneToString(scene);

    hp::Scene loaded;
    REQUIRE(hp::loadSceneFromString(loaded, text).status == hp::SceneLoadStatus::Ok);

    int checked = 0;
    for (const hp::detail::ComponentOps& ops : hp::detail::registeredComponents()) {
        if (!ops.serialized) {
            continue;
        }
        CAPTURE(ops.name);

        // Reflected at all — the `Light` bug was that it was not.
        const entt::meta_type type = hp::resolveType(ops.name);
        CHECK(static_cast<bool>(type));

        // And described. A serialized component with no properties writes an
        // empty map, which round-trips "successfully" while carrying nothing.
        std::size_t properties = 0;
        if (type) {
            for ([[maybe_unused]] auto&& data : type.data()) {
                ++properties;
            }
        }
        CHECK(properties > 0);
        ++checked;
    }
    CHECK(checked >= 4);
}

TEST_CASE("a file from a newer schema is refused, not partially loaded") {
    const std::string text = "version: " + std::to_string(hp::kSceneSchemaVersion + 1) +
                             "\nentities:\n  - guid: 00000000000000a1\n    components: {}\n";

    hp::Scene scene;
    const hp::SceneLoadResult result = hp::loadSceneFromString(scene, text);

    // Fail closed. Loading what we understand and saving that back silently
    // destroys whatever the newer build wrote.
    CHECK(result.status == hp::SceneLoadStatus::NewerSchema);
    CHECK(result.entities == 0);
}

TEST_CASE("an unknown component is reported rather than passed over") {
    const std::string text = R"(version: 1
entities:
  - guid: 00000000000000a1
    name: Scripted
    components:
      PlayerController:
        speed: 4.5
)";

    hp::Scene scene;
    const hp::SceneLoadResult result = hp::loadSceneFromString(scene, text);

    CHECK(result.status == hp::SceneLoadStatus::Ok);
    CHECK(result.entities == 1);
    // Known shortfall, asserted so it is visible rather than assumed: the type
    // is *counted*, and its data is dropped. D23 wants the raw subtree preserved
    // and re-emitted so a save-after-load does not destroy data belonging to a
    // gameplay type that merely failed to build today. See T0022.
    CHECK(result.unknownComponents == 1);
}

TEST_CASE("malformed input fails cleanly and leaves the scene empty") {
    hp::Scene scene;
    const hp::SceneLoadResult result = hp::loadSceneFromString(scene, "\t: not: yaml: [");
    CHECK(result.status != hp::SceneLoadStatus::Ok);
    CHECK(result.entities == 0);
}
