// A world layer and a HUD layer, stacked, with the pixels checked (T0027.3/27.4).
//
// Bucket: gpu. **This is the test the ticket's last open "Done when" was waiting
// for**, and its design follows from one line in T0028's notes: two submitted
// layers prove nothing about ordering. Every counter can report a draw while the
// frame is empty, or while the HUD is underneath the world. So the assertions are
// on pixels in named regions and on nothing else.
//
// ---
//
// **Measured first, and it changed the design: this engine renders every mesh
// pure black** — `(0, 0, 0, 255)`, whatever the material's base colour or
// emissive factor. `SceneRenderer` runs `PBR_Renderer` with no lights, no IBL
// and no emissive (each belongs to a later ticket and each needs resources
// nothing supplies yet), so the shading result is zero. T0028's mesh test passes
// against this because black differs from its blue clear colour, which is all it
// asked.
//
// So **material colour cannot tell the two layers apart**, and the discriminator
// here is the clear instead. It is a stronger one:
//
//   * the world layer clears the whole target to blue, then draws into the
//     **left** half — its camera's viewport is the left half;
//   * the HUD layer clears nothing and draws into the **right** half.
//
// Render the world alone and the right half stays blue. Render both and the
// right half is covered. **That is what proves the order**: had the HUD drawn
// first, the world layer's colour clear would have erased it, and the right half
// would come back blue in both cases. The left half staying covered proves the
// HUD did not erase the world.
//
// It also proves something the spec called out separately: the viewport comes
// from the resolved camera, not from the pass size. If it came from the pass, the
// world layer would fill the whole frame and the control case would have no blue
// in it at all.
//
// **The two layers draw two separate scenes, and that is a finding rather than a
// convenience.** A view slot picks the *camera*; it does not filter *objects*.
// Both layers run `parseScene` over whatever scene they are given, so one shared
// scene would have each layer drawing the other's geometry. Filtering objects per
// layer is `Camera::cullingMask`, which is stored and honoured nowhere — T0045.

#include <doctest/doctest.h>

#include <hp/Assets.hpp>
#include <hp/Camera.hpp>
#include <hp/FrameTargets.hpp>
#include <hp/Log.hpp>
#include <hp/Render.hpp>
#include <hp/RenderStack.hpp>
#include <hp/Scene.hpp>
#include <hp/SceneRenderLayer.hpp>
#include <hp/Vfs.hpp>
#include <hp/Window.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr int kSize = 256;

struct Device {
    std::unique_ptr<hp::Window> window;
    std::unique_ptr<hp::RenderLayer> render;
    [[nodiscard]] bool ok() const { return render && render->ready(); }
};

