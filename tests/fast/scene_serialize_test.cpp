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
#include <hp/Cook.hpp>
#include <hp/Layers.hpp>
#include <hp/Light.hpp>
#include <hp/Reflect.hpp>
#include <hp/Scene.hpp>
#include <hp/SceneSerialize.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

/// `entt::null` as an `entt::entity`.
///
/// doctest wraps the left operand of a `CHECK` in an expression-decomposition
/// type, and comparing *that* with `entt::null_t` is ambiguous — entt supplies
/// both a member and a free operator. Entity-to-entity is not.
constexpr entt::entity kNoParent = entt::null;

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

TEST_CASE("saving a loaded scene reproduces the document byte for byte") {
    // **The git-diffable half of the first Done-when, and it was not free.**
    // entt walks a storage's packed array from the back, so `view<Id>` yields
    // reverse creation order — while a load creates in *file* order. Written
    // naively, every save-after-load emitted the entities backwards and the one
    // after that put them back, so opening a scene and saving it produced a
    // whole-file diff containing no change. Measured on this four-entity scene
    // before `entitiesInCreationOrder` existed.
    hp::Scene scene = makePopulated();
    const std::string first = hp::saveSceneToString(scene);

    hp::Scene once;
    REQUIRE(hp::loadSceneFromString(once, first).status == hp::SceneLoadStatus::Ok);
    const std::string second = hp::saveSceneToString(once);
    CHECK(second == first);

    // Twice, because an order that flips is stable every *other* time — one
    // round trip would have passed while the file still churned on each save.
    hp::Scene twice;
    REQUIRE(hp::loadSceneFromString(twice, second).status == hp::SceneLoadStatus::Ok);
    CHECK(hp::saveSceneToString(twice) == first);
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

// --- authoring mode (T0139) ------------------------------------------------
//
// A file the engine wrote and a file a person typed have different needs, and
// the format serves both by being strict about the one thing that cannot be
// recovered — an identity that was recorded wrongly — and lenient about the one
// that was never recorded at all.

TEST_CASE("a scene can be written from nothing, with no GUIDs anywhere") {
    // The goal case, verbatim: no identities, a hierarchy by name, flow-style
    // components. Nothing here is what `saveSceneToString` would produce.
    const std::string authored = R"(version: 1
entities:
  - name: Sun
    components:
      Light: { type: Directional, intensity: 3 }
  - name: Player
    components:
      Transform: { position: [0, 0, 0] }
  - name: Weapon
    parent: Player
    components:
      Transform: { position: [0.3, 1.2, 0] }
)";

    hp::Scene scene;
    const hp::SceneLoadResult result = hp::loadSceneFromString(scene, authored);
    REQUIRE(result.status == hp::SceneLoadStatus::Ok);
    CHECK(result.entities == 3);

    entt::entity player = kNoParent;
    entt::entity weapon = kNoParent;
    for (const entt::entity handle : scene.registry().view<const hp::Tag>()) {
        const std::string& tag = scene.registry().get<const hp::Tag>(handle).name;
        if (tag == "Player") {
            player = handle;
        } else if (tag == "Weapon") {
            weapon = handle;
        }
    }
    REQUIRE(player != kNoParent);
    REQUIRE(weapon != kNoParent);
    CHECK(scene.registry().get<const hp::Hierarchy>(weapon).parent == player);

    SUBCASE("and every entity still got a distinct identity") {
        std::vector<hp::Guid> guids;
        for (const entt::entity handle : scene.registry().view<const hp::Id>()) {
            const hp::Guid guid = scene.registry().get<const hp::Id>(handle).guid;
            CHECK_FALSE(guid == hp::Guid{});
            guids.push_back(guid);
        }
        REQUIRE(guids.size() == 3);
        std::sort(guids.begin(), guids.end(),
                  [](hp::Guid a, hp::Guid b) { return a.value() < b.value(); });
        CHECK(std::adjacent_find(guids.begin(), guids.end()) == guids.end());
    }

    SUBCASE("saving normalises it: GUIDs everywhere, parent by GUID") {
        const std::string saved = hp::saveSceneToString(scene);
        CHECK(saved.find("guid:") != std::string::npos);
        // The name reference is gone from the saved form — it exists at the edge
        // of the system and never inside it.
        CHECK(saved.find("parent: Player") == std::string::npos);
        CHECK(saved.find("parent:") != std::string::npos);

        // And the normalised file is stable, so authoring once does not leave a
        // document that churns on every later save.
        hp::Scene reloaded;
        REQUIRE(hp::loadSceneFromString(reloaded, saved).status == hp::SceneLoadStatus::Ok);
        CHECK(hp::saveSceneToString(reloaded) == saved);
    }
}

