// Frame render targets (T0046).
//
// Bucket: fast, so there is no device here. These cover the declaration,
// lookup and debounce logic, which is all of `FrameTargets` that does not need
// one — and which is where the mistakes that produce a null view three passes
// later actually live. The device path is exercised separately; see the
// ticket's "not verified here" note.

#include <doctest/doctest.h>

#include <hp/FrameTargets.hpp>

TEST_CASE("a target set starts empty and hands out nothing") {
    hp::FrameTargets targets;

    CHECK_FALSE(targets.ready());
    CHECK(targets.width() == 0);
    CHECK(targets.height() == 0);
    CHECK(targets.memoryBytes() == 0);
    CHECK(targets.declared().empty());

    // Every lookup on an uncreated set is null rather than undefined. A pass
    // that runs before creation gets a null it can check, not a dangling view.
    CHECK(targets.renderTarget("colour") == nullptr);
    CHECK(targets.depthStencil("depth") == nullptr);
    CHECK(targets.shaderResource("colour") == nullptr);
}

TEST_CASE("declarations are kept in order, and duplicates are refused") {
    hp::FrameTargets targets;
    targets.declare({"colour", hp::TargetFormat::ColourHDR, 1.0F});
    targets.declare({"depth", hp::TargetFormat::Depth, 1.0F});
    targets.declare({"bloom", hp::TargetFormat::ColourHDR, 0.5F});

    REQUIRE(targets.declared().size() == 3);
    CHECK(targets.declared()[0].name == "colour");
    CHECK(targets.declared()[1].name == "depth");
    CHECK(targets.declared()[2].name == "bloom");
    CHECK(targets.declared()[2].scale == doctest::Approx(0.5F));

    // A second declaration under the same name keeps the first. Silently
    // replacing it would mean a pass that looked the name up got a target with
    // someone else's format, which fails as a rendering artefact rather than an
    // error.
    targets.declare({"colour", hp::TargetFormat::Depth, 0.25F});
    REQUIRE(targets.declared().size() == 3);
    CHECK(targets.declared()[0].format == hp::TargetFormat::ColourHDR);
    CHECK(targets.declared()[0].scale == doctest::Approx(1.0F));
}

TEST_CASE("an unknown name is null rather than a crash") {
    hp::FrameTargets targets;
    targets.declare({"colour", hp::TargetFormat::Colour, 1.0F});

    CHECK(targets.renderTarget("typo") == nullptr);
    CHECK(targets.depthStencil("typo") == nullptr);
    CHECK(targets.shaderResource("typo") == nullptr);
}

TEST_CASE("creating without a device fails rather than half-succeeding") {
    hp::FrameTargets targets;
    targets.declare({"colour", hp::TargetFormat::Colour, 1.0F});

    CHECK_FALSE(targets.create(nullptr, 1280, 720));
    CHECK_FALSE(targets.ready());
    CHECK(targets.memoryBytes() == 0);

    // The requested size is still recorded, so a later create with a real device
    // does not need it repeated.
    CHECK(targets.width() == 1280);
    CHECK(targets.height() == 720);
}

TEST_CASE("a zero or negative size is refused") {
    hp::FrameTargets targets;
    targets.declare({"colour", hp::TargetFormat::Colour, 1.0F});

    CHECK_FALSE(targets.create(nullptr, 0, 720));
    CHECK_FALSE(targets.create(nullptr, 1280, -1));
    CHECK_FALSE(targets.ready());
}

TEST_CASE("release is idempotent") {
    hp::FrameTargets targets;
    targets.declare({"colour", hp::TargetFormat::Colour, 1.0F});

    targets.release();
    targets.release();
    CHECK_FALSE(targets.ready());
    // Declarations survive a release: the set can be rebuilt at a new size
    // without every caller re-declaring.
    CHECK(targets.declared().size() == 1);
}

TEST_CASE("a set is movable, so it can live in a pimpl") {
    hp::FrameTargets targets;
    targets.declare({"colour", hp::TargetFormat::ColourHDR, 1.0F});

    hp::FrameTargets moved = std::move(targets);
    CHECK(moved.declared().size() == 1);
    CHECK(moved.declared()[0].name == "colour");
}
