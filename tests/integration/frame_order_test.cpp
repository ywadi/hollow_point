// Frame anatomy: the documented phase order (T0100).
//
// Bucket: integration -- it links the engine shared library and runs real
// loops. Headless for the reason application_test.cpp spells out: CI has no
// display, and a test that silently needs one only proves the machine it ran on
// had a display.
//
// **These assert relative order, not counts.** `Application::run` drives the
// clock from real time, so how many fixed steps a frame runs is a property of
// the machine, not of the code. A test that pinned an exact count would pass on
// a developer desktop and flake on a loaded CI runner -- which is precisely the
// class of failure the fixed-step cap exists to make survivable. What *is*
// deterministic, and what the frame contract actually promises, is the order.
//
// The contract under test is claude_documentation/documentation/08-frame-anatomy.md
// and decision D17. If this test and that document disagree, both are wrong
// until someone reconciles them.

#include <doctest/doctest.h>

#include <hp/Application.hpp>
#include <hp/Layer.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {

/// Appends a tag for every phase it sees, in the order it sees them.
class RecordingLayer final : public hp::ILayer {
public:
    explicit RecordingLayer(std::vector<std::string>& log)
        : hp::ILayer("recording"), log_(log) {}

private:
    void onFixedUpdate(double) override { log_.emplace_back("layer.fixed"); }
    void onUpdate(double) override { log_.emplace_back("layer.update"); }
    void onLateUpdate(double) override { log_.emplace_back("layer.late"); }
    void onRender() override { log_.emplace_back("layer.render"); }

    std::vector<std::string>& log_;
};

/// Runs a bounded headless loop with one recording layer, and records its own
/// hooks into the same log so layer-vs-application ordering is visible too.
class OrderApp final : public hp::Application {
public:
    explicit OrderApp(std::uint64_t frames) : hp::Application(makeConfig(frames)) {}

    std::vector<std::string> log;

private:
    static hp::ApplicationConfig makeConfig(std::uint64_t frames) {
        hp::ApplicationConfig config;
        config.headless = true;
        config.name = "frame-order";
        config.exitAfterFrames = frames;
        return config;
    }

    void onStartup() override { layers().push(std::make_unique<RecordingLayer>(log)); }

    void onFixedUpdate(double) override { log.emplace_back("app.fixed"); }
    void onUpdate(double) override { log.emplace_back("app.update"); }
    void onLateUpdate(double) override { log.emplace_back("app.late"); }
    void onRender() override { log.emplace_back("app.render"); }
};

/// Index of the first occurrence of `tag`, or -1.
std::ptrdiff_t firstIndex(const std::vector<std::string>& log, const std::string& tag) {
    const auto it = std::find(log.begin(), log.end(), tag);
    return it == log.end() ? -1 : it - log.begin();
}

std::size_t countOf(const std::vector<std::string>& log, const std::string& tag) {
    return static_cast<std::size_t>(std::count(log.begin(), log.end(), tag));
}

} // namespace

TEST_CASE("a frame runs update, then late update, then render, in that order") {
    OrderApp app(1);
    // A fixed step far larger than any plausible frame keeps the fixed block out
    // of this case entirely, so it tests one thing.
    app.clock().setFixedStep(1000.0);
    app.run();

    REQUIRE(firstIndex(app.log, "app.update") >= 0);
    REQUIRE(firstIndex(app.log, "app.late") >= 0);
    REQUIRE(firstIndex(app.log, "app.render") >= 0);

    CHECK(firstIndex(app.log, "app.update") < firstIndex(app.log, "app.late"));
    CHECK(firstIndex(app.log, "app.late") < firstIndex(app.log, "app.render"));
}

TEST_CASE("layers run before the application within each phase") {
    // Bottom-up: the world simulates before the interface over it, and the
    // application's own hook is the last word in the phase.
    OrderApp app(1);
    app.clock().setFixedStep(1000.0);
    app.run();

    CHECK(firstIndex(app.log, "layer.update") < firstIndex(app.log, "app.update"));
    CHECK(firstIndex(app.log, "layer.late") < firstIndex(app.log, "app.late"));
    CHECK(firstIndex(app.log, "layer.render") < firstIndex(app.log, "app.render"));
}

TEST_CASE("the fixed-step block runs zero times when the step is longer than the frame") {
    // Not a degenerate case: it is the normal state of a 60 Hz fixed step on a
    // frame that took less than 16 ms. Code in onFixedUpdate must tolerate it.
    OrderApp app(2);
    app.clock().setFixedStep(1000.0);
    app.run();

    CHECK(countOf(app.log, "app.fixed") == 0);
    CHECK(countOf(app.log, "layer.fixed") == 0);
    // The rest of the frame still happened.
    CHECK(countOf(app.log, "app.update") == 2);
}

TEST_CASE("the fixed-step block runs before update, and never exceeds its cap") {
    // A tiny step guarantees the accumulator always has debt, so the cap is what
    // stops the loop rather than the accumulator running dry. That is the branch
    // worth testing: uncapped, this is the spiral of death.
    constexpr int kCap = 3;
    OrderApp app(2);
    app.clock().setFixedStep(1e-9);
    app.clock().setMaxFixedStepsPerFrame(kCap);
    app.run();

    const std::size_t fixedCalls = countOf(app.log, "app.fixed");
    CHECK(fixedCalls >= 1);
    CHECK(fixedCalls <= static_cast<std::size_t>(kCap) * 2);

    // Ordering: the first fixed step of the run precedes the first update.
    CHECK(firstIndex(app.log, "app.fixed") < firstIndex(app.log, "app.update"));
    CHECK(firstIndex(app.log, "layer.fixed") < firstIndex(app.log, "app.fixed"));
}

TEST_CASE("the documented order holds on every frame, not just the first") {
    // Guards the ordering-by-accident case: a loop that happened to be right on
    // frame one and drifted afterwards would pass every check above.
    constexpr std::uint64_t kFrames = 4;
    OrderApp app(kFrames);
    app.clock().setFixedStep(1000.0);
    app.run();

    // With the fixed block disabled, each frame contributes exactly this cycle.
    const std::vector<std::string> expected = {
        "layer.update", "app.update", "layer.late", "app.late", "layer.render", "app.render",
    };

    REQUIRE(app.log.size() == expected.size() * kFrames);
    for (std::size_t frame = 0; frame < kFrames; ++frame) {
        for (std::size_t phase = 0; phase < expected.size(); ++phase) {
            const std::size_t at = frame * expected.size() + phase;
            CHECK(app.log[at] == expected[phase]);
        }
    }
}