Device bringUp(hp::RenderBackend backend) {
    static const bool sink = [] {
        hp::logAddConsoleSink();
        return true;
    }();
    (void)sink;

    hp::WindowConfig windowConfig;
    windowConfig.title = "hp render stack composite test";
    windowConfig.width = kSize;
    windowConfig.height = kSize;
    windowConfig.openGLContext = backend == hp::RenderBackend::OpenGL;

    Device device;
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

void tearDown(Device& device) {
    if (device.render) {
        device.render->onDetach();
        device.render.reset();
    }
    device.window.reset();
}

/// A quad large enough to overfill either camera here, written as glTF.
///
/// **`doubleSided` is true on purpose**, for the reason T0028's mesh test gives:
/// back-face culling would answer "did fragments reach the target" with "no" for
/// a winding mistake, which is a different bug with the same symptom.
///
/// The engine is left-handed and both cameras look down +Z, so it sits at z = 3.
void writeQuadGltf(const std::filesystem::path& directory) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    const float vertices[] = {
        -4.0F, -4.0F, 3.0F, 0.0F, 0.0F, -1.0F,
         4.0F, -4.0F, 3.0F, 0.0F, 0.0F, -1.0F,
         4.0F,  4.0F, 3.0F, 0.0F, 0.0F, -1.0F,
        -4.0F,  4.0F, 3.0F, 0.0F, 0.0F, -1.0F,
    };
    const std::uint16_t indices[] = {0, 1, 2, 0, 2, 3};

    std::vector<unsigned char> bin;
    const auto* vb = reinterpret_cast<const unsigned char*>(vertices);
    bin.insert(bin.end(), vb, vb + sizeof vertices);
    const auto* ib = reinterpret_cast<const unsigned char*>(indices);
    bin.insert(bin.end(), ib, ib + sizeof indices);
    while (bin.size() % 4 != 0) {
        bin.push_back(0);
    }
    {
        std::ofstream file(directory / "quad.bin", std::ios::binary);
        file.write(reinterpret_cast<const char*>(bin.data()),
                   static_cast<std::streamsize>(bin.size()));
    }

    const std::string json = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [ { "mesh": 0 } ],
  "meshes": [ { "primitives": [ {
      "attributes": { "POSITION": 0, "NORMAL": 1 },
      "indices": 2,
      "material": 0
  } ] } ],
  "materials": [ {
    "doubleSided": true,
    "pbrMetallicRoughness": {
      "baseColorFactor": [ 1.0, 1.0, 1.0, 1.0 ],
      "metallicFactor": 0.0,
      "roughnessFactor": 1.0
  } } ],
  "buffers": [ { "uri": "quad.bin", "byteLength": )"
                             + std::to_string(bin.size()) + R"( } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 96, "byteStride": 24 },
    { "buffer": 0, "byteOffset": 96, "byteLength": 12 }
  ],
  "accessors": [
    { "bufferView": 0, "byteOffset": 0,  "componentType": 5126, "count": 4, "type": "VEC3",
      "min": [-4.0, -4.0, 3.0], "max": [4.0, 4.0, 3.0] },
    { "bufferView": 0, "byteOffset": 12, "componentType": 5126, "count": 4, "type": "VEC3" },
    { "bufferView": 1, "byteOffset": 0,  "componentType": 5123, "count": 6, "type": "SCALAR" }
  ]
})";
    std::ofstream file(directory / "quad.gltf", std::ios::binary);
    file << json;
}

struct Rgb {
    int r = 0;
    int g = 0;
    int b = 0;

    [[nodiscard]] std::string text() const {
        return "(" + std::to_string(r) + ", " + std::to_string(g) + ", " + std::to_string(b) + ")";
    }
};

/// Mean colour of a rectangle, so a single stray pixel cannot decide a case.
Rgb averageOf(const std::vector<std::uint8_t>& rgba, int x0, int y0, int x1, int y1) {
    long long r = 0;
    long long g = 0;
    long long b = 0;
    long long n = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * kSize + x) * 4;
            if (i + 3 >= rgba.size()) {
                continue;
            }
            r += rgba[i];
            g += rgba[i + 1];
            b += rgba[i + 2];
            ++n;
        }
    }
    if (n == 0) {
        return {};
    }
    return {static_cast<int>(r / n), static_cast<int>(g / n), static_cast<int>(b / n)};
}

int distanceBetween(Rgb a, Rgb b) {
    const int dr = a.r - b.r;
    const int dg = a.g - b.g;
    const int db = a.b - b.b;
    return dr * dr + dg * dg + db * db;
}

/// The clear colour the world layer uses: pure blue, which nothing drawn can be.
const Rgb kCleared{0, 0, 255};

/// What a covered pixel looks like. Black, and see the file comment for why that
/// is the engine's answer for every material today.
const Rgb kDrawn{0, 0, 0};

bool isNear(Rgb a, Rgb b) {
    return distanceBetween(a, b) < 16 * 16;
}

