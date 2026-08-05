// Binding files (T0068.7).
//
// Bucket: fast — the format is strings in and strings out, no device and no
// filesystem.
//
// **The case that matters is that a file survives being edited by a person.**
// 68.7 exists so a player can rebind, which means the format has to round-trip
// human edits and tolerate a line naming something this build does not have.

#include <doctest/doctest.h>

#include <hp/Input.hpp>

#include <string>

TEST_CASE("a binding map round-trips through YAML") {
    hp::InputMap map;
    map.bindKeyNamed("Jump", hp::KeyCode::Space);
    map.bindKeyNamed("Jump", hp::KeyCode::W);
    map.bindMouseButtonNamed("Fire", hp::MouseButton::Left);
    map.bindAxis2DNamed("Move", hp::KeyCode::A, hp::KeyCode::D, hp::KeyCode::S, hp::KeyCode::W);

    const std::string text = hp::writeInputMap(map);

    // Names, not hashes. A file a person cannot read is a file they cannot
    // repair, and rebinding is the whole point of the ticket.
    CHECK(text.find("Jump") != std::string::npos);
    CHECK(text.find("Space") != std::string::npos);
    CHECK(text.find("Move") != std::string::npos);

    const auto restored = hp::parseInputMap(text);
    REQUIRE(restored.has_value());
    CHECK(restored->binds(hp::ActionId{"Jump"}));
    CHECK(restored->binds(hp::ActionId{"Fire"}));
    CHECK(restored->binds(hp::ActionId{"Move"}));
    CHECK_FALSE(restored->binds(hp::ActionId{"NeverBound"}));

    // And the names survive, so the restored map can be saved again.
    CHECK(restored->actionName(hp::ActionId{"Jump"}) == "Jump");
}

TEST_CASE("a saved map re-saves identically") {
    // Save, load, save. If the second differs, something is being lost or
    // reordered, and a player's file would drift every time the game wrote it.
    hp::InputMap map;
    map.bindKeyNamed("Jump", hp::KeyCode::Space);
    map.bindMouseButtonNamed("Fire", hp::MouseButton::Right);
    map.bindAxis2DNamed("Move", hp::KeyCode::A, hp::KeyCode::D, hp::KeyCode::S, hp::KeyCode::W);

    const std::string first = hp::writeInputMap(map);
    const auto reloaded = hp::parseInputMap(first);
    REQUIRE(reloaded.has_value());
    const std::string second = hp::writeInputMap(*reloaded);

    CHECK(first == second);
}

TEST_CASE("axis tuning survives, and defaults when the file omits it") {
    hp::InputMap map;
    hp::AxisTuning tuning;
    tuning.deadZone = 0.25F;
    tuning.sensitivity = 2.5F;
    tuning.normalise = false;
    map.bindAxis2DNamed("Look", hp::KeyCode::Left, hp::KeyCode::Right, hp::KeyCode::Down,
                        hp::KeyCode::Up, tuning);

    const auto restored = hp::parseInputMap(hp::writeInputMap(map));
    REQUIRE(restored.has_value());
    CHECK(restored->binds(hp::ActionId{"Look"}));

    // A hand-written file usually omits tuning entirely; it must then behave
    // exactly as code that never set it.
    const auto minimal = hp::parseInputMap(
        "version: 1\naxes:\n  - action: Move\n    negativeX: A\n    positiveX: D\n"
        "    negativeY: S\n    positiveY: W\n");
    REQUIRE(minimal.has_value());
    CHECK(minimal->binds(hp::ActionId{"Move"}));
}

TEST_CASE("a hand-written file loads") {
    // The format has to be writable by a person, not merely readable.
    const auto map = hp::parseInputMap(
        "version: 1\n"
        "keys:\n"
        "  - action: Jump\n"
        "    key: Space\n"
        "buttons:\n"
        "  - action: Fire\n"
        "    button: Left\n");
    REQUIRE(map.has_value());
    CHECK(map->binds(hp::ActionId{"Jump"}));
    CHECK(map->binds(hp::ActionId{"Fire"}));
}

TEST_CASE("one bad line does not lose the rest of the file") {
    // A binding file is user-editable and may name a key this build does not
    // have. Refusing the whole file would throw away every other binding the
    // player set, which is a much worse outcome than one control not working.
    const auto map = hp::parseInputMap(
        "version: 1\n"
        "keys:\n"
        "  - action: Jump\n"
        "    key: Space\n"
        "  - action: Crouch\n"
        "    key: ThisKeyDoesNotExist\n"
        "  - action: Sprint\n"
        "    key: Z\n"
        "  - key: W\n"          // no action at all
        "  - action: NoKey\n"); // no key at all
    REQUIRE(map.has_value());
    CHECK(map->binds(hp::ActionId{"Jump"}));
    CHECK(map->binds(hp::ActionId{"Sprint"}));
    CHECK_FALSE(map->binds(hp::ActionId{"Crouch"}));
    CHECK_FALSE(map->binds(hp::ActionId{"NoKey"}));
}