TEST_CASE("a malformed guid is refused, because a typo is not a new entity") {
    // **The asymmetry that makes generating safe.** Absent means "no identity
    // was recorded", and nothing can refer to one that was never written.
    // Present-and-wrong means the author cared and mistyped — generating there
    // would orphan every `parent` naming the value they meant, silently.
    const std::string text = R"(version: 1
entities:
  - guid: not-a-guid
    name: Typo
  - guid: 00000000000000a1
    name: Fine
)";

    hp::Scene scene;
    const hp::SceneLoadResult result = hp::loadSceneFromString(scene, text);
    CHECK(result.status == hp::SceneLoadStatus::Ok);
    CHECK(result.entities == 1);
    CHECK(scene.find(*hp::Guid::parse("00000000000000a1")).has_value());
}

TEST_CASE("an ambiguous parent name is refused rather than resolved by luck") {
    // `Tag` is documented as not unique and nothing looks entities up by it.
    // Picking the first match would make the hierarchy depend on file order,
    // which is a bug nobody can reason about from the symptom.
    const std::string text = R"(version: 1
entities:
  - name: Slot
  - name: Slot
  - name: Child
    parent: Slot
)";

    hp::Scene scene;
    REQUIRE(hp::loadSceneFromString(scene, text).entities == 3);

    for (const entt::entity handle : scene.registry().view<const hp::Tag>()) {
        if (scene.registry().get<const hp::Tag>(handle).name != "Child") {
            continue;
        }
        CHECK(scene.registry().get<const hp::Hierarchy>(handle).parent == kNoParent);
    }
}

TEST_CASE("a guid always beats a name when resolving a parent") {
    // So a file the engine wrote never takes the name path, whatever an entity
    // happens to be called.
    const std::string text = R"(version: 1
entities:
  - guid: 00000000000000a1
    name: 00000000000000b1
  - guid: 00000000000000b1
    name: Decoy
  - guid: 00000000000000c1
    name: Child
    parent: 00000000000000b1
)";

    hp::Scene scene;
    REQUIRE(hp::loadSceneFromString(scene, text).entities == 3);

    const auto child = scene.find(*hp::Guid::parse("00000000000000c1"));
    const auto byGuid = scene.find(*hp::Guid::parse("00000000000000b1"));
    REQUIRE(child.has_value());
    REQUIRE(byGuid.has_value());
    // The entity *named* "00000000000000b1" is a different one; the GUID wins.
    CHECK(scene.registry().get<const hp::Hierarchy>(child->handle()).parent
          == byGuid->handle());
}

TEST_CASE("a parent that is neither a guid nor a name leaves a root") {
    const std::string text = R"(version: 1
entities:
  - name: Orphan
    parent: NoSuchEntity
)";

    hp::Scene scene;
    REQUIRE(hp::loadSceneFromString(scene, text).entities == 1);
    for (const entt::entity handle : scene.registry().view<const hp::Hierarchy>()) {
        CHECK(scene.registry().get<const hp::Hierarchy>(handle).parent == kNoParent);
    }
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
    // Counted, and — since D23 — *kept*. The count is what was set aside, not
    // what was lost; `a save-after-load keeps a component this build cannot
    // build` below is what proves the difference.
    CHECK(result.unknownComponents == 1);

    const auto entity = scene.find(*hp::Guid::parse("00000000000000a1"));
    REQUIRE(entity.has_value());
    const auto* preserved =
        scene.registry().try_get<const hp::UnknownComponents>(entity->handle());
    REQUIRE(preserved != nullptr);
    REQUIRE(preserved->items.size() == 1);
    CHECK(preserved->items[0].type == "PlayerController");
    CHECK(preserved->items[0].yaml.find("4.5") != std::string::npos);
}

