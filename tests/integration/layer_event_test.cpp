// Layers and event propagation (T0017, T0018).
//
// Bucket: integration -- links the engine shared library.
//
// The two orderings are the substance of both tickets and are asserted
// independently, because they are deliberately opposite: update runs bottom-up
// so the world simulates before the interface drawn over it, and events run
// top-down so the topmost thing on screen gets first refusal on a click.

#include <doctest/doctest.h>

#include <hp/Application.hpp>
#include <hp/Event.hpp>
#include <hp/Layer.hpp>

#include <memory>
#include <string>
#include <vector>

namespace {

/// Records every callback it receives into a shared log.
class Recorder final : public hp::ILayer {
public:
    Recorder(std::string_view name, std::vector<std::string>* log, bool consumes = false)
        : hp::ILayer(name), log_(log), consumes_(consumes) {}

    void onAttach() override { log_->push_back(std::string(name()) + ":attach"); }

    void onDetach() override { log_->push_back(std::string(name()) + ":detach"); }

    void onUpdate(double) override { log_->push_back(std::string(name()) + ":update"); }

    void onRender() override { log_->push_back(std::string(name()) + ":render"); }

    void onEvent(hp::Event& event) override {
        log_->push_back(std::string(name()) + ":event");
        if (consumes_) {
            event.consume();
        }
    }

private:
    std::vector<std::string>* log_;
    bool consumes_;
};

std::unique_ptr<Recorder> make(std::string_view name, std::vector<std::string>* log,
                               bool consumes = false) {
    return std::make_unique<Recorder>(name, log, consumes);
}

} // namespace

TEST_CASE("update runs bottom-up, events run top-down") {
    std::vector<std::string> log;
    hp::LayerStack stack;
    stack.push(make("bottom", &log));
    stack.push(make("top", &log));
    log.clear();

    stack.update(0.016);
    REQUIRE(log.size() == 2);
    CHECK(log[0] == "bottom:update");
    CHECK(log[1] == "top:update");

    log.clear();
    hp::WindowCloseEvent event;
    stack.dispatch(event);
    REQUIRE(log.size() == 2);
    CHECK(log[0] == "top:event");
    CHECK(log[1] == "bottom:event");
}

TEST_CASE("a consumed event does not reach lower layers") {
    // The property the whole design exists for: an editor panel takes a
    // keypress the game would otherwise act on.
    std::vector<std::string> log;
    hp::LayerStack stack;
    stack.push(make("game", &log));
    stack.push(make("ui", &log, /*consumes=*/true));
    log.clear();

    hp::KeyEvent key(hp::KeyCode::Escape, true, {});
    stack.dispatch(key);

    CHECK(key.isConsumed());
    REQUIRE(log.size() == 1);
    CHECK(log[0] == "ui:event");
}

TEST_CASE("overlays stay above layers pushed later") {
    // Without this, pushing a gameplay layer after the editor UI silently puts
    // the world on top of the interface -- invisible until someone clicks.
    std::vector<std::string> log;
    hp::LayerStack stack;
    stack.push(make("world", &log));
    stack.pushOverlay(make("overlay", &log));
    stack.push(make("later", &log));
    log.clear();

    hp::WindowCloseEvent event;
    stack.dispatch(event);
    REQUIRE(log.size() == 3);
    CHECK(log[0] == "overlay:event"); // still first, despite being pushed second
    CHECK(log[1] == "later:event");
    CHECK(log[2] == "world:event");
    CHECK(stack.overlayCount() == 1);
}

TEST_CASE("attach on push, detach on pop, detach top-down on clear") {
    std::vector<std::string> log;
    {
        hp::LayerStack stack;
        stack.push(make("a", &log));
        auto* b = stack.push(make("b", &log));
        CHECK(log[0] == "a:attach");
        CHECK(log[1] == "b:attach");

        CHECK(stack.pop(b));
        CHECK(log[2] == "b:detach");
        CHECK_FALSE(stack.pop(b)); // already gone
        CHECK(stack.size() == 1);
    }
    // Destructor clears; teardown is top-down, mirroring dispatch.
    CHECK(log.back() == "a:detach");
}

TEST_CASE("dispatchEvent casts safely and consumes on a true handler") {
    hp::KeyEvent key(hp::KeyCode::Space, true, {});
    hp::Event& base = key;

    const bool wrongType = hp::dispatchEvent<hp::MouseMovedEvent>(
        base, [](const hp::MouseMovedEvent&) { return true; });
    CHECK_FALSE(wrongType);
    CHECK_FALSE(base.isConsumed());

    const bool declined =
        hp::dispatchEvent<hp::KeyEvent>(base, [](const hp::KeyEvent&) { return false; });
    CHECK_FALSE(declined);
    CHECK_FALSE(base.isConsumed());

    const bool handled = hp::dispatchEvent<hp::KeyEvent>(
        base, [](const hp::KeyEvent& e) { return e.key() == hp::KeyCode::Space; });
    CHECK(handled);
    CHECK(base.isConsumed());
}

TEST_CASE("the application dispatches through layers, then to its own hook") {
    class App final : public hp::Application {
    public:
        App() : hp::Application(headless()) {}

        int fallback = 0;

    private:
        static hp::ApplicationConfig headless() {
            hp::ApplicationConfig c;
            c.headless = true;
            c.exitAfterFrames = 1;
            return c;
        }

        void onEvent(hp::Event&) override { ++fallback; }
    };

    std::vector<std::string> log;
    App app;
    app.layers().push(make("layer", &log));

    hp::WindowCloseEvent unclaimed;
    app.dispatch(unclaimed);
    CHECK(app.fallback == 1); // nothing consumed it, so the app saw it

    app.layers().push(make("greedy", &log, /*consumes=*/true));
    hp::WindowCloseEvent claimed;
    app.dispatch(claimed);
    CHECK(app.fallback == 1); // consumed by a layer, never reached the app
}
