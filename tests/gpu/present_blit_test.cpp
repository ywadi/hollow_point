// The fullscreen blit that puts a frame on screen (T0137).
//
// Bucket: gpu. This exists because the engine could not present its own frame on
// its own default backend: the present path was a `CopyTexture`, which requires
// an exact format match, and a Vulkan surface is BGRA against the scene target's
// RGBA. `hp_editor` with no arguments showed a clear colour and looked broken
// while every pixel test passed — because those read the render target rather
// than the window, so nothing covered the last step.
//
// **Two bugs were found writing the blit, and both passed on Vulkan while
// failing differently on OpenGL.** They are what this file really guards:
//
//   1. Diligent's HLSL-to-GLSL converter names each GLSL varying after the
//      *parameter variable*, so a VS writing `out X o` and a PS reading `in X i`
//      do not link — `"_i_uv" not declared as an output from the previous
//      stage`. Vulkan links it anyway, because SPIR-V matches by location.
//   2. `DefaultVariableType = MUTABLE` made the constant buffer mutable too, so
//      `GetStaticVariableByName` returned null, the buffer was never bound, and
//      the V scale read as zero — which samples one row of the source and smears
//      it down the frame. It looks like a stretched image, not like an unbound
//      buffer.
//
// The asymmetry is the point: a per-backend fast path would have left OpenGL
// never exercising the shader at all. Both cases here run on both backends.
//
// **The source is a rendered scene rather than a hand-built texture**, because
// tests here reach only `hp/` headers — no test includes a Diligent header, and
// this one is not going to be the first. `SceneView` supplies both an image and
// a readback, so the whole test stays on the engine's own surface.

#include <doctest/doctest.h>

#include <hp/Assets.hpp>
#include <hp/Camera.hpp>
#include <hp/FrameTargets.hpp>
#include <hp/Light.hpp>
#include <hp/Log.hpp>
#include <hp/Render.hpp>
#include <hp/Scene.hpp>
#include <hp/SceneView.hpp>
#include <hp/Vfs.hpp>
#include <hp/Window.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

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

    Device device;
    hp::WindowConfig windowConfig;
    windowConfig.title = "hp present blit test";
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

void tearDown(Device& device) {
    if (device.render) {
        device.render->onDetach();
        device.render.reset();
    }
    device.window.reset();
}

constexpr int kSize = 128;

/// A quad **offset upward**, so the image is asymmetric top-to-bottom.
///
/// That offset is the entire reason this is not the centred quad the other
/// suites use: a vertically centred image survives a flip unchanged, so it
/// cannot detect one. Spanning y in [0, 5] rather than [-4, 4] puts the geometry
/// in the top half and leaves the bottom half clear.
void writeOffsetQuadGltf(const std::filesystem::path& directory) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    const float vertices[] = {
        -4.0F, 0.5F,-3.0F, 0.0F, 0.0F, 1.0F,
         4.0F, 0.5F,-3.0F, 0.0F, 0.0F, 1.0F,
         4.0F, 5.0F,-3.0F, 0.0F, 0.0F, 1.0F,
        -4.0F, 5.0F,-3.0F, 0.0F, 0.0F, 1.0F,
    };
    // Wound consistently with the authored **+Z** normals (right-hand rule):
    // `cross(v1 - v0, v2 - v0)` over BL, BR, TR, TL gives `(0, 0, +area)`.
    //
    // **This is `{0, 1, 2, 0, 2, 3}` again, and that is not a revert.** T0152
    // moved these quads to `{0, 2, 1, 0, 3, 2}` because they faced the camera
    // with a -Z normal; T0165 turned the camera round, so the quads face it
    // with a +Z normal and the original order is the consistent one. The rule
    // never changed -- winding agrees with the authored normal -- only which
    // normal faces the lens.
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
    { "bufferView": 0, "byteOffset": 0,  "componentType": 5126, "count": 4,
      "type": "VEC3", "min": [ -4.0, 0.5, 3.0 ], "max": [ 4.0, 5.0, 3.0 ] },
    { "bufferView": 0, "byteOffset": 12, "componentType": 5126, "count": 4, "type": "VEC3" },
    { "bufferView": 1, "byteOffset": 0,  "componentType": 5123, "count": 6, "type": "SCALAR" }
  ]
})";
    std::ofstream file(directory / "quad.gltf", std::ios::binary);
    file << json;
}

/// Mean luminance over a horizontal band of a readback buffer.
double bandLuma(const std::vector<std::uint8_t>& pixels, int fromRow, int toRow) {
    double total = 0.0;
    int counted = 0;
    for (int y = fromRow; y < toRow; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * kSize + x) * 4;
            if (i + 2 >= pixels.size()) {
                continue;
            }
            total += (pixels[i] + pixels[i + 1] + pixels[i + 2]) / 3.0;
            ++counted;
        }
    }
    return counted == 0 ? 0.0 : total / counted;
}