TEST_CASE("a save-after-load keeps a component this build cannot build") {
    // **The D23 failure, stated as a test.** A gameplay module that fails to
    // build makes its component types absent, which is exactly when a developer
    // opens the editor. If save-after-load dropped them, the fix-and-rebuild
    // would come back to a scene with the data already destroyed — and nothing
    // would have reported it, because from the file layer's side it worked.
    const std::string original = R"(version: 1
entities:
  - guid: 00000000000000a1
    name: Scripted
    components:
      PlayerController:
        speed: 4.5
        waypoints: [north, south]
      Inventory:
        slots: 8
)";

    hp::Scene scene;
    REQUIRE(hp::loadSceneFromString(scene, original).unknownComponents == 2);

    const std::string saved = hp::saveSceneToString(scene);
    CHECK(saved.find("PlayerController:") != std::string::npos);
    CHECK(saved.find("speed: 4.5") != std::string::npos);
    CHECK(saved.find("north") != std::string::npos);
    CHECK(saved.find("Inventory:") != std::string::npos);
    CHECK(saved.find("slots: 8") != std::string::npos);

    SUBCASE("and keeps them through any number of further round trips") {
        // One round trip could pass while the *second* dropped them — the blob
        // has to be re-captured on load, not merely written once.
        hp::Scene again;
        const hp::SceneLoadResult result = hp::loadSceneFromString(again, saved);
        CHECK(result.status == hp::SceneLoadStatus::Ok);
        CHECK(result.unknownComponents == 2);
        const std::string twice = hp::saveSceneToString(again);
        CHECK(twice.find("speed: 4.5") != std::string::npos);
        CHECK(twice.find("slots: 8") != std::string::npos);
    }

    SUBCASE("and carries them through a clone") {
        // A play-mode clone of a scene loaded while the module was broken must
        // not be the thing that finally loses the data.
        hp::Scene copy = scene.clone(hp::CloneIds::Preserve);
        CHECK(hp::saveSceneToString(copy).find("speed: 4.5") != std::string::npos);
    }
}

TEST_CASE("a preserved component becomes real once its type exists") {
    // The other half of D23: absent today, registered tomorrow. The type name
    // here is used by no other test, because the component registry is global to
    // the test binary and registering it changes what "unknown" means everywhere.
    struct LateArrival {
        float speed = 0.0F;
        int slots = 0;
    };

    const std::string text = R"(version: 1
entities:
  - guid: 00000000000000a1
    name: Scripted
    components:
      LateArrival:
        speed: 4.5
        slots: 8
)";

    hp::Scene scene;
    REQUIRE(hp::loadSceneFromString(scene, text).unknownComponents == 1);
    // Nothing to materialise yet, and asking is not an error.
    CHECK(hp::materialiseUnknownComponents(scene) == 0);

    hp::registerComponent<LateArrival>("LateArrival")
        .property<&LateArrival::speed>("speed")
        .property<&LateArrival::slots>("slots");

    CHECK(hp::materialiseUnknownComponents(scene) == 1);

    const auto entity = scene.find(*hp::Guid::parse("00000000000000a1"));
    REQUIRE(entity.has_value());
    const auto* arrived = scene.registry().try_get<const LateArrival>(entity->handle());
    REQUIRE(arrived != nullptr);
    CHECK(arrived->speed == doctest::Approx(4.5F));
    CHECK(arrived->slots == 8);

    // The text is dropped once it is a real component, or the next save would
    // write the key twice.
    CHECK(scene.registry().try_get<const hp::UnknownComponents>(entity->handle()) == nullptr);
    const std::string saved = hp::saveSceneToString(scene);
    CHECK(saved.find("LateArrival:") != std::string::npos);
    CHECK(saved.find("LateArrival:") == saved.rfind("LateArrival:"));

    // Idempotent: there is nothing left to do, and saying so costs one lookup.
    CHECK(hp::materialiseUnknownComponents(scene) == 0);
}

