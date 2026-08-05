// The frame-rendered event (T0028.5).
//
// Bucket: fast. The event carries a texture view, but **nothing here needs a
// device**: what is being tested is that it travels the layer stack and reaches
// a listener that never sees the renderer. That decoupling is the whole point of
// the event, and it is testable with a null view.

#include <doctest/doctest.h>

#include <hp/Event.hpp>
#include <hp/Layer.hpp>

#include <memory>

namespace {

/// A listener that records what it saw, standing in for the viewport panel
/// (T0033) and the runtime's full-screen blit (T0042).
class Viewport final : public hp::ILayer {
public:
    void onEvent(hp::Event& event) override {
        if (event.type() != hp::EventType::FrameRendered) {
            return;
        }
        auto& frame = static_cast<hp::FrameRenderedEvent&>(event);
        seen = true;
        width = frame.width();
        height = frame.height();
        if (consumeIt) {
            event.consume();
        }
    }

    bool seen = false;
    bool consumeIt = false;
    int width = 0;
    int height = 0;
};

} // namespace

TEST_CASE("a frame-rendered event reaches a listener that never sees the renderer") {
    hp::LayerStack layers;
    auto* viewport = static_cast<Viewport*>(layers.push(std::make_unique<Viewport>()));

    hp::FrameRenderedEvent event(nullptr, 1280, 720);
    layers.dispatch(event);

    CHECK(viewport->seen);
    CHECK(viewport->width == 1280);
    CHECK(viewport->height == 720);
}

TEST_CASE("the frame-rendered event is categorised as render, not as a window event") {
    // A layer asks for "anything the renderer published" by category. Miscategorising
    // it as Window would deliver it to every layer watching for resizes.
    hp::FrameRenderedEvent event(nullptr, 4, 4);
    CHECK(event.isIn(hp::EventCategory::Render));
    CHECK_FALSE(event.isIn(hp::EventCategory::Window));
    CHECK_FALSE(event.isIn(hp::EventCategory::Input));
    CHECK(event.type() == hp::EventType::FrameRendered);
    CHECK(event.name() == "FrameRendered");
}

TEST_CASE("a consuming listener stops the frame reaching layers below it") {
    // Two viewports on one frame is legitimate -- a game view and a preview --
    // so consumption must be the listener's choice rather than automatic.
    hp::LayerStack layers;
    auto* below = static_cast<Viewport*>(layers.push(std::make_unique<Viewport>()));
    auto* above = static_cast<Viewport*>(layers.push(std::make_unique<Viewport>()));
    above->consumeIt = true;

    hp::FrameRenderedEvent event(nullptr, 8, 8);
    layers.dispatch(event);

    CHECK(above->seen);
    CHECK_FALSE(below->seen);
}

TEST_CASE("two non-consuming listeners both receive the same frame") {
    hp::LayerStack layers;
    auto* first = static_cast<Viewport*>(layers.push(std::make_unique<Viewport>()));
    auto* second = static_cast<Viewport*>(layers.push(std::make_unique<Viewport>()));

    hp::FrameRenderedEvent event(nullptr, 16, 9);
    layers.dispatch(event);

    CHECK(first->seen);
    CHECK(second->seen);
}
