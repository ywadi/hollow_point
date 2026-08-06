// Frame targets and the render stack, against a real device (T0046, T0027).
//
// Bucket: gpu. Every case here needs a graphics device, and **every case skips
// cleanly when the engine reports there is not one** -- no window (headless), or
// a window but no device.
//
// **That is not enough on its own, which is why `zig build test -Dtest=all`
// builds this bucket and does not run it.** The skip path handles a device the
// engine can refuse. It cannot handle a driver that takes the process down: on
// the Windows CI host this suite exited with code 5 having printed nothing at
// all, not even doctest's banner, which is what a crash looks like when a piped
// stdout is never flushed. That host has a desktop session, so a window is
// created and the engine goes on to meet GDI's generic OpenGL 1.1 -- below the
// level anything here can intercept. Run it with `-Dtest=gpu` on a machine with
// hardware.
//
// The skip is deliberate about *where* it gives up: no window (headless), or a
// window but no device (no driver, software-only, wine without a GL/Vulkan ICD).
// Both are environment facts rather than defects, so both report and return —
// and both now say so through the log sink attached in `bringUp`, because a
// skip whose reason is invisible is indistinguishable from a device the engine
// deliberately refused.

#include <doctest/doctest.h>

#include <hp/FrameTargets.hpp>
#include <hp/Log.hpp>
#include <hp/Render.hpp>
#include <hp/RenderStack.hpp>
#include <hp/Window.hpp>

#include <memory>
#include <string>

namespace {

/// A window and a device, or nothing.
struct Device {
    std::unique_ptr<hp::Window> window;
    std::unique_ptr<hp::RenderLayer> render;