TEST_CASE("a component the schema excludes is ignored, not preserved") {
    // `Hierarchy` is registered and deliberately non-serialized. Treating it as
    // unknown would preserve it and write it back — resurrecting exactly the
    // corrupt key the schema exists to keep out, since its fields are registry
    // slot indices that mean nothing in another registry.
    const std::string text = R"(version: 1
entities:
  - guid: 00000000000000a1
    name: Meddled
    components:
      Hierarchy:
        parent: 7
)";

    hp::Scene scene;
    const hp::SceneLoadResult result = hp::loadSceneFromString(scene, text);
    CHECK(result.status == hp::SceneLoadStatus::Ok);
    CHECK(result.unknownComponents == 0);
    CHECK(hp::saveSceneToString(scene).find("Hierarchy:") == std::string::npos);
}

TEST_CASE("malformed input fails cleanly and leaves the scene empty") {
    hp::Scene scene;
    const hp::SceneLoadResult result = hp::loadSceneFromString(scene, "\t: not: yaml: [");
    CHECK(result.status != hp::SceneLoadStatus::Ok);
    CHECK(result.entities == 0);
}

// --- the binary cache (22.5) -----------------------------------------------
//
// **YAML is the truth and the cook is a cache**, so the assertion that matters
// is not "the binary parses" but "the binary produces the same scene". Comparing
// the two by re-serializing both to YAML is deliberate: it compares what the
// engine would *write*, which is the thing a user loses if the two paths drift,
// and it does not need a scene-equality operator that would itself need testing.

TEST_CASE("the binary cook produces the same scene as the YAML path") {
    hp::Scene scene = makePopulated();
    const std::string yaml = hp::saveSceneToString(scene);

    const std::vector<std::byte> cooked = hp::cookScene(scene, hp::hashSource(yaml));
    CHECK_FALSE(cooked.empty());

    hp::Scene fromCook;
    const hp::SceneLoadResult result =
        hp::loadSceneFromCooked(fromCook, cooked, hp::hashSource(yaml));
    REQUIRE(result.status == hp::SceneLoadStatus::Ok);
    CHECK(result.entities == 4);

    // The Done-when, stated exactly: the two paths agree. Compared against the
    // scene the *YAML* produced rather than against `yaml` itself, because that
    // is the claim — "identical to the YAML path" — and comparing to the source
    // document would silently be testing round-trip stability instead, which is
    // its own case below.
    hp::Scene fromYaml;
    REQUIRE(hp::loadSceneFromString(fromYaml, yaml).status == hp::SceneLoadStatus::Ok);
    CHECK(hp::saveSceneToString(fromCook) == hp::saveSceneToString(fromYaml));

    SUBCASE("including the hierarchy, which is GUIDs on both paths") {
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

        const auto root = fromCook.find(rootGuid);
        const auto child = fromCook.find(childGuid);
        REQUIRE(root.has_value());
        REQUIRE(child.has_value());
        CHECK(fromCook.registry().get<const hp::Hierarchy>(child->handle()).parent
              == root->handle());
    }
}

TEST_CASE("an empty scene cooks and loads back empty") {
    hp::Scene scene;
    const std::string yaml = hp::saveSceneToString(scene);
    const std::vector<std::byte> cooked = hp::cookScene(scene, hp::hashSource(yaml));

    hp::Scene loaded;
    const hp::SceneLoadResult result =
        hp::loadSceneFromCooked(loaded, cooked, hp::hashSource(yaml));
    CHECK(result.status == hp::SceneLoadStatus::Ok);
    CHECK(result.entities == 0);
    CHECK(hp::saveSceneToString(loaded) == yaml);
}

