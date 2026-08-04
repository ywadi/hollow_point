// Scene, entities, hierarchy and cloning (T0021).

#include <doctest/doctest.h>

#include <hp/Scene.hpp>

#include <algorithm>
#include <string>

namespace {

/// A component the engine knows nothing about, standing in for one a gameplay
/// module would define.
struct Health {
    int current = 100;
};

/// Registered lazily so the "unregistered types are dropped by clone" case can
/// observe both states.
void registerHealth() {
    static bool done = false;
    if (!done) {
        done = true;
        hp::registerComponent<Health>("Health").property<&Health::current>("current");
    }
}

} // namespace

TEST_CASE("a new entity carries identity, tag, transform and hierarchy") {
    hp::Scene scene;
    const auto entity = scene.create("Door_01");

    CHECK(entity.valid());
    CHECK(scene.size() == 1);
    CHECK(entity.has<hp::Id>());
    CHECK(entity.has<hp::Tag>());
    CHECK(entity.has<hp::Transform>());
    CHECK(entity.has<hp::Hierarchy>());

    auto handle = entity;
    CHECK(handle.get<hp::Tag>().name == "Door_01");
    // Parenthesised: doctest's expression decomposition is ambiguous against
    // entt::null's own comparison operators.
    CHECK((handle.get<hp::Hierarchy>().parent == entt::null));

    // The default transform is identity, not zeroed -- a zero scale would make
    // every newly created entity invisible.
    const auto& transform = handle.get<hp::Transform>();
    CHECK(transform.position.x == doctest::Approx(0.0F));
    CHECK(transform.scale.x == doctest::Approx(1.0F));
    CHECK(transform.rotation.q.w == doctest::Approx(1.0F));
}

TEST_CASE("a GUID resolves back to its entity, and stops resolving once destroyed") {
    hp::Scene scene;
    auto entity = scene.create();
    const auto guid = entity.guid();

    CHECK(guid.isValid());
    REQUIRE(scene.find(guid).has_value());
    CHECK(*scene.find(guid) == entity);

    scene.destroy(entity);
    CHECK_FALSE(entity.valid());
    CHECK_FALSE(scene.find(guid).has_value());
    CHECK(scene.size() == 0);
}

TEST_CASE("identity survives everything the handle does not") {
    hp::Scene scene;
    auto first = scene.create("first");
    const auto guid = first.guid();
    const auto raw = first.raw();

    scene.destroy(first);
    auto second = scene.create("second");

    // entt reuses the slot, which is exactly why nothing persists a raw entity:
    // the new entity may occupy the old one's index while being a different
    // thing entirely. The GUID is what distinguishes them.
    CHECK(second.guid() != guid);
    if (second.raw() == raw) {
        MESSAGE("slot reused, as expected -- this is why identity is the GUID");
    }
}

TEST_CASE("createWithGuid refuses a duplicate rather than making find ambiguous") {
    hp::Scene scene;
    const auto guid = hp::Guid::generate();
    const auto first = scene.createWithGuid(guid, "first");
    const auto second = scene.createWithGuid(guid, "second");

    CHECK(scene.size() == 1);
    CHECK(first == second);
}

TEST_CASE("components add, read and remove") {
    registerHealth();
    hp::Scene scene;
    auto entity = scene.create();

    CHECK_FALSE(entity.has<Health>());
    CHECK(entity.tryGet<Health>() == nullptr);

    entity.add<Health>(Health{42});
    REQUIRE(entity.has<Health>());
    CHECK(entity.get<Health>().current == 42);

    // add replaces rather than asserting, so a second add is well defined.
    entity.add<Health>(Health{7});
    CHECK(entity.get<Health>().current == 7);

    CHECK(entity.remove<Health>() == 1);
    CHECK_FALSE(entity.has<Health>());
    CHECK(entity.remove<Health>() == 0);
}

TEST_CASE("parenting maintains both sides of the link") {
    hp::Scene scene;
    auto parent = scene.create("parent");
    auto child = scene.create("child");

    REQUIRE(scene.setParent(child, parent));
    CHECK(child.get<hp::Hierarchy>().parent == parent.raw());
    CHECK(parent.get<hp::Hierarchy>().children.size() == 1);
    CHECK(parent.get<hp::Hierarchy>().children.front() == child.raw());

    // Reparenting to the same parent must not duplicate the child entry.
    REQUIRE(scene.setParent(child, parent));
    CHECK(parent.get<hp::Hierarchy>().children.size() == 1);

    // Unparenting detaches both ends.
    REQUIRE(scene.setParent(child, hp::Entity{}));
    CHECK((child.get<hp::Hierarchy>().parent == entt::null));
    CHECK(parent.get<hp::Hierarchy>().children.empty());
}

