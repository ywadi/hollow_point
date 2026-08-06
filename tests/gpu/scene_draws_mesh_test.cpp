// The one test that proves T0028 renders: a real mesh, drawn, and the pixels
// read back.
//
// Bucket: gpu. **Everything else in this ticket can pass while nothing is
// drawn.** Pipeline states can build, a draw call can be issued, statistics can
// report a submission -- and the frame can still come back as pure clear colour
// because the depth comparison rejected every fragment. That is precisely what a
// reverse-Z mistake looks like (T0130), and reading the pixels is the only thing
// that distinguishes it from success.

#include <doctest/doctest.h>

#include <hp/Assets.hpp>
#include <hp/Camera.hpp>
#include <hp/CameraSystem.hpp>
#include <hp/Log.hpp>
#include <hp/Render.hpp>
#include <hp/Scene.hpp>
#include <hp/SceneView.hpp>
#include <hp/Vfs.hpp>
#include <hp/Window.hpp>

#include <cstdint>
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

    hp::WindowConfig windowConfig;
    windowConfig.title = "hp mesh draw test";
    windowConfig.width = 256;
    windowConfig.height = 256;

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

/// A quad squarely in front of a default camera, written as glTF.
///
/// **`doubleSided` is true on purpose.** This test asks one question — do
/// fragments reach the target — and back-face culling would answer it "no" for a
/// winding mistake, which is a different bug with the same symptom. Winding is
/// worth its own test; conflating them here would make a culling error look like
/// a depth error.
///
/// The engine is left-handed and a default camera at the origin looks down +Z,
/// so the quad sits at z = 3 and spans well beyond the frustum edges at that
/// distance — it should fill the frame.
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

/// How many pixels differ from the clear colour by more than a rounding wobble.
std::size_t pixelsUnlike(const std::vector<std::uint8_t>& rgba, std::uint8_t r, std::uint8_t g,
                         std::uint8_t b) {
    std::size_t count = 0;
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        const int dr = static_cast<int>(rgba[i]) - r;
        const int dg = static_cast<int>(rgba[i + 1]) - g;
        const int db = static_cast<int>(rgba[i + 2]) - b;
        if (dr * dr + dg * dg + db * db > 12 * 12) {
            ++count;
        }
    }
    return count;
}

void exerciseMeshDraw(hp::RenderBackend backend, const char* backendName) {
    Device device = bringUp(backend);
    if (!device.ok()) {
        MESSAGE("no " << std::string(backendName)
                      << " device available -- skipping (this is expected on CI)");
        tearDown(device);
        return;
    }

    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "hp_mesh_draw_test";
    std::error_code ec;
    std::filesystem::remove_all(scratch, ec);
    writeQuadGltf(scratch / "models");

    REQUIRE(hp::Vfs::init(nullptr));
    REQUIRE(hp::Vfs::mount(scratch.string()));

    hp::AssetPool pool;
    const hp::Guid meshGuid = hp::Guid::generate();
    auto mesh = hp::loadMesh(device.render->device(), device.render->context(),
                             "models/quad.gltf");
    REQUIRE(mesh);
    REQUIRE(mesh->valid());
    pool.store<hp::MeshAsset>(meshGuid, mesh);

    hp::Scene scene;
    hp::Entity cameraEntity = scene.create("camera");
    cameraEntity.add<hp::Camera>(hp::Camera{});

    hp::Entity quad = scene.create("quad");
    hp::MeshRenderer renderer;
    renderer.mesh = meshGuid;
    quad.add<hp::MeshRenderer>(renderer);
    scene.propagateTransforms();

    hp::SceneView view;
    REQUIRE(view.create(device.render->device(), device.render->context(), 256, 256));
    // A clear colour nothing in the mesh could coincidentally match.
    view.setClearColour(0.0F, 0.0F, 1.0F, 1.0F);

    hp::SceneViewStats stats;
    Diligent::ITextureView* published = view.render(device.render->context(), scene, pool,
                                                    device.render->clipSpace(), 0, &stats);

    REQUIRE(published != nullptr);
    CHECK(stats.hadCamera);
    CHECK(stats.considered == 1);
    CHECK(stats.missingMesh == 0);
    // The mesh resolved from the pool by GUID and a draw was issued.
    CHECK(stats.submitted == 1);

    std::vector<std::uint8_t> pixels;
    REQUIRE(view.readback(device.render->context(), pixels));
    REQUIRE(pixels.size() == 256U * 256U * 4U);

    // **This is the assertion the whole ticket rests on.** Everything above can
    // pass with a completely empty image: `submitted == 1` says a draw was
    // *issued*, not that anything survived the depth test. Under a reversed
    // depth comparison every fragment is rejected and this count is zero.
    const std::size_t drawn = pixelsUnlike(pixels, 0, 0, 255);
    MESSAGE("pixels differing from the clear colour: " << drawn << " of " << (256 * 256));
    CHECK(drawn > 1000);

    hp::Vfs::shutdown();
    view.release();
    tearDown(device);
    std::filesystem::remove_all(scratch, ec);
}

} // namespace

TEST_CASE("a mesh is drawn and reaches the target on the default backend"
          * doctest::test_suite("gpu")) {
    exerciseMeshDraw(hp::RenderBackend::Default, "default");
}
