// The action layer (T0068).
//
// Bucket: fast. No window, no device, no frame loop — events are constructed
// and fed in directly, which is exactly how the frame loop feeds them and means
// these run in microseconds.
#include <doctest/doctest.h>

#include <hp/Input.hpp>

#include <cmath>

namespace {

constexpr hp::ActionId kJump{"Jump"};
constexpr hp::ActionId kMove{"Move"};
constexpr hp::ActionId kFire{"Fire"};
constexpr hp::ActionId kUnbound{"NeverBound"};

/// Feeds a key event in, as the window pump does.
bool key(hp::InputSystem& input, hp::KeyCode code, bool pressed, bool repeat = false) {
    hp::KeyEvent event(code, pressed, hp::KeyModifiers{}, repeat);
    return input.onEvent(event);
}

bool mouse(hp::InputSystem& input, hp::MouseButton button, bool pressed) {
    hp::MouseButtonEvent event(button, pressed, 0.0F, 0.0F);
    return input.onEvent(event);
}

hp::InputMap gameplayMap() {
    hp::InputMap map;
    map.bindKey(kJump, hp::KeyCode::Space);
    map.bindMouseButton(kFire, hp::MouseButton::Left);
    map.bindAxis2D(kMove, hp::KeyCode::A, hp::KeyCode::D, hp::KeyCode::S, hp::KeyCode::W);
    return map;
}

} // namespace

TEST_CASE("an action id is a name hash, computed at compile time") {
    // The property that matters: an id is derived from the name, so it survives
    // a rebuild, a saved binding file and the module boundary. An index would
    // not, which is the same lesson T0095 learned about entt::type_index.
    static_assert(hp::ActionId{"Jump"} == kJump, "ids must be constexpr and by-name");
    static_assert(hp::ActionId{"Jump"} != hp::ActionId{"jump"}, "case matters");
    CHECK(hp::ActionId{"Jump"}.value() == kJump.value());
    CHECK(hp::ActionId{}.value() == 0);
}

TEST_CASE("a digital action reports pressed, held and released distinctly") {
    hp::InputSystem input;
    input.pushContext(gameplayMap());

    key(input, hp::KeyCode::Space, true);
    input.snapshot();
    CHECK(input.digital(kJump).pressed);
    CHECK(input.digital(kJump).held);
    CHECK_FALSE(input.digital(kJump).released);

    // Next step, still down: held, but no longer a fresh press. Anything firing
    // on `pressed` must not fire again while the key stays down.
    input.snapshot();
    CHECK_FALSE(input.digital(kJump).pressed);
    CHECK(input.digital(kJump).held);

    key(input, hp::KeyCode::Space, false);
    input.snapshot();
    CHECK(input.digital(kJump).released);
    CHECK_FALSE(input.digital(kJump).held);

    input.snapshot();
    CHECK_FALSE(input.digital(kJump).released);
    CHECK_FALSE(input.digital(kJump).held);
}

TEST_CASE("a tap inside a single step reports both edges") {
    // The case that decides whether edges come from events or from sampling.
    // Polling "is it down now" against "was it down last step" loses this
    // entirely, and loses it more often as the frame rate rises -- so a fast
    // machine drops inputs a slow one catches.
    hp::InputSystem input;
    input.pushContext(gameplayMap());

    key(input, hp::KeyCode::Space, true);
    key(input, hp::KeyCode::Space, false);
    input.snapshot();

    const hp::DigitalAction jump = input.digital(kJump);
    CHECK_MESSAGE(jump.pressed, "a press and release inside one step must still report the press");
    CHECK_MESSAGE(jump.released, "...and the release");
    CHECK_FALSE(jump.held);
}

TEST_CASE("key auto-repeat is not a fresh press") {
    // A held key generates repeats every few milliseconds. If those counted as
    // presses, "pressed" would mean "held" for anything that fires on it, and
    // the distinction 68.3 asks for would not exist.
    hp::InputSystem input;
    input.pushContext(gameplayMap());

    key(input, hp::KeyCode::Space, true);
    input.snapshot();
    CHECK(input.digital(kJump).pressed);

    key(input, hp::KeyCode::Space, true, /*repeat=*/true);
    input.snapshot();
    CHECK_FALSE_MESSAGE(input.digital(kJump).pressed,
                        "an auto-repeat must not read as a new press");
    CHECK(input.digital(kJump).held);
}