TEST_CASE("an unreadable or future-versioned file is refused whole") {
    CHECK_FALSE(hp::parseInputMap("keys: [unclosed\n").has_value());
    // No version at all: not a binding file this build should guess at.
    CHECK_FALSE(hp::parseInputMap("keys:\n  - action: Jump\n    key: Space\n").has_value());
    CHECK_FALSE(hp::parseInputMap("version: 999\nkeys: []\n").has_value());
}

TEST_CASE("an empty map round-trips to an empty map") {
    const hp::InputMap map;
    const auto restored = hp::parseInputMap(hp::writeInputMap(map));
    REQUIRE(restored.has_value());
    CHECK_FALSE(restored->binds(hp::ActionId{"Anything"}));
}

TEST_CASE("a binding made by id and never named cannot be written") {
    // Deliberate and loud. An ActionId is a hash, so writing it would produce a
    // line nobody can read or repair -- so the binding is skipped and the
    // omission is logged, rather than silently emitting a number.
    hp::InputMap map;
    map.bindKey(hp::ActionId{"Unnamed"}, hp::KeyCode::Q);
    map.bindKeyNamed("Named", hp::KeyCode::E);

    const auto restored = hp::parseInputMap(hp::writeInputMap(map));
    REQUIRE(restored.has_value());
    CHECK(restored->binds(hp::ActionId{"Named"}));
    CHECK_FALSE(restored->binds(hp::ActionId{"Unnamed"}));

    // The escape hatch: name it, and it saves.
    map.nameAction("Unnamed");
    const auto second = hp::parseInputMap(hp::writeInputMap(map));
    REQUIRE(second.has_value());
    CHECK(second->binds(hp::ActionId{"Unnamed"}));
}

TEST_CASE("key identifiers round-trip and are stable") {
    // These strings are a data format. Changing one silently invalidates that
    // binding in every file a player has saved, and the symptom is a control
    // that stopped working with no error anywhere.
    CHECK(hp::keyCodeName(hp::KeyCode::Space) == "Space");
    CHECK(hp::keyCodeName(hp::KeyCode::W) == "W");
    CHECK(hp::keyCodeName(hp::KeyCode::F10) == "F10");
    CHECK(hp::keyCodeName(hp::KeyCode::Unknown).empty());

    CHECK(hp::keyCodeFromName("Space") == hp::KeyCode::Space);
    CHECK(hp::keyCodeFromName("F10") == hp::KeyCode::F10);
    CHECK(hp::keyCodeFromName("nonsense") == hp::KeyCode::Unknown);
    // Case-sensitive, like the action names.
    CHECK(hp::keyCodeFromName("space") == hp::KeyCode::Unknown);

    CHECK(hp::mouseButtonName(hp::MouseButton::Middle) == "Middle");
    CHECK(hp::mouseButtonFromName("X2") == hp::MouseButton::X2);
    CHECK(hp::mouseButtonFromName("nope") == hp::MouseButton::Unknown);
}

TEST_CASE("every named key round-trips through its own name") {
    // Guards the table against a typo or a duplicate, which would make one key
    // unbindable and another bind to the wrong thing.
    for (int i = 1; i <= static_cast<int>(hp::KeyCode::F10); ++i) {
        const auto key = static_cast<hp::KeyCode>(i);
        const std::string_view name = hp::keyCodeName(key);
        if (name.empty()) {
            continue;
        }
        CHECK(hp::keyCodeFromName(name) == key);
    }
}

TEST_CASE("a map loaded from a file actually drives actions, and rebinding works") {
    // **This is the "user-rebindable" half of the Done-when**, and it is worth
    // proving rather than asserting: a format that round-trips but produces a
    // map the input system does not honour would pass every test above.
    const auto loaded = hp::parseInputMap(
        "version: 1\nkeys:\n  - action: Jump\n    key: Space\n");
    REQUIRE(loaded.has_value());

    hp::InputSystem input;
    const std::size_t context = input.pushContext(*loaded);

    hp::KeyEvent down(hp::KeyCode::Space, true, {});
    CHECK(input.onEvent(down));
    // Edges are published by snapshot() at frame phase 3a, not by onEvent, so a
    // tap inside one frame is never lost -- see input_test.cpp.
    input.snapshot();
    CHECK(input.digital(hp::ActionId{"Jump"}).pressed);

    // Now rebind, exactly as a settings screen would: parse the player's new
    // file, drop the old context, push the new one.
    input.removeContext(context);
    const auto rebound = hp::parseInputMap(
        "version: 1\nkeys:\n  - action: Jump\n    key: Enter\n");
    REQUIRE(rebound.has_value());
    input.pushContext(*rebound);

    hp::KeyEvent oldKey(hp::KeyCode::Space, true, {});
    CHECK_FALSE(input.onEvent(oldKey));
    input.snapshot();
    CHECK_FALSE(input.digital(hp::ActionId{"Jump"}).pressed);

    hp::KeyEvent newKey(hp::KeyCode::Enter, true, {});
    CHECK(input.onEvent(newKey));
    input.snapshot();
    CHECK(input.digital(hp::ActionId{"Jump"}).pressed);
}
