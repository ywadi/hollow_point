// The scene parse step (T0028.1): entities in, an explicit draw list out.
//
// Bucket: fast. The parse step deliberately takes no device, no asset pool and
// no camera, which is exactly what makes it testable here -- these are the
// filtering rules, and none of them needs a GPU to be wrong.

#include <doctest/doctest.h>

#include <hp/DrawSubmission.hpp>
#include <hp/Scene.hpp>

#include <cstdint>

namespace {

/// An entity with a mesh renderer at a known position.
///
/// Goes through `setLocalTransform` + `propagateTransforms` rather than writing
/// `WorldTransform` directly, because that is the only write guaranteed to reach
/// the world transform -- and a test that bypassed it would pass while the real
/// path was broken.
hp::Entity makeDrawable(hp::Scene& scene, const char* name, hp::Guid mesh, hp::Guid material = {}) {
    hp::Entity entity = scene.create(name);
    hp::MeshRenderer renderer;
    renderer.mesh = mesh;
    renderer.material = material;
    entity.add<hp::MeshRenderer>(renderer);
    return entity;
}

hp::Guid meshGuid(std::uint64_t n) {
    return hp::Guid(n);
}

} // namespace

TEST_CASE("an entity with a world transform and a mesh is collected") {
    hp::Scene scene;
    const hp::Entity entity = makeDrawable(scene, "cube", meshGuid(1));
    scene.propagateTransforms();

    hp::DrawParseStats stats;
    const hp::DrawList list = hp::parseScene(scene, &stats);

    REQUIRE(list.size() == 1);
    CHECK(list[0].entity == entity.raw());
    CHECK(list[0].mesh == meshGuid(1));
    CHECK(stats.considered == 1);
    CHECK(stats.drawn == 1);
    CHECK(stats.withoutMesh == 0);
}

TEST_CASE("an entity with no mesh renderer is not collected") {
    hp::Scene scene;
    scene.create("empty");
    scene.propagateTransforms();

    hp::DrawParseStats stats;
    const hp::DrawList list = hp::parseScene(scene, &stats);

    CHECK(list.empty());
    // Not merely absent from the list: it must not even be *considered*, or the
    // "why did nothing draw" report counts every entity in the scene.
    CHECK(stats.considered == 0);
}

TEST_CASE("an unset mesh GUID is dropped, and counted rather than treated as an error") {
    // `MeshRenderer` documents a default mesh GUID as "nothing to draw", which is
    // a legitimate state -- an entity can exist before its asset is assigned. The
    // distinction that matters is dropped-and-counted versus dropped-silently:
    // the count is what lets an empty frame explain itself.
    hp::Scene scene;
    makeDrawable(scene, "unassigned", hp::Guid{});
    makeDrawable(scene, "assigned", meshGuid(7));
    scene.propagateTransforms();

    hp::DrawParseStats stats;
    const hp::DrawList list = hp::parseScene(scene, &stats);

    REQUIRE(list.size() == 1);
    CHECK(list[0].mesh == meshGuid(7));
    CHECK(stats.considered == 2);
    CHECK(stats.drawn == 1);
    CHECK(stats.withoutMesh == 1);
}

TEST_CASE("an unset material is kept, because default means the fallback material") {
    // The asymmetry with the mesh case is deliberate and is 28.2: a mesh with no
    // material must still be visible rather than silently absent, so a default
    // material GUID is carried through instead of dropping the item.
    hp::Scene scene;
    makeDrawable(scene, "no material", meshGuid(3), hp::Guid{});
    scene.propagateTransforms();

    const hp::DrawList list = hp::parseScene(scene);

    REQUIRE(list.size() == 1);
    CHECK(list[0].mesh == meshGuid(3));
    CHECK_FALSE(list[0].material.isValid());
}

TEST_CASE("the world matrix is copied, not the local one") {
    // The reason parsing filters on `WorldTransform` rather than `Transform`:
    // a child's local position is meaningless to a draw. If this ever regresses,
    // every parented entity renders at its local offset from the origin, which
    // reads as a broken scene rather than as a parse bug.
    hp::Scene scene;

    hp::Entity parent = scene.create("parent");
    hp::Transform parentTransform;
    parentTransform.position = hp::float3(10.0F, 0.0F, 0.0F);
    scene.setLocalTransform(parent, parentTransform);

    hp::Entity child = makeDrawable(scene, "child", meshGuid(2));
    scene.setParent(child, parent);
    hp::Transform childTransform;
    childTransform.position = hp::float3(5.0F, 0.0F, 0.0F);
    scene.setLocalTransform(child, childTransform);

    scene.propagateTransforms();

    const hp::DrawList list = hp::parseScene(scene);

    REQUIRE(list.size() == 1);
    // 10 + 5, not 5. Row-major, so translation is row 3.
    CHECK(list[0].world.m30 == doctest::Approx(15.0F));
}

TEST_CASE("stats are optional") {
    hp::Scene scene;
    makeDrawable(scene, "cube", meshGuid(1));
    scene.propagateTransforms();

    // Passing no stats must not crash -- the pointer is genuinely optional, and
    // the null check is easy to write and easy to forget.
    const hp::DrawList list = hp::parseScene(scene);
    CHECK(list.size() == 1);
}

TEST_CASE("an empty scene parses to an empty list rather than failing") {
    hp::Scene scene;
    scene.propagateTransforms();

    hp::DrawParseStats stats;
    const hp::DrawList list = hp::parseScene(scene, &stats);

    CHECK(list.empty());
    CHECK(stats.considered == 0);
    CHECK(stats.drawn == 0);
}