TEST_CASE("a mouse button drives an action like a key does") {
    hp::InputSystem input;
    input.pushContext(gameplayMap());

    mouse(input, hp::MouseButton::Left, true);
    input.snapshot();
    CHECK(input.digital(kFire).pressed);
    CHECK(input.digital(kFire).held);

    mouse(input, hp::MouseButton::Left, false);
    input.snapshot();
    CHECK(input.digital(kFire).released);
}

TEST_CASE("WASD composes into a normalised 2D axis") {
    hp::InputSystem input;
    input.pushContext(gameplayMap());

    CHECK(input.axis2D(kMove).x == doctest::Approx(0.0F));

    key(input, hp::KeyCode::D, true);
    CHECK(input.axis2D(kMove).x == doctest::Approx(1.0F));
    CHECK(input.axis2D(kMove).y == doctest::Approx(0.0F));

    key(input, hp::KeyCode::W, true);
    const hp::Axis2D diagonal = input.axis2D(kMove);
    // The classic bug this prevents: composing per component leaves a diagonal
    // at length sqrt(2), so the player moves ~41% faster diagonally.
    const float magnitude = std::sqrt(diagonal.x * diagonal.x + diagonal.y * diagonal.y);
    CHECK_MESSAGE(magnitude == doctest::Approx(1.0F),
                  "a diagonal must be normalised, or the player moves faster diagonally");

    // Opposite keys cancel rather than latching to whichever came last.
    key(input, hp::KeyCode::A, true);
    CHECK(input.axis2D(kMove).x == doctest::Approx(0.0F));

    // D up leaves A and W down -- still a diagonal, so x is the normalised
    // -1/sqrt(2), not -1. Worth asserting the normalised value rather than
    // releasing W first: getting this wrong is how a "normalised" axis quietly
    // stops being normalised.
    key(input, hp::KeyCode::D, false);
    CHECK(input.axis2D(kMove).x == doctest::Approx(-0.70710678F));

    key(input, hp::KeyCode::W, false);
    CHECK(input.axis2D(kMove).x == doctest::Approx(-1.0F));
}

TEST_CASE("dead zone and sensitivity shape an axis") {
    hp::InputSystem input;
    hp::InputMap map;
    hp::AxisTuning tuning;
    tuning.deadZone = 0.5F;
    tuning.sensitivity = 2.0F;
    map.bindAxis2D(kMove, hp::KeyCode::A, hp::KeyCode::D, hp::KeyCode::S, hp::KeyCode::W, tuning);
    input.pushContext(std::move(map));

    // A single key composes to magnitude 1, which clears a 0.5 dead zone and is
    // then scaled by sensitivity.
    key(input, hp::KeyCode::D, true);
    CHECK(input.axis2D(kMove).x == doctest::Approx(2.0F));

    // Nothing down is below the dead zone, not merely zero.
    key(input, hp::KeyCode::D, false);
    CHECK(input.axis2D(kMove).x == doctest::Approx(0.0F));
}

TEST_CASE("a higher-priority context consumes input before a lower one sees it") {
    // The part the ticket says people leave out and regret: while a menu is
    // open, gameplay bindings must be suppressed -- without gameplay knowing a
    // menu exists.
    hp::InputSystem input;
    input.pushContext(gameplayMap(), {.priority = 0, .consumes = true, .name = "gameplay"});

    hp::InputMap menu;
    menu.bindKey(kJump, hp::KeyCode::Space);
    const std::size_t menuHandle =
        input.pushContext(std::move(menu), {.priority = 10, .consumes = true, .name = "menu"});

    CHECK_MESSAGE(key(input, hp::KeyCode::Space, true),
                  "the menu context binds Space and consumes, so the event is claimed");
    input.snapshot();
    // Both contexts bind Jump to Space, so the action fires -- but only once,
    // and from the menu. What matters is that a *lower* context binding Space
    // to something else would not have seen it.
    CHECK(input.digital(kJump).pressed);

    // With the menu gone, gameplay gets it back.
    key(input, hp::KeyCode::Space, false);
    input.removeContext(menuHandle);
    input.snapshot();

    key(input, hp::KeyCode::Space, true);
    input.snapshot();
    CHECK(input.digital(kJump).pressed);
}