    [[nodiscard]] bool ok() const { return render && render->ready(); }
};

/// Brings up a window and a device on the requested backend.
///
/// The window is small and short-lived on purpose: these run on a developer's
/// actual desktop, and a test that leaves a window up is a test people stop
/// running.
Device bringUp(hp::RenderBackend backend) {
    Device device;

    // Once, for the whole bucket. Without a sink the engine's log goes nowhere,
    // so a device the engine *deliberately refused* -- the D15 compute floor,
    // or T0130.3's clip-space floor -- is indistinguishable from a machine with
    // no GPU: both present as "skipping". The reason is the entire diagnostic
    // value of a skip, and it was silent until this line existed.
    static const bool sink = [] {
        hp::logAddConsoleSink();
        return true;
    }();
    (void)sink;

    hp::WindowConfig windowConfig;
    windowConfig.title = "hp gpu test";
    windowConfig.width = 320;
    windowConfig.height = 240;
    windowConfig.resizable = true;

    device.window = hp::Window::create(windowConfig);
    if (!device.window) {
        return device;
    }

    hp::RenderConfig renderConfig;
    renderConfig.backend = backend;
    renderConfig.vsync = false;
    device.render = std::make_unique<hp::RenderLayer>(*device.window, renderConfig);
    device.render->onAttach();
    return device;
}

/// Tears down in the order the device requires: layer first, then window.
void tearDown(Device& device) {
    if (device.render) {
        device.render->onDetach();
        device.render.reset();
    }
    device.window.reset();
}

/// The shared body, run once per backend.
void exerciseTargetsAndStack(hp::RenderBackend backend, const char* backendName) {
    Device device = bringUp(backend);
    if (!device.ok()) {
        MESSAGE("no " << std::string(backendName)
                      << " device available -- skipping (this is expected on CI)");
        tearDown(device);
        return;
    }

    // `std::string(backendName)`, not `backendName`. doctest's MessageBuilder
    // takes a literal fine but decays a `const char*` *variable* to bool, so the
    // first version of this line printed "device up on 1" -- a log that looked
    // like it named the backend and did not.
    MESSAGE("device up on " << std::string(backendName) << ": "
                            << device.render->adapterDescription());

    SUBCASE("clip space is [0, 1] on every backend") {
        // T0130.3. This is the measurement the ticket refused to take on
        // faith. Diligent's `EngineGLCreateInfo::ZeroToOneNDZ` defaults to
        // **false**, so before T0130 this engine ran Vulkan on [0, 1] and
        // OpenGL on [-1, 1] -- and nothing had yet built a projection matrix,
        // which is the only reason it had not produced a visible bug.
        const hp::ClipSpace clip = device.render->clipSpace();
        MESSAGE("clip space on " << std::string(backendName) << ": minZ=" << clip.minZ
                                 << " yToV=" << clip.yToV);

        CHECK(clip.minZ == 0.0F);
        CHECK_FALSE(clip.negativeOneToOneZ());

        // Not incidental: reverse-Z is only worth having on a [0, 1] clip
        // space, so this is the precondition for the whole depth convention.
        CHECK(clip.yToV != 0.0F);
    }

    SUBCASE("targets create for every role") {
        hp::FrameTargets targets;
        targets.declare({"scene", hp::TargetFormat::ColourHDR, 1.0F});
        targets.declare({"depth", hp::TargetFormat::Depth, 1.0F});
        targets.declare({"half", hp::TargetFormat::Colour, 0.5F});

        // The three roles are exactly the places a backend can legitimately
        // refuse: RGBA16_FLOAT as a render target, D32_FLOAT carrying
        // BIND_SHADER_RESOURCE, and an sRGB colour target.
        REQUIRE(targets.create(device.render->device(), 320, 240));
        CHECK(targets.ready());

        CHECK(targets.renderTarget("scene") != nullptr);
        CHECK(targets.depthStencil("depth") != nullptr);
        CHECK(targets.renderTarget("half") != nullptr);

        // Depth must be readable, or the transparent pass cannot fade soft
        // particles against scene depth (T0106.5).
        CHECK(targets.shaderResource("depth") != nullptr);
        CHECK(targets.shaderResource("scene") != nullptr);

        // Roles are enforced, not merely documented.
        CHECK(targets.depthStencil("scene") == nullptr);
        CHECK(targets.renderTarget("depth") == nullptr);

        // 320*240*8 (HDR) + 320*240*4 (depth) + 160*120*4 (half).
        CHECK(targets.memoryBytes() == 320ULL * 240 * 8 + 320ULL * 240 * 4 + 160ULL * 120 * 4);
    }

    SUBCASE("a ping-pong pair alternates between two real textures") {
        // 46.5 on a device, which is the only place the claim means anything:
        // **source and target must never be the same texture**. Binding one as
        // both is undefined, usually appears to work, and produces a subtly
        // wrong image nobody traces back to the bind.
        hp::FrameTargets targets;
        targets.declarePingPong({"blur", hp::TargetFormat::ColourHDR, 0.5F});
        REQUIRE(targets.create(device.render->device(), 320, 240));
        REQUIRE(targets.hasPingPong("blur"));

        auto* writeEven = targets.pingPongTarget("blur", 0);
        auto* readEven = targets.pingPongSource("blur", 0);
        auto* writeOdd = targets.pingPongTarget("blur", 1);
        auto* readOdd = targets.pingPongSource("blur", 1);

        REQUIRE(writeEven != nullptr);
        REQUIRE(readEven != nullptr);
        REQUIRE(writeOdd != nullptr);
        REQUIRE(readOdd != nullptr);

        // The two halves are genuinely distinct allocations, and each pass
        // reads the one it is not writing.
        CHECK(writeEven != writeOdd);
        CHECK(readEven != readOdd);

        // Alternating: what pass 0 writes is what pass 1 reads.
        CHECK(targets.shaderResource("blur.b") == readOdd);
        CHECK(targets.renderTarget("blur.b") == writeEven);

        // Both halves cost the same, since they share a description -- a pair
        // that differed in size would change resolution halfway through an
        // effect.
        CHECK(targets.memoryBytes() == 2ULL * 160 * 120 * 8);
    }

    SUBCASE("resize rebuilds at the new size and is debounced") {
        hp::FrameTargets targets;
        targets.declare({"scene", hp::TargetFormat::ColourHDR, 1.0F});
        REQUIRE(targets.create(device.render->device(), 320, 240));

        auto* before = targets.renderTarget("scene");
        REQUIRE(before != nullptr);

        // Unchanged size must not recreate. If it did, every frame would throw
        // away and rebuild every target -- which works, looks fine, and quietly
        // costs a full reallocation per frame.
        REQUIRE(targets.resize(320, 240));
        CHECK(targets.renderTarget("scene") == before);

        REQUIRE(targets.resize(640, 480));
        CHECK(targets.width() == 640);
        CHECK(targets.memoryBytes() == 640ULL * 480 * 8);

        // Repeated resizes must not accumulate. This does not prove the absence
        // of a leak -- nothing here can -- but a target set that grew per
        // resize would show it immediately.
        for (int i = 0; i < 16; ++i) {
            REQUIRE(targets.resize(320 + i, 240 + i));
        }
        CHECK(targets.memoryBytes() == (320ULL + 15) * (240 + 15) * 8);
    }

    SUBCASE("the stack renders enabled layers only, in order") {
        hp::FrameTargets targets;
        targets.declare({"scene", hp::TargetFormat::Colour, 1.0F});
        targets.declare({"depth", hp::TargetFormat::Depth, 1.0F});
        REQUIRE(targets.create(device.render->device(), 320, 240));

        /// Records that it ran and what it was handed.
        class Recording final : public hp::IRenderLayer {
        public:
            explicit Recording(const char* name, std::string* log) : name_(name), log_(log) {}

            void onRenderLayer(const hp::RenderPassContext& pass) override {
                sawDepth = pass.depth != nullptr;
                sawColour = pass.colour != nullptr;
                sawDevice = pass.device != nullptr;
                *log_ += name_;
            }

            [[nodiscard]] const char* name() const override { return name_; }

            bool sawDepth = false;
            bool sawColour = false;
            bool sawDevice = false;

        private:
            const char* name_;
            std::string* log_;
        };

        std::string order;
        Recording world("world", &order);
        Recording hud("hud", &order);
        Recording disabled("disabled", &order);

        world.order = 0;
        world.clear = hp::LayerClear::ColourAndDepth;
        world.useDepth = true;

        hud.order = 100;
        // The case the whole per-layer depth policy exists for.
        hud.useDepth = false;

        disabled.order = 50;
        disabled.enabled = false;

        hp::RenderStack stack;
        stack.add(&hud);
        stack.add(&disabled);
        stack.add(&world);

        const std::size_t rendered =
            stack.render(device.render->device(), device.render->context(),
                         targets.renderTarget("scene"), targets.depthStencil("depth"), &targets,
                         320, 240, device.render->clipSpace());

        CHECK(rendered == 2);
        CHECK(order == "worldhud");
        CHECK(world.sawDepth);
        CHECK(world.sawColour);
        CHECK(world.sawDevice);
        // A HUD must never receive the depth target, or it depth-tests against
        // world geometry and disappears behind whatever is near the camera.
        CHECK_FALSE(hud.sawDepth);
        CHECK(hud.sawColour);

        // The clear and the draws must actually reach the driver. If any call in
        // RenderStack::render were malformed, the validation layers report it
        // and the device is lost or the flush fails -- so surviving this is the
        // evidence that the RHI calls are well-formed.
        device.render->onRender();
        CHECK(device.render->ready());
    }

    tearDown(device);
}

} // namespace

TEST_CASE("frame targets and the render stack work on the default backend"
          * doctest::test_suite("gpu")) {
    exerciseTargetsAndStack(hp::RenderBackend::Default, "default");
}
