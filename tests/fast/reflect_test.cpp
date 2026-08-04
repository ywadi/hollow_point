// Reflection core (T0053).
//
// Bucket: fast. No device, no subprocesses — registration and lookup are pure
// in-process work. The *interesting* case, whether a gameplay module and the
// engine share one context, needs a real module and lives in the integration
// bucket alongside the rest of the boundary suite.
//
// What these guard is the contract every downstream system will be written
// against: serialization (T0022), the inspector (T0035) and undo/redo (T0065)
// all enumerate properties through this and nothing else. If registration or
// lookup is wrong here, it is wrong in four places later.

#include <doctest/doctest.h>

#include <hp/Reflect.hpp>

#include <entt/meta/meta.hpp>

#include <hp/Guid.hpp>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// entt's `"name"_hs` hashed-string literal. Scoped here rather than in the
// engine header, so consuming code is not forced to adopt entt's literals.
using namespace entt::literals;

namespace {

struct Position {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Health {
    int current = 100;
    int maximum = 100;
    bool invulnerable = false;
};

/// Registration is done once per test run rather than per case: entt's context
/// is process-wide, and re-registering the same type is not what production code
/// does. A local flag rather than a global constructor, so the ordering is
/// visible instead of depending on static init order across TUs.
void registerOnce() {
    static bool done = false;
    if (done) {
        return;
    }
    done = true;

    hp::adoptMetaContext();

    hp::reflect<Position>("Position")
        .property<&Position::x>("x")
        .meta({.min = -1000.0, .max = 1000.0, .tooltip = "world X"})
        .property<&Position::y>("y")
        .property<&Position::z>("z");

    hp::reflect<Health>("Health")
        .property<&Health::current>("current")
        .meta({.min = 0.0, .max = 100.0, .tooltip = "current hit points"})
        .property<&Health::maximum>("maximum")
        .property<&Health::invulnerable>("invulnerable")
        .meta({.tooltip = "ignores all damage", .hidden = true});
}

} // namespace

TEST_CASE("a registered type resolves by name" * doctest::test_suite("reflect")) {
    registerOnce();

    const entt::meta_type type = hp::resolveType("Position");
    REQUIRE_MESSAGE(static_cast<bool>(type),
                    "Position was registered and must resolve; an empty result here usually "
                    "means the meta context was not adopted");
    CHECK(type.info() == entt::type_id<Position>());
}

TEST_CASE("an unregistered name resolves to nothing, and says so" * doctest::test_suite("reflect")) {
    registerOnce();
    // Not an error and not a crash -- callers test the result. The inspector
    // will meet unregistered types routinely and must render them as "no
    // reflection" rather than refusing to draw.
    CHECK_FALSE(static_cast<bool>(hp::resolveType("NoSuchTypeExists")));
    CHECK_FALSE(static_cast<bool>(hp::resolveType(nullptr)));
}

TEST_CASE("properties are enumerable: name, get and set" * doctest::test_suite("reflect")) {
    registerOnce();

    Position p{1.0f, 2.0f, 3.0f};
    const entt::meta_type type = hp::resolveType("Position");
    REQUIRE(static_cast<bool>(type));

    // Enumeration is the whole point: nothing downstream may switch on a
    // concrete type, or adding a component means editing four places.
    int seen = 0;
    for (auto&& [id, data] : type.data()) {
        (void)id;
        (void)data;
        ++seen;
    }
    CHECK(seen == 3);

    entt::meta_any any{entt::forward_as_meta(p)};

    auto y = type.data("y"_hs);
    REQUIRE(static_cast<bool>(y));
    CHECK(y.get(any).cast<float>() == doctest::Approx(2.0f));

    REQUIRE(y.set(any, 42.0f));
    CHECK(p.y == doctest::Approx(42.0f));   // wrote through to the real object
}

TEST_CASE("a set with the wrong type fails instead of corrupting"
          * doctest::test_suite("reflect")) {
    registerOnce();

    Position p{1.0f, 2.0f, 3.0f};
    const entt::meta_type type = hp::resolveType("Position");
    entt::meta_any any{entt::forward_as_meta(p)};

    auto x = type.data("x"_hs);
    REQUIRE(static_cast<bool>(x));

    // The safe fallback 53.4 asks for. A serializer reading a malformed file
    // must be told no, not allowed to reinterpret bytes.
    CHECK_FALSE(x.set(any, std::string{"not a float"}));
    CHECK(p.x == doctest::Approx(1.0f));    // unchanged
}

TEST_CASE("property metadata survives registration" * doctest::test_suite("reflect")) {
    registerOnce();

    const entt::meta_type type = hp::resolveType("Position");
    auto x = type.data("x"_hs);
    REQUIRE(static_cast<bool>(x));

    const auto* meta = static_cast<const hp::PropertyMeta*>(x.custom());
    REQUIRE_MESSAGE(meta != nullptr, "PropertyMeta attached to 'x' should be retrievable");
    CHECK(meta->min == doctest::Approx(-1000.0));
    CHECK(meta->max == doctest::Approx(1000.0));
    CHECK(std::strcmp(meta->tooltip, "world X") == 0);
    CHECK_FALSE(meta->hidden);
}

TEST_CASE("a property without metadata is not a failure" * doctest::test_suite("reflect")) {
    registerOnce();
    // Most properties will carry none. The inspector must fall back to defaults
    // rather than treating absence as an error.
    const entt::meta_type type = hp::resolveType("Position");
    auto z = type.data("z"_hs);
    REQUIRE(static_cast<bool>(z));
    CHECK(static_cast<const hp::PropertyMeta*>(z.custom()) == nullptr);
}

TEST_CASE("hidden and read-only are carried, not interpreted" * doctest::test_suite("reflect")) {
    registerOnce();
    const entt::meta_type type = hp::resolveType("Health");
    auto inv = type.data("invulnerable"_hs);
    REQUIRE(static_cast<bool>(inv));

    const auto* meta = static_cast<const hp::PropertyMeta*>(inv.custom());
    REQUIRE(meta != nullptr);
    CHECK(meta->hidden);
    // Hiding is a UI decision, so a hidden property is still fully readable and
    // writable through reflection -- serialization must not lose it.
    Health h{};
    entt::meta_any any{entt::forward_as_meta(h)};
    REQUIRE(inv.set(any, true));
    CHECK(h.invulnerable);
}

TEST_CASE("identity is the name, not the type index" * doctest::test_suite("reflect")) {
    registerOnce();
    // The rule the whole subsystem rests on (T0095): entt::type_index is a
    // per-module sequential number that differs across the boundary and must
    // never be serialised. The name hash is stable, and is what resolve uses.
    const entt::meta_type type = hp::resolveType("Position");
    REQUIRE(static_cast<bool>(type));
    CHECK(type.id() == entt::hashed_string{"Position"}.value());
}

// --- 53.6: nested structs, enums, containers, GUID references ----------------
//
// These are the cases where a reflection layer usually turns out to be a
// property-list-of-scalars in disguise. A scene is not scalars: a Transform
// holds a nested vector, an enemy holds a state enum, an inventory holds a
// container, and anything referring to an asset holds a GUID. If any of these
// does not round-trip, serialization (T0022) and the inspector (T0035) grow a
// special case each, which is the outcome this ticket exists to prevent.

namespace {

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

enum class Team : std::uint8_t { Neutral = 0, Ally = 1, Hostile = 2 };

struct AssetRef {
    hp::Guid guid;
};

struct Actor {
    Vec3 position;              // nested struct
    Team team = Team::Neutral;  // enum
    std::vector<int> tags;      // container
    AssetRef mesh;              // GUID-bearing nested struct
};

void registerCompositeOnce() {
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    hp::adoptMetaContext();

    hp::reflect<Vec3>("Vec3")
        .property<&Vec3::x>("x")
        .property<&Vec3::y>("y")
        .property<&Vec3::z>("z");

    hp::reflect<Team>("Team")
        .value<Team::Neutral>("Neutral")
        .value<Team::Ally>("Ally")
        .value<Team::Hostile>("Hostile");

    // Guid keeps its value private behind a getter, which is the house style --
    // so it registers read-only rather than as a member pointer.
    hp::reflect<hp::Guid>("Guid").readOnlyProperty<&hp::Guid::value>("value");
    hp::reflect<AssetRef>("AssetRef").property<&AssetRef::guid>("guid");

    hp::reflect<Actor>("Actor")
        .property<&Actor::position>("position")
        .property<&Actor::team>("team")
        .property<&Actor::tags>("tags")
        .property<&Actor::mesh>("mesh");
}

} // namespace

TEST_CASE("a nested struct property is reflected, not flattened"
          * doctest::test_suite("reflect")) {
    registerCompositeOnce();

    Actor actor;
    actor.position = {1.0f, 2.0f, 3.0f};

    const entt::meta_type type = hp::resolveType("Actor");
    REQUIRE(static_cast<bool>(type));
    entt::meta_any any{entt::forward_as_meta(actor)};

    auto position = type.data("position"_hs);
    REQUIRE(static_cast<bool>(position));

    // The nested value is reachable *as a reflected type*, so a serializer can
    // recurse rather than needing to know what a Vec3 is.
    entt::meta_any nested = position.get(any);
    REQUIRE(static_cast<bool>(nested));
    const entt::meta_type nested_type = nested.type();
    CHECK(nested_type.id() == entt::hashed_string{"Vec3"}.value());

    auto y = nested_type.data("y"_hs);
    REQUIRE(static_cast<bool>(y));
    CHECK(y.get(nested).cast<float>() == doctest::Approx(2.0f));
}

TEST_CASE("an enum's values are named, not bare integers" * doctest::test_suite("reflect")) {
    registerCompositeOnce();

    const entt::meta_type team = hp::resolveType("Team");
    REQUIRE(static_cast<bool>(team));
    CHECK(team.is_enum());

    // Named lookup is what lets a save file say `team: Hostile` rather than
    // `team: 2`, and lets the inspector render a combo box.
    auto hostile = team.data("Hostile"_hs);
    REQUIRE(static_cast<bool>(hostile));
    CHECK(hostile.get({}).cast<Team>() == Team::Hostile);

    int named = 0;
    for (auto&& [id, data] : team.data()) {
        (void)id;
        (void)data;
        ++named;
    }
    CHECK(named == 3);
}

TEST_CASE("a container property is reflected as a sequence" * doctest::test_suite("reflect")) {
    registerCompositeOnce();

    Actor actor;
    actor.tags = {10, 20, 30};

    const entt::meta_type type = hp::resolveType("Actor");
    entt::meta_any any{entt::forward_as_meta(actor)};
    auto tags = type.data("tags"_hs);
    REQUIRE(static_cast<bool>(tags));

    entt::meta_any value = tags.get(any);
    REQUIRE(static_cast<bool>(value));

    // Sequence access is what lets a serializer write a list without knowing the
    // element type at compile time.
    auto view = value.as_sequence_container();
    REQUIRE_MESSAGE(static_cast<bool>(view),
                    "std::vector should be reachable as a sequence container");
    REQUIRE(view.size() == 3);
    CHECK(view[1].cast<int>() == 20);
}

TEST_CASE("an asset GUID reference survives reflection" * doctest::test_suite("reflect")) {
    registerCompositeOnce();

    Actor actor;
    actor.mesh.guid = hp::Guid{0x0123456789abcdefULL};

    const entt::meta_type type = hp::resolveType("Actor");
    entt::meta_any any{entt::forward_as_meta(actor)};

    auto mesh = type.data("mesh"_hs);
    REQUIRE(static_cast<bool>(mesh));
    entt::meta_any ref = mesh.get(any);
    REQUIRE(static_cast<bool>(ref));

    auto guid = ref.type().data("guid"_hs);
    REQUIRE(static_cast<bool>(guid));
    entt::meta_any guid_any = guid.get(ref);
    REQUIRE(static_cast<bool>(guid_any));

    // A GUID is the only stable way to name an asset across a save (T0016), so
    // it has to survive the round trip exactly -- not as a truncated double or a
    // re-hashed string.
    auto raw = guid_any.type().data("value"_hs);
    REQUIRE(static_cast<bool>(raw));
    CHECK(raw.get(guid_any).cast<std::uint64_t>() == 0x0123456789abcdefULL);
}

TEST_CASE("enumerated properties carry their names, not just their ids") {
    // **Found while starting T0020.3, and it had been wrong since T0053.**
    //
    // `TypeBuilder::property` passed the name to entt only as a hashed id.
    // Everything that *queries* a property by id worked perfectly -- which is
    // every test that existed, and why nothing caught it -- while everything
    // that *enumerates* a type got a null name for every field.
    //
    // That breaks the three T0053 consumers which walk a type rather than
    // query it: serialization cannot write a readable key, the inspector
    // cannot label a field, and undo/redo cannot say what changed. Measured
    // before the fix: all three properties of `Transform` reported null.
    //
    // The id is unchanged -- it is still the hash of the same string -- so this
    // is not a data-format change. Only the name became available.
    hp::adoptMetaContext();
    hp::reflect<Actor>("Actor").property<&Actor::mesh>("mesh");

    const entt::meta_type type = hp::resolveType("Actor");
    REQUIRE(static_cast<bool>(type));

    auto mesh = type.data("mesh"_hs);
    REQUIRE(static_cast<bool>(mesh));
    REQUIRE(mesh.name() != nullptr);
    CHECK(std::string(mesh.name()) == "mesh");

    // The identity is still the hash of the name, so lookups by id are
    // untouched and enumeration now yields usable names -- both checked
    // together, because the point is that the two agree.
    bool sawMesh = false;
    for (auto&& [id, data] : type.data()) {
        if (data.name() != nullptr && std::string(data.name()) == "mesh") {
            sawMesh = true;
            CHECK(id == entt::hashed_string{"mesh"}.value());
        }
    }
    CHECK(sawMesh);
}