void exerciseComposite(hp::RenderBackend backend, const char* backendName) {
    Device device = bringUp(backend);
    if (!device.ok()) {
        MESSAGE("no " << std::string(backendName)
                      << " device available -- skipping (this is expected on CI)");
        tearDown(device);
        return;
    }

    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "hp_stack_composite_test";
    std::error_code ec;
    std::filesystem::remove_all(scratch, ec);
    writeQuadGltf(scratch / "models");

    REQUIRE(hp::Vfs::init(nullptr));
    REQUIRE(hp::Vfs::mount(scratch.string()));

    hp::AssetPool pool;
    const hp::Guid quadMesh = hp::Guid::generate();
    {
        auto loaded =
            hp::loadMesh(device.render->device(), device.render->context(), "models/quad.gltf");
        REQUIRE(loaded);
        REQUIRE(loaded->valid());
        // One mesh, shared by both scenes: the material cannot tell them apart
        // anyway, so a second would only suggest it could.
        pool.store<hp::MeshAsset>(quadMesh, loaded);
    }

    const auto addQuad = [&](hp::Scene& scene, const char* name) {
        hp::Entity quad = scene.create(name);
        hp::MeshRenderer renderer;
        renderer.mesh = quadMesh;
        quad.add<hp::MeshRenderer>(renderer);
    };

    // --- the two scenes ------------------------------------------------------

    hp::Scene worldScene;
    {
        hp::Entity camera = worldScene.create("world camera");
        hp::Camera lens;
        lens.viewSlot = 0;
        // **The left half of the target.** Perspective, 60 degrees, and the quad
        // overfills it at z = 3.
        lens.viewport = hp::ViewportRect{0.0F, 0.0F, 0.5F, 1.0F};
        camera.add<hp::Camera>(lens);
        addQuad(worldScene, "world quad");
        worldScene.propagateTransforms();
    }

    hp::Scene hudScene;
    {
        hp::Entity camera = hudScene.create("hud camera");
        hp::Camera lens;
        lens.viewSlot = 1;
        lens.orthographic = true;
        // Half-height 2 against a quad of half-extent 4, so it overfills rather
        // than leaving bands of world showing through the HUD's own half.
        lens.orthographicSize = 2.0F;
        lens.viewport = hp::ViewportRect{0.5F, 0.0F, 0.5F, 1.0F};
        camera.add<hp::Camera>(lens);
        addQuad(hudScene, "hud quad");
        hudScene.propagateTransforms();
    }

    // --- the targets and the stack -------------------------------------------

    hp::FrameTargets targets;
    targets.declare(hp::FrameTargetDesc{"colour", hp::TargetFormat::Colour, 1.0F});
    targets.declare(hp::FrameTargetDesc{"depth", hp::TargetFormat::Depth, 1.0F});
    REQUIRE(targets.create(device.render->device(), kSize, kSize));

    hp::SceneRenderLayer world("world");
    world.order = 0;
    world.clear = hp::LayerClear::ColourAndDepth;
    world.clearColour[0] = 0.0F;
    world.clearColour[1] = 0.0F;
    world.clearColour[2] = 1.0F;
    world.clearColour[3] = 1.0F;
    REQUIRE(world.create(device.render->device(), device.render->context()));
    world.setScene(&worldScene, &pool);

    hp::SceneRenderLayer hud("hud");
    // **Configured before created, and that ordering is load-bearing** — the
    // absence of a depth attachment is baked into the pipeline state, and a
    // state that declares a depth target while none is bound is a render-pass
    // incompatibility rather than a slightly wrong image.
    hp::configureAsHud(hud);
    REQUIRE_FALSE(hud.useDepth);
    REQUIRE(hud.create(device.render->device(), device.render->context()));
    hud.setScene(&hudScene, &pool);

    hp::RenderStack stack;
    // Added in the wrong order on purpose: the stack sorts, callers do not.
    stack.add(&hud);
    stack.add(&world);
    REQUIRE(stack.layers()[0] == &world);

    const auto renderOnce = [&]() {
        return stack.render(device.render->device(), device.render->context(),
                            targets.renderTarget("colour"), targets.depthStencil("depth"),
                            &targets, kSize, kSize, device.render->clipSpace());
    };

    // Well inside each half, so neither samples the seam at x = 128.
    const auto leftHalf = [](const std::vector<std::uint8_t>& p) {
        return averageOf(p, 16, 16, 112, 240);
    };
    const auto rightHalf = [](const std::vector<std::uint8_t>& p) {
        return averageOf(p, 144, 16, 240, 240);
    };

    std::vector<std::uint8_t> pixels;

    SUBCASE("the world layer draws only into its own viewport") {
        // The control, and an assertion in its own right: if the layer took its
        // viewport from the pass size rather than from the resolved camera, the
        // quad would cover the whole frame and the right half would not be blue.
        hud.enabled = false;
        CHECK(renderOnce() == 1);
        REQUIRE(targets.readback(device.render->context(), "colour", pixels));
        REQUIRE(pixels.size() == static_cast<std::size_t>(kSize) * kSize * 4);

        const Rgb left = leftHalf(pixels);
        const Rgb right = rightHalf(pixels);
        MESSAGE("world only: left " << left.text() << ", right " << right.text());

        CHECK(isNear(left, kDrawn));
        CHECK(isNear(right, kCleared));
        CHECK(world.lastFrameHadCamera);
        CHECK(world.lastFrame.submitted == 1);
    }

    SUBCASE("the HUD composites over the world, after it, into the same target") {
        CHECK(renderOnce() == 2);
        REQUIRE(targets.readback(device.render->context(), "colour", pixels));
        REQUIRE(pixels.size() == static_cast<std::size_t>(kSize) * kSize * 4);

        const Rgb left = leftHalf(pixels);
        const Rgb right = rightHalf(pixels);
        MESSAGE("stacked: left " << left.text() << ", right " << right.text());

        CHECK(world.lastFrameHadCamera);
        CHECK(hud.lastFrameHadCamera);
        CHECK(world.lastFrame.submitted == 1);
        CHECK(hud.lastFrame.submitted == 1);

        // **The assertion the subtask exists for.** The right half was blue with
        // the world alone; it is covered now, so the HUD drew into the same
        // target. And it drew *after* the world: the world layer clears colour,
        // so a HUD that ran first would have been erased and this would be blue.
        CHECK(isNear(right, kDrawn));

        // The HUD did not erase what was under it -- `LayerClear::None`.
        CHECK(isNear(left, kDrawn));
    }

    SUBCASE("a HUD with no camera on its slot leaves the world untouched") {
        // The normal state of a scene that has no HUD yet. It must not clear,
        // crash, or blank the frame beneath it.
        hp::Scene empty;
        empty.propagateTransforms();
        hud.setScene(&empty, &pool);

        CHECK(renderOnce() == 2);
        REQUIRE(targets.readback(device.render->context(), "colour", pixels));

        CHECK_FALSE(hud.lastFrameHadCamera);
        CHECK(hud.lastFrame.submitted == 0);
        // Exactly the world-only image: a layer that draws nothing erases
        // nothing.
        CHECK(isNear(leftHalf(pixels), kDrawn));
        CHECK(isNear(rightHalf(pixels), kCleared));
    }

    SUBCASE("the depth-less HUD pipeline is well-formed on the device") {
        // 27.4's other half, and the one with no pixel to look at. A HUD binds no
        // depth target, so its pipeline state must declare none -- a state
        // carrying a DSV format with nothing bound is a render-pass
        // incompatibility, which the validation layers report by losing the
        // device or failing the flush. Surviving a present is the evidence.
        CHECK(renderOnce() == 2);
        device.render->onRender();
        CHECK(device.render->ready());

        // And the guard that names the mistake rather than leaving it to the
        // driver: flipping `useDepth` after `create` makes the state and the
        // binding disagree, and the layer must refuse rather than draw.
        hud.useDepth = true;
        CHECK(renderOnce() == 2);
        CHECK(hud.lastFrame.submitted == 0);
        CHECK_FALSE(hud.lastFrameHadCamera);
        device.render->onRender();
        CHECK(device.render->ready());
        hud.useDepth = false;
    }

    hp::Vfs::shutdown();
    world.release();
    hud.release();
    targets.release();
    tearDown(device);
    std::filesystem::remove_all(scratch, ec);
}

} // namespace

TEST_CASE("a world layer and a HUD layer composite correctly on the default backend"
          * doctest::test_suite("gpu")) {
    exerciseComposite(hp::RenderBackend::Default, "default");
}

TEST_CASE("a world layer and a HUD layer composite correctly on OpenGL"
          * doctest::test_suite("gpu")) {
    exerciseComposite(hp::RenderBackend::OpenGL, "OpenGL");
}