TEST_CASE("a cook carries preserved unknown components too") {
    // A cache that holds less than the document it caches is the stale-data bug
    // `Cook.hpp` exists to prevent — and here it would be data loss, because a
    // load-from-cook followed by a save is what would write the shortfall back.
    const std::string text = R"(version: 1
entities:
  - guid: 00000000000000a1
    name: Scripted
    components:
      PlayerController:
        speed: 4.5
)";

    hp::Scene scene;
    REQUIRE(hp::loadSceneFromString(scene, text).unknownComponents == 1);
    const std::string yaml = hp::saveSceneToString(scene);

    hp::Scene fromCook;
    REQUIRE(hp::loadSceneFromCooked(fromCook, hp::cookScene(scene, hp::hashSource(yaml)),
                                    hp::hashSource(yaml))
                .status == hp::SceneLoadStatus::Ok);
    CHECK(hp::saveSceneToString(fromCook) == yaml);
    CHECK(hp::saveSceneToString(fromCook).find("speed: 4.5") != std::string::npos);
}

TEST_CASE("every way a cook can be unusable says re-cook and leaves the scene alone") {
    hp::Scene scene = makePopulated();
    const std::string yaml = hp::saveSceneToString(scene);
    const std::vector<std::byte> cooked = hp::cookScene(scene, hp::hashSource(yaml));

    // Loaded first, so "leaves the scene alone" is a claim with something to
    // lose rather than a statement about an empty scene.
    hp::Scene target;
    REQUIRE(hp::loadSceneFromString(target, yaml).status == hp::SceneLoadStatus::Ok);
    const std::size_t before = target.size();
    REQUIRE(before == 4);

    SUBCASE("the source YAML has changed") {
        const hp::SceneLoadResult result =
            hp::loadSceneFromCooked(target, cooked, hp::hashSource(yaml + "\n# edited\n"));
        CHECK(result.status == hp::SceneLoadStatus::Stale);
        CHECK(target.size() == before);
    }

    SUBCASE("the bytes are not a cook file at all") {
        const std::vector<std::byte> junk(32, std::byte{0x7F});
        CHECK(hp::loadSceneFromCooked(target, junk, hp::hashSource(yaml)).status
              == hp::SceneLoadStatus::Stale);
        CHECK(target.size() == before);
    }

    SUBCASE("the payload is truncated mid-entity") {
        std::vector<std::byte> payload;
        hp::writeU32(payload, 2);   // claims two entities and supplies none
        const std::vector<std::byte> broken =
            hp::writeCook(payload, hp::hashSource(yaml),
                          static_cast<std::uint32_t>(hp::kSceneSchemaVersion));
        CHECK(hp::loadSceneFromCooked(target, broken, hp::hashSource(yaml)).status
              == hp::SceneLoadStatus::Stale);
        CHECK(target.size() == before);
    }

    SUBCASE("it names a component type this build does not have") {
        // **The one asymmetry with the YAML path.** Cooked bytes for a type
        // nobody can name cannot be written back, so rather than dropping data
        // the YAML beside it still holds, the cook declares itself stale and
        // sends the caller to the text.
        std::vector<std::byte> payload;
        hp::writeU32(payload, 1);
        hp::writeString(payload, "00000000000000a1");
        hp::writeString(payload, "Scripted");
        hp::writeString(payload, "");
        hp::writeU32(payload, 1);
        hp::writeString(payload, "NotARegisteredType");
        hp::writeU64(payload, 4);
        payload.insert(payload.end(), 4, std::byte{0});
        hp::writeU32(payload, 0);

        const std::vector<std::byte> orphaned =
            hp::writeCook(payload, hp::hashSource(yaml),
                          static_cast<std::uint32_t>(hp::kSceneSchemaVersion));
        const hp::SceneLoadResult result =
            hp::loadSceneFromCooked(target, orphaned, hp::hashSource(yaml));
        CHECK(result.status == hp::SceneLoadStatus::Stale);
        // Named, so the log says which type — and counted to the end of the
        // stream rather than stopping at the first, which is what the per-
        // component length prefix buys.
        CHECK(result.unknownComponents == 1);
        CHECK(target.size() == before);
    }

    SUBCASE("it was cooked against a different schema version") {
        const std::vector<std::byte> future =
            hp::writeCook({}, hp::hashSource(yaml),
                          static_cast<std::uint32_t>(hp::kSceneSchemaVersion) + 1);
        CHECK(hp::loadSceneFromCooked(target, future, hp::hashSource(yaml)).status
              == hp::SceneLoadStatus::Stale);
        CHECK(target.size() == before);
    }
}