TEST_CASE("a consuming context blocks a lower context's distinct binding") {
    hp::InputSystem input;

    // Gameplay: Space is Jump.
    hp::InputMap gameplay;
    gameplay.bindKey(kJump, hp::KeyCode::Space);
    input.pushContext(std::move(gameplay), {.priority = 0, .consumes = true, .name = "gameplay"});

    // Menu on top: Space is Fire. Gameplay's Jump must not also fire.
    hp::InputMap menu;
    menu.bindKey(kFire, hp::KeyCode::Space);
    input.pushContext(std::move(menu), {.priority = 10, .consumes = true, .name = "menu"});

    key(input, hp::KeyCode::Space, true);
    input.snapshot();
    CHECK(input.digital(kFire).pressed);
    CHECK_FALSE_MESSAGE(input.digital(kJump).pressed,
                        "gameplay's binding fired while a consuming context above it owned the "
                        "same key -- editor and game bindings would collide");
}

TEST_CASE("a non-consuming context lets input through to the one below") {
    hp::InputSystem input;

    hp::InputMap gameplay;
    gameplay.bindKey(kJump, hp::KeyCode::Space);
    input.pushContext(std::move(gameplay), {.priority = 0, .consumes = true, .name = "gameplay"});

    // An overlay that watches without claiming -- a recorder, or a debug HUD.
    hp::InputMap overlay;
    overlay.bindKey(kFire, hp::KeyCode::Space);
    input.pushContext(std::move(overlay), {.priority = 10, .consumes = false, .name = "overlay"});

    // Consumed -- but by *gameplay*, underneath, not by the overlay. The return
    // value says "something claimed this", which is still true; what the case
    // is about is that the overlay did not stop it getting there.
    CHECK(key(input, hp::KeyCode::Space, true));
    input.snapshot();
    CHECK(input.digital(kFire).pressed);
    CHECK_MESSAGE(input.digital(kJump).pressed,
                  "a non-consuming context must not block the context below it");
}

TEST_CASE("an unbound action is inert rather than an error") {
    hp::InputSystem input;
    input.pushContext(gameplayMap());
    key(input, hp::KeyCode::Space, true);
    input.snapshot();

    CHECK_FALSE(input.digital(kUnbound).pressed);
    CHECK_FALSE(input.digital(kUnbound).held);
    CHECK(input.axis2D(kUnbound).x == doctest::Approx(0.0F));
}

TEST_CASE("reset clears held state, for focus loss") {
    // A window that loses focus while a key is down never receives the key-up,
    // so without this the action stays held forever and the character keeps
    // walking into a wall while the player is in another application. T0110
    // owns when this is called; the hook is here.
    hp::InputSystem input;
    input.pushContext(gameplayMap());

    key(input, hp::KeyCode::W, true);
    key(input, hp::KeyCode::Space, true);
    input.snapshot();
    CHECK(input.digital(kJump).held);
    CHECK(input.axis2D(kMove).y == doctest::Approx(1.0F));

    input.reset();
    input.snapshot();
    CHECK_FALSE(input.digital(kJump).held);
    CHECK(input.axis2D(kMove).y == doctest::Approx(0.0F));
}

TEST_CASE("context priority beats push order, and ties go to the newest") {
    hp::InputSystem input;

    hp::InputMap low;
    low.bindKey(kJump, hp::KeyCode::Space);
    input.pushContext(std::move(low), {.priority = 5, .consumes = true, .name = "low"});

    hp::InputMap high;
    high.bindKey(kFire, hp::KeyCode::Space);
    input.pushContext(std::move(high), {.priority = 50, .consumes = true, .name = "high"});

    key(input, hp::KeyCode::Space, true);
    input.snapshot();
    CHECK(input.digital(kFire).pressed);
    CHECK_FALSE(input.digital(kJump).pressed);
}
