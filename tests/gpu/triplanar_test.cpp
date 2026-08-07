// Triplanar projection textures a mesh that has no UVs at all (T0141.8).
//
// **That is the defining property, and it is the whole test.** A UV-mapped
// material on a mesh without texture coordinates cannot sample anything —
// DiligentFX's getters return the bare factor and the surface renders flat.
// Triplanar projects from world position instead, so the *same* mesh with the
// *same* textures comes back covered in rock. Asserting "flat without, detailed
// with" on a UV-less quad is a check no other sampling path can pass by
// accident, which is what makes it the second data point on the surface-stage
// question rather than a rerun of the first.
//
// The material is unlit, so detail in the frame is sampled texture and nothing
// else. Bucket: gpu. Skips cleanly with no device or without the textures.

#include <doctest/doctest.h>

#include <hp/Assets.hpp>
#include <hp/Camera.hpp>
#include <hp/Log.hpp>
#include <hp/Material.hpp>
#include <hp/Render.hpp>
#include <hp/Scene.hpp>
#include <hp/SceneView.hpp>
#include <hp/Vfs.hpp>
#include <hp/Window.hpp>

#include <cmath>
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

Device bringUp() {
    static const bool sink = [] {
        hp::logAddConsoleSink();
        return true;
    }();
    (void)sink;

    hp::WindowConfig windowConfig;
    windowConfig.title = "hp triplanar test";
    windowConfig.width = kSize;
    windowConfig.height = kSize;

    Device device;
    device.window = hp::Window::create(windowConfig);
    if (!device.window) {
        return device;
    }
    hp::RenderConfig renderConfig;
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

std::filesystem::path findTextureDir() {
    std::filesystem::path here = std::filesystem::current_path();
    for (int up = 0; up < 6; ++up) {
        const std::filesystem::path candidate = here / "test_assets" / "derived";
        if (std::filesystem::exists(candidate / "rock_basecolour.png")) {
            return candidate;
        }
        if (!here.has_parent_path()) {
            break;
        }
        here = here.parent_path();
    }
    return {};
}

/// A quad with positions and normals only — **no texture coordinates**, which
/// is the case this whole technique exists for.
void writeUvlessQuadGltf(const std::filesystem::path& directory) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    const float vertices[] = {
        -4.0F, -4.0F,-3.0F, 0.0F, 0.0F, 1.0F,
         4.0F, -4.0F,-3.0F, 0.0F, 0.0F, 1.0F,
         4.0F,  4.0F,-3.0F, 0.0F, 0.0F, 1.0F,
        -4.0F,  4.0F,-3.0F, 0.0F, 0.0F, 1.0F,
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
    "pbrMetallicRoughness": { "metallicFactor": 0.0, "roughnessFactor": 1.0 }
  } ],
  "buffers": [ { "uri": "quad.bin", "byteLength": )" + std::to_string(bin.size()) + R"( } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 96, "byteStride": 24 },
    { "buffer": 0, "byteOffset": 96, "byteLength": 12 }
  ],
  "accessors": [
    { "bufferView": 0, "byteOffset": 0,  "componentType": 5126, "count": 4, "type": "VEC3",
      "min": [-4.0, -4.0, -3.0], "max": [4.0, 4.0, -3.0] },
    { "bufferView": 0, "byteOffset": 12, "componentType": 5126, "count": 4, "type": "VEC3" },
    { "bufferView": 1, "byteOffset": 0,  "componentType": 5123, "count": 6, "type": "SCALAR" }
  ]
})";
    std::ofstream file(directory / "quad.gltf", std::ios::binary);
    file << json;
}

std::filesystem::path writePpm(const std::string& name, const std::vector<std::uint8_t>& rgba) {
    std::error_code ec;
    const std::filesystem::path directory = std::filesystem::current_path() / "test-frames";
    std::filesystem::create_directories(directory, ec);
    const std::filesystem::path path = directory / (name + ".ppm");

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    file << "P6\n" << kSize << " " << kSize << "\n255\n";
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        file.put(static_cast<char>(rgba[i]));
        file.put(static_cast<char>(rgba[i + 1]));
        file.put(static_cast<char>(rgba[i + 2]));
    }
    return path;
}

