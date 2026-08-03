// Time (T0057).
//
// Bucket: fast. Every case drives `advance()` with an explicit delta rather
// than sleeping -- a test that had to wait real time to exercise the
// accumulator would be slow and flaky, which is why that seam exists.

#include <doctest/doctest.h>

#include <hp/Time.hpp>

TEST_CASE("delta is scaled, unscaled delta is not") {
    hp::Clock clock;
    clock.setTimeScale(0.5);
    clock.advance(0.1);

    CHECK(clock.delta() == doctest::Approx(0.05));
    CHECK(clock.unscaledDelta() == doctest::Approx(0.1));
    CHECK(clock.elapsed() == doctest::Approx(0.05));
    CHECK(clock.unscaledElapsed() == doctest::Approx(0.1));
    CHECK(clock.frame() == 1);
}

TEST_CASE("pause stops game time but not unscaled time") {
    // The property that makes play-mode pause usable: the editor keeps running.
    hp::Clock clock;
    clock.advance(0.1);
    clock.setPaused(true);
    clock.advance(0.1);

    CHECK(clock.delta() == doctest::Approx(0.0));
    CHECK(clock.unscaledDelta() == doctest::Approx(0.1));
    CHECK(clock.elapsed() == doctest::Approx(0.1));         // did not advance
    CHECK(clock.unscaledElapsed() == doctest::Approx(0.2)); // did
}

TEST_CASE("unpausing restores the previous time scale") {
    // Pause is separate from a zero scale precisely so this works. If pause were
    // implemented as setTimeScale(0), unpausing would have to guess.
    hp::Clock clock;
    clock.setTimeScale(0.25);
    clock.setPaused(true);
    clock.advance(0.1);
    CHECK(clock.delta() == doctest::Approx(0.0));

    clock.setPaused(false);
    clock.advance(0.1);
    CHECK(clock.timeScale() == doctest::Approx(0.25));
    CHECK(clock.delta() == doctest::Approx(0.025));
}

TEST_CASE("a huge delta is clamped") {
    // The breakpoint case. Unclamped, one frame of eight seconds teleports
    // everything through everything.
    hp::Clock clock;
    clock.setMaxDelta(0.25);
    clock.advance(8.0);

    CHECK(clock.unscaledDelta() == doctest::Approx(0.25));
    CHECK(clock.delta() == doctest::Approx(0.25));
    CHECK(clock.unscaledElapsed() == doctest::Approx(0.25));
}

TEST_CASE("time never runs backwards") {
    hp::Clock clock;
    clock.advance(-1.0);
    CHECK(clock.delta() == doctest::Approx(0.0));
    CHECK(clock.elapsed() == doctest::Approx(0.0));

    clock.setTimeScale(-2.0);
    CHECK(clock.timeScale() == doctest::Approx(0.0));
}

TEST_CASE("the fixed step runs the right number of times") {
    hp::Clock clock;
    clock.setFixedStep(1.0 / 60.0);

    clock.advance(1.0 / 60.0 * 3.0);
    int steps = 0;
    while (clock.consumeFixedStep()) {
        ++steps;
    }
    CHECK(steps == 3);

    // A frame shorter than one step runs none, and the remainder carries.
    clock.advance(1.0 / 240.0);
    CHECK_FALSE(clock.consumeFixedStep());
}

TEST_CASE("leftover time accumulates rather than being lost") {
    // Exact binary fractions on purpose. An earlier version of this test used
    // 0.1 and 0.06 and expected three steps from five frames; five additions of
    // 0.06 give 0.29999999999999993, which is genuinely two whole steps of 0.1,
    // and the test was wrong rather than the accumulator. Anything asserting an
    // exact step count across a boundary has to use values a double represents
    // exactly, or it is asserting the behaviour of floating point.
    hp::Clock clock;
    clock.setFixedStep(0.125); // 1/8

    for (int i = 0; i < 4; ++i) {
        clock.advance(0.0625); // 1/16, so four frames is exactly 0.25
    }
    int steps = 0;
    while (clock.consumeFixedStep()) {
        ++steps;
    }
    CHECK(steps == 2);
    CHECK(clock.interpolationAlpha() == doctest::Approx(0.0));
}

TEST_CASE("a partial step carries into the next frame") {
    // The property the previous case is really about, stated without depending
    // on an exact boundary: time not consumed this frame is not discarded.
    hp::Clock clock;
    clock.setFixedStep(0.125);

    clock.advance(0.075);
    CHECK_FALSE(clock.consumeFixedStep()); // not enough yet

    clock.advance(0.075); // 0.15 total -- now past one step
    int steps = 0;
    while (clock.consumeFixedStep()) {
        ++steps;
    }
    CHECK(steps == 1);
}

TEST_CASE("the interpolation alpha reports the fraction between steps") {
    hp::Clock clock;
    clock.setFixedStep(0.1);
    clock.advance(0.25);

    while (clock.consumeFixedStep()) {
    }
    // 0.25 leaves 0.05 after two steps -- half a step.
    CHECK(clock.interpolationAlpha() == doctest::Approx(0.5));
    CHECK(clock.interpolationAlpha() >= 0.0);
    CHECK(clock.interpolationAlpha() < 1.0);
}

TEST_CASE("the fixed step is bounded so a slow machine cannot spiral") {
    // Without the bound, a frame that cannot keep up accumulates more debt each
    // time and runs more steps to pay it, making the next frame slower still.
    // The bound turns that into the game running slow, which is survivable.
    hp::Clock clock;
    clock.setFixedStep(1.0 / 60.0);
    clock.setMaxFixedStepsPerFrame(4);
    clock.setMaxDelta(10.0);

    clock.advance(1.0); // 60 steps' worth of debt
    int steps = 0;
    while (clock.consumeFixedStep()) {
        ++steps;
    }
    CHECK(steps == 4);

    // And the debt is dropped rather than carried, or the next frame inherits
    // the spiral.
    clock.advance(1.0 / 60.0);
    steps = 0;
    while (clock.consumeFixedStep()) {
        ++steps;
    }
    CHECK(steps == 1);
}

TEST_CASE("two clocks are independent") {
    // Editor and game each own one; pausing the game must not stop the editor.
    hp::Clock game;
    hp::Clock editor;

    game.setPaused(true);
    game.advance(0.1);
    editor.advance(0.1);

    CHECK(game.elapsed() == doctest::Approx(0.0));
    CHECK(editor.elapsed() == doctest::Approx(0.1));
}

TEST_CASE("tick measures real elapsed time") {
    // The one case that touches the wall clock, to prove tick() is wired to it
    // at all. Asserts only non-negativity and a generous ceiling -- anything
    // tighter would be a flaky test on a loaded machine.
    hp::Clock clock;
    clock.tick();
    CHECK(clock.unscaledDelta() >= 0.0);
    CHECK(clock.unscaledDelta() <= clock.maxDelta());
    CHECK(clock.frame() == 1);
}