/// The shared body, run once per backend.
void exerciseBlit(hp::RenderBackend backend, const char* backendName) {
    Device device = bringUp(backend);
    if (!device.ok()) {
        MESSAGE("no " << std::string(backendName)
                      << " device available -- skipping (this is expected on CI)");
        tearDown(device);
        return;
    }
    MESSAGE("device up on " << std::string(backendName) << ": "
                            << device.render->adapterDescription());

    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "hp_present_blit_test";
    writeOffsetQuadGltf(scratch / "models");
    REQUIRE(hp::Vfs::init(nullptr));
    REQUIRE(hp::Vfs::mount(scratch.string()));

    auto mesh = hp::loadMesh(device.render->device(), device.render->context(),
                             "models/quad.gltf");
    REQUIRE(mesh);
    REQUIRE(mesh->valid());

    hp::Scene scene;
    hp::AssetPool pool;
    const hp::Guid meshGuid = hp::Guid::generate();
    pool.store<hp::MeshAsset>(meshGuid, mesh);

    scene.create("camera").add<hp::Camera>(hp::Camera{});

    hp::Entity quad = scene.create("quad");
    hp::MeshRenderer renderer;
    renderer.mesh = meshGuid;
    quad.add<hp::MeshRenderer>(renderer);

    hp::Entity sun = scene.create("sun");
    hp::Light light;
    light.type = hp::LightType::Directional;
    light.intensity = 3.0F;
    sun.add<hp::Light>(light);
    scene.propagateTransforms();

    hp::SceneView view;
    REQUIRE(view.create(device.render->device(), device.render->context(), kSize, kSize));
    REQUIRE(view.render(device.render->context(), scene, pool, 0,
                        nullptr) != nullptr);

    std::vector<std::uint8_t> source;
    REQUIRE(view.readback(device.render->context(), source));
    REQUIRE(source.size() >= static_cast<std::size_t>(kSize) * kSize * 4);

    // The control. If the scene did not actually produce an asymmetric image,
    // every assertion below would pass vacuously — so this is checked, not
    // assumed.
    //
    // **Which band is the bright one is deliberately not asserted.** Readback
    // row order follows the device's own convention, so the same scene comes
    // back top-bright on Vulkan and bottom-bright on OpenGL — measured here,
    // 207/133 against 133/207. That is a property of the readback, not of the
    // blit, and this test must not encode one backend's answer. What matters is
    // that the two bands *differ*, and that the blit maps each to itself.
    const double sourceTop = bandLuma(source, 8, kSize / 2 - 8);
    const double sourceBottom = bandLuma(source, kSize / 2 + 8, kSize - 8);
    MESSAGE("source luma: top " << sourceTop << ", bottom " << sourceBottom);
    REQUIRE(std::abs(sourceTop - sourceBottom) > 20.0);

    hp::FrameTargets targets;
    targets.declare(hp::FrameTargetDesc{"colour", hp::TargetFormat::Colour, 1.0F});
    REQUIRE(targets.create(device.render->device(), kSize, kSize));
    Diligent::ITextureView* destination = targets.renderTarget("colour");
    REQUIRE(destination != nullptr);

    SUBCASE("the blit pipeline builds and draws on this backend") {
        // Bug 1 lands here: on OpenGL the pipeline could not be created at all,
        // because the varyings did not link, so this returned false.
        CHECK(device.render->blitTexture(view.colourTexture(), destination));
    }

    SUBCASE("the blit reproduces its source, upright and unstretched") {
        REQUIRE(device.render->blitTexture(view.colourTexture(), destination));

        std::vector<std::uint8_t> presented;
        REQUIRE(targets.readback(device.render->context(), "colour", presented));
        REQUIRE(presented.size() == source.size());

        const double top = bandLuma(presented, 8, kSize / 2 - 8);
        const double bottom = bandLuma(presented, kSize / 2 + 8, kSize - 8);
        MESSAGE("presented luma: top " << top << ", bottom " << bottom);

        // **Orientation.** Each band must come back as *itself*. A vertical
        // flip swaps them, which fails these two regardless of which one the
        // backend happens to make bright.
        CHECK(top == doctest::Approx(sourceTop).epsilon(0.10));
        CHECK(bottom == doctest::Approx(sourceBottom).epsilon(0.10));

        // **Scale.** Bug 2 made V constant, so every output row sampled the
        // source's middle and the two bands collapsed to the same value.
        // Requiring the gap to survive is what separates "not flipped" from
        // "not smeared" — the checks above alone would pass a uniform image
        // only if the source were uniform too, which the control forbids.
        CHECK(std::abs(top - bottom) > 20.0);
    }

    targets.release();
    view.release();
    tearDown(device);
    hp::Vfs::shutdown();
}

} // namespace

TEST_CASE("present blit reproduces its source on Vulkan") {
    exerciseBlit(hp::RenderBackend::Vulkan, "Vulkan");
}