/// Mean absolute luminance deviation — the detail measure the textured test
/// established: a flat fill has none, a photograph of rock has plenty.
double variationOf(const std::vector<std::uint8_t>& rgba) {
    double mean = 0.0;
    long long n = 0;
    std::vector<double> luma;
    luma.reserve(static_cast<std::size_t>(kSize) * kSize);
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        const double value = 0.2126 * rgba[i] + 0.7152 * rgba[i + 1] + 0.0722 * rgba[i + 2];
        luma.push_back(value);
        mean += value;
        ++n;
    }
    if (n == 0) {
        return 0.0;
    }
    mean /= static_cast<double>(n);
    double deviation = 0.0;
    for (const double value : luma) {
        deviation += value > mean ? value - mean : mean - value;
    }
    return deviation / static_cast<double>(n);
}

/// Renders the UV-less quad with the rock base colour assigned.
bool renderQuad(Device& device, bool triplanar, std::vector<std::uint8_t>& pixels) {
    const std::filesystem::path textures = findTextureDir();
    if (textures.empty()) {
        return false;
    }

    std::error_code ec;
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "hp-triplanar";
    std::filesystem::remove_all(scratch, ec);
    std::filesystem::create_directories(scratch / "models", ec);
    std::filesystem::copy_file(textures / "rock_basecolour.png",
                               scratch / "models" / "rock_basecolour.png",
                               std::filesystem::copy_options::overwrite_existing, ec);
    writeUvlessQuadGltf(scratch / "models");

    hp::Vfs::shutdown();
    if (!hp::Vfs::init(nullptr) || !hp::Vfs::mount(scratch.string())) {
        return false;
    }

    hp::AssetPool pool;
    const hp::Guid meshGuid = hp::Guid::generate();
    auto mesh =
        hp::loadMesh(device.render->device(), device.render->context(), "models/quad.gltf");
    if (!mesh || !mesh->valid()) {
        return false;
    }
    pool.store<hp::MeshAsset>(meshGuid, mesh);

    const hp::Guid colourGuid = hp::Guid::generate();
    pool.store<hp::TextureAsset>(
        colourGuid, hp::loadTexture(device.render->device(), "models/rock_basecolour.png"));

    const hp::Guid materialGuid = hp::Guid::generate();
    {
        auto material = std::make_shared<hp::Material>();
        material->baseColourTexture = colourGuid;
        material->triplanar = triplanar;
        // Half a tile per metre over an 8-metre quad: four tiles across the
        // frame, plenty of rock detail without shrinking texels into noise.
        material->triplanarScale = 0.5F;
        material->unlit = true;
        material->doubleSided = true;
        pool.store<hp::Material>(materialGuid, material);
    }

    hp::Scene scene;
    hp::Entity cameraEntity = scene.create("camera");
    cameraEntity.add<hp::Camera>(hp::Camera{});

    hp::Entity quad = scene.create("quad");
    hp::MeshRenderer renderer;
    renderer.mesh = meshGuid;
    renderer.materials = {materialGuid};
    quad.add<hp::MeshRenderer>(renderer);
    scene.propagateTransforms();

    hp::SceneView view;
    if (!view.create(device.render->device(), device.render->context(), kSize, kSize)) {
        return false;
    }
    view.setClearColour(0.0F, 0.0F, 1.0F, 1.0F);

    hp::SceneViewStats stats;
    if (view.render(device.render->context(), scene, pool, 0, &stats) == nullptr) {
        return false;
    }
    if (stats.submitted != 1) {
        return false;
    }
    return view.readback(device.render->context(), pixels);
}

} // namespace

TEST_CASE("triplanar projection textures a mesh that has no UVs") {
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }
    if (findTextureDir().empty()) {
        MESSAGE("test_assets/derived not found; skipping");
        tearDown(device);
        return;
    }

    std::vector<std::uint8_t> flat;
    std::vector<std::uint8_t> projected;
    REQUIRE(renderQuad(device, /*triplanar=*/false, flat));
    REQUIRE(renderQuad(device, /*triplanar=*/true, projected));

    writePpm("triplanar_off", flat);
    writePpm("triplanar_on", projected);

    const double flatVariation = variationOf(flat);
    const double projectedVariation = variationOf(projected);
    MESSAGE("uv-mapped on a UV-less mesh: variation " << flatVariation
                                                      << "; triplanar: " << projectedVariation);

    // **The control pins the premise.** The same material sampled the normal
    // way cannot reach its texture on this mesh — DiligentFX's getters return
    // the factor — so the frame must be essentially flat. If this ever gains
    // detail, the mesh grew UVs and the case stops testing triplanar.
    CHECK(flatVariation < 1.0);

    // Rock, from a mesh that has no way to sample it but world position.
    CHECK(projectedVariation > 8.0);

    hp::Vfs::shutdown();
    tearDown(device);
}