TEST_CASE("a cycle is refused, because every traversal assumes termination") {
    hp::Scene scene;
    auto grandparent = scene.create("grandparent");
    auto parent = scene.create("parent");
    auto child = scene.create("child");

    REQUIRE(scene.setParent(parent, grandparent));
    REQUIRE(scene.setParent(child, parent));

    CHECK_FALSE(scene.setParent(grandparent, child));
    CHECK_FALSE(scene.setParent(parent, parent));
    CHECK((grandparent.get<hp::Hierarchy>().parent == entt::null));
}

TEST_CASE("destroying a parent destroys its descendants") {
    hp::Scene scene;
    auto root = scene.create("root");
    auto middle = scene.create("middle");
    auto leaf = scene.create("leaf");
    REQUIRE(scene.setParent(middle, root));
    REQUIRE(scene.setParent(leaf, middle));
    REQUIRE(scene.size() == 3);

    const auto leafGuid = leaf.guid();
    scene.destroy(root);

    CHECK(scene.size() == 0);
    CHECK_FALSE(scene.find(leafGuid).has_value());
    CHECK(scene.roots().empty());
}

TEST_CASE("destroying a child leaves the parent's list correct") {
    hp::Scene scene;
    auto parent = scene.create("parent");
    auto first = scene.create("first");
    auto second = scene.create("second");
    REQUIRE(scene.setParent(first, parent));
    REQUIRE(scene.setParent(second, parent));

    scene.destroy(first);

    const auto& children = parent.get<hp::Hierarchy>().children;
    CHECK(children.size() == 1);
    CHECK(children.front() == second.raw());
    CHECK(scene.size() == 2);
}

TEST_CASE("roots lists exactly the unparented entities") {
    hp::Scene scene;
    auto a = scene.create("a");
    auto b = scene.create("b");
    auto c = scene.create("c");
    REQUIRE(scene.setParent(c, a));

    const auto roots = scene.roots();
    CHECK(roots.size() == 2);
    CHECK(std::find(roots.begin(), roots.end(), a) != roots.end());
    CHECK(std::find(roots.begin(), roots.end(), b) != roots.end());
}

TEST_CASE("a preserving clone keeps every GUID, so references still resolve") {
    registerHealth();
    hp::Scene scene;
    auto entity = scene.create("original");
    entity.add<Health>(Health{55});
    const auto guid = entity.guid();

    const hp::Scene copy = scene.clone(hp::CloneIds::Preserve);

    CHECK(copy.size() == 1);
    auto found = copy.find(guid);
    REQUIRE(found.has_value());
    CHECK(found->guid() == guid);
    CHECK(found->get<hp::Tag>().name == "original");
    REQUIRE(found->has<Health>());
    CHECK(found->get<Health>().current == 55);

    // Independent storage: mutating the clone must not touch the source.
    found->get<Health>().current = 1;
    CHECK(entity.get<Health>().current == 55);
}

TEST_CASE("a regenerating clone issues fresh GUIDs and remaps the hierarchy") {
    hp::Scene scene;
    auto parent = scene.create("parent");
    auto child = scene.create("child");
    REQUIRE(scene.setParent(child, parent));
    const auto parentGuid = parent.guid();

    hp::Scene copy = scene.clone(hp::CloneIds::Regenerate);

    CHECK(copy.size() == 2);
    CHECK_FALSE(copy.find(parentGuid).has_value());

    const auto roots = copy.roots();
    REQUIRE(roots.size() == 1);
    auto copiedParent = roots.front();
    CHECK(copiedParent.get<hp::Tag>().name == "parent");

    const auto& children = copiedParent.get<hp::Hierarchy>().children;
    REQUIRE(children.size() == 1);

    // The remap must point inside the *clone*. If it still held the source
    // registry's entities this would address an unrelated slot, or none.
    hp::Entity copiedChild{copy, children.front()};
    CHECK(copiedChild.valid());
    CHECK(copiedChild.get<hp::Tag>().name == "child");
    CHECK(copiedChild.get<hp::Hierarchy>().parent == copiedParent.raw());
}

TEST_CASE("an unregistered component is silently dropped by clone") {
    // Documenting the failure mode rather than pretending it does not exist:
    // clone copies exactly the types passed to registerComponent, so a type
    // registered nowhere survives in the source and vanishes in the copy.
    struct Unregistered {
        int value = 9;
    };

    hp::Scene scene;
    auto entity = scene.create();
    entity.add<Unregistered>();
    const auto guid = entity.guid();

    const hp::Scene copy = scene.clone(hp::CloneIds::Preserve);
    auto found = copy.find(guid);
    REQUIRE(found.has_value());
    CHECK(found->has<hp::Transform>());
    CHECK_FALSE(found->has<Unregistered>());
}

TEST_CASE("core components are reflected under stable names") {
    hp::registerCoreComponents();

    // Identity is the name, never entt::type_index -- T0095 measured that index
    // to be a per-module number with no meaning across the module boundary.
    CHECK(static_cast<bool>(hp::resolveType("Transform")));
    CHECK(static_cast<bool>(hp::resolveType("Tag")));
    CHECK(static_cast<bool>(hp::resolveType("MeshRenderer")));
    CHECK(static_cast<bool>(hp::resolveType("Camera")));
}
