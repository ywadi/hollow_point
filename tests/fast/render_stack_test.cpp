// Render stack ordering and membership (T0027).
//
// Bucket: fast, so there is no device. What is covered here is the stack's own
// logic — ordering, insertion stability, double-add refusal, removal. What is
// *not* covered is `render()` doing anything, because that needs a real
// `IDeviceContext` and stubbing one would mean implementing several dozen pure
// virtuals to observe two calls. That belongs in a device test; see the ticket.

#include <doctest/doctest.h>

#include <hp/RenderStack.hpp>

#include <string>
#include <vector>

namespace {

/// A layer that records nothing and draws nothing. Enough to be a distinct
/// identity in a stack, which is all these cases need.
class ProbeLayer final : public hp::IRenderLayer {
public:
    explicit ProbeLayer(const char* name) : name_(name) {}

    void onRenderLayer(const hp::RenderPassContext& pass) override {
        (void)pass;
        ++calls;
    }

    [[nodiscard]] const char* name() const override { return name_; }

    int calls = 0;

private:
    const char* name_;
};

} // namespace

TEST_CASE("a new stack is empty") {
    hp::RenderStack stack;
    CHECK(stack.size() == 0);
    CHECK(stack.layers().empty());
}

TEST_CASE("layers sort by order, lowest first") {
    hp::RenderStack stack;
    ProbeLayer world("world");
    ProbeLayer hud("hud");
    ProbeLayer debug("debug");
    world.order = 0;
    hud.order = 100;
    debug.order = 200;

    // Added out of order on purpose -- the stack sorts, callers do not.
    stack.add(&debug);
    stack.add(&world);
    stack.add(&hud);

    REQUIRE(stack.size() == 3);
    CHECK(std::string(stack.layers()[0]->name()) == "world");
    CHECK(std::string(stack.layers()[1]->name()) == "hud");
    CHECK(std::string(stack.layers()[2]->name()) == "debug");
}

TEST_CASE("equal orders keep insertion order") {
    hp::RenderStack stack;
    ProbeLayer first("first");
    ProbeLayer second("second");
    ProbeLayer third("third");
    // All the same order: the sort must be stable, or a composite that depends
    // on their sequence would be intermittently wrong between runs.
    stack.add(&first);
    stack.add(&second);
    stack.add(&third);

    CHECK(std::string(stack.layers()[0]->name()) == "first");
    CHECK(std::string(stack.layers()[1]->name()) == "second");
    CHECK(std::string(stack.layers()[2]->name()) == "third");
}

TEST_CASE("a layer added twice is refused, not drawn twice") {
    hp::RenderStack stack;
    ProbeLayer layer("once");

    stack.add(&layer);
    stack.add(&layer);

    // The failure this prevents presents as a blending or alpha artefact rather
    // than as a double-add, which is why it is refused rather than tolerated.
    CHECK(stack.size() == 1);
}

TEST_CASE("adding nothing is harmless") {
    hp::RenderStack stack;
    stack.add(nullptr);
    CHECK(stack.size() == 0);
}

TEST_CASE("removal reports whether the layer was there") {
    hp::RenderStack stack;
    ProbeLayer present("present");
    ProbeLayer absent("absent");
    stack.add(&present);

    CHECK(stack.remove(&present));
    CHECK(stack.size() == 0);
    CHECK_FALSE(stack.remove(&present));
    CHECK_FALSE(stack.remove(&absent));
}

TEST_CASE("reorder picks up an order changed after insertion") {
    hp::RenderStack stack;
    ProbeLayer a("a");
    ProbeLayer b("b");
    a.order = 0;
    b.order = 10;
    stack.add(&a);
    stack.add(&b);
    REQUIRE(std::string(stack.layers()[0]->name()) == "a");

    // `order` is public data, so the stack cannot observe the change -- which is
    // exactly why reorder() is explicit rather than the stack sorting every
    // frame to catch something that happens rarely.
    a.order = 20;
    stack.reorder();

    CHECK(std::string(stack.layers()[0]->name()) == "b");
    CHECK(std::string(stack.layers()[1]->name()) == "a");
}

TEST_CASE("clear empties the stack without destroying layers") {
    hp::RenderStack stack;
    ProbeLayer layer("layer");
    stack.add(&layer);

    stack.clear();

    CHECK(stack.size() == 0);
    // The layer is still alive and usable -- the stack never owned it. That is
    // the rule that keeps a gameplay module's layer safe: the module owns it and
    // removes it on unload.
    CHECK(layer.calls == 0);
    stack.add(&layer);
    CHECK(stack.size() == 1);
}

TEST_CASE("render does nothing without a context, rather than crashing") {
    hp::RenderStack stack;
    ProbeLayer layer("layer");
    stack.add(&layer);

    CHECK(stack.render(nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0) == 0);
    CHECK(layer.calls == 0);
}
