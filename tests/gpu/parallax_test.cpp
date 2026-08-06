// Parallax occlusion moves the pixels a height map says to move (T0141.7).
//
// **This is the owner's original question rendered and asserted** — "screen
// displacement for texture" was the capability the whole surface stage was
// built to reach, and this is the first test that can only pass if it works.
//
// The assertions are *differential*, deliberately. A parallax image has no
// single right value to pin, but it has a defining property: the height map
// **moves texels**, and how far depends on `heightScale`. So the same oblique
// scene is rendered three ways —
//
//   * no height texture at all,
//   * a height texture at `heightScale = 0`,
//   * the same texture at a working scale —
//
// and the first two must agree (a zero scale marches nowhere) while the third
// must differ from them substantially. The material is **unlit** so every
// difference is UV displacement, not lighting; a lit comparison would smuggle
// the normal map's response in and the assertion would stop meaning parallax.
//
// **The quad is viewed obliquely, and that is load-bearing.** Parallax offsets
// scale with `viewTS.xy / viewTS.z`; a head-on view has viewTS.xy ~ 0 and the
// whole effect collapses to nothing — a test with a facing quad would pass
// with the march deleted. ~40 degrees of yaw is where the effect is obvious.
//
// Bucket: gpu. Skips cleanly with no device or without the committed textures.

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
    windowConfig.title = "hp parallax test";
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

/// The committed texture set, found by walking up from the working directory.
std::filesystem::path findTextureDir() {
    std::filesystem::path here = std::filesystem::current_path();
    for (int up = 0; up < 6; ++up) {
        const std::filesystem::path candidate = here / "test_assets" / "derived";
        if (std::filesystem::exists(candidate / "rock_height.png")) {
            return candidate;
        }
        if (!here.has_parent_path()) {
            break;
        }
        here = here.parent_path();
    }
    return {};
}

/// A quad in its own mesh space (z = 0), so the *entity's* transform can turn
/// it obliquely without swinging it around the camera. UVs, no tangents —
/// parallax builds its frame from derivatives and must not need them.
void writeQuadGltf(const std::filesystem::path& directory) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    const float vertices[] = {
        -4.0F, -4.0F, 0.0F, 0.0F, 0.0F, -1.0F, 0.0F, 1.0F,
         4.0F, -4.0F, 0.0F, 0.0F, 0.0F, -1.0F, 1.0F, 1.0F,
         4.0F,  4.0F, 0.0F, 0.0F, 0.0F, -1.0F, 1.0F, 0.0F,
        -4.0F,  4.0F, 0.0F, 0.0F, 0.0F, -1.0F, 0.0F, 0.0F,
    };
    // Wound consistently with the authored -Z normals (right-hand rule),
    // per WindingConvention/T0152 -- the older suites' `{0,1,2, 0,2,3}` is
    // backwards and is being corrected, not copied.
    const std::uint16_t indices[] = {0, 2, 1, 0, 3, 2};

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
      "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 },
      "indices": 3,
      "material": 0
  } ] } ],
  "materials": [ {
    "doubleSided": true,
    "pbrMetallicRoughness": { "metallicFactor": 0.0, "roughnessFactor": 1.0 }
  } ],
  "buffers": [ { "uri": "quad.bin", "byteLength": )" + std::to_string(bin.size()) + R"( } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 128, "byteStride": 32 },
    { "buffer": 0, "byteOffset": 128, "byteLength": 12 }
  ],
  "accessors": [
    { "bufferView": 0, "byteOffset": 0,  "componentType": 5126, "count": 4, "type": "VEC3",
      "min": [-4.0, -4.0, 0.0], "max": [4.0, 4.0, 0.0] },
    { "bufferView": 0, "byteOffset": 12, "componentType": 5126, "count": 4, "type": "VEC3" },
    { "bufferView": 0, "byteOffset": 24, "componentType": 5126, "count": 4, "type": "VEC2" },
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

/// Mean absolute per-channel difference between two frames.
double meanDifference(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    if (a.size() != b.size() || a.empty()) {
        return -1.0;
    }
    double total = 0.0;
    long long counted = 0;
    for (std::size_t i = 0; i + 3 < a.size(); i += 4) {
        total += std::abs(int(a[i]) - int(b[i])) + std::abs(int(a[i + 1]) - int(b[i + 1])) +
                 std::abs(int(a[i + 2]) - int(b[i + 2]));
        counted += 3;
    }
    return total / static_cast<double>(counted);
}

/// Renders the oblique rock quad. @p withHeight binds the height map;
/// @p heightScale is the material's parallax depth.
bool renderRock(Device& device, bool withHeight, float heightScale,
                std::vector<std::uint8_t>& pixels) {
    const std::filesystem::path textures = findTextureDir();
    if (textures.empty()) {
        return false;
    }

    std::error_code ec;
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "hp-parallax";
    std::filesystem::remove_all(scratch, ec);
    std::filesystem::create_directories(scratch / "models", ec);
    for (const char* name : {"rock_basecolour.png", "rock_height.png"}) {
        std::filesystem::copy_file(textures / name, scratch / "models" / name,
                                   std::filesystem::copy_options::overwrite_existing, ec);
    }
    writeQuadGltf(scratch / "models");

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
    hp::Guid heightGuid;
    if (withHeight) {
        heightGuid = hp::Guid::generate();
        pool.store<hp::TextureAsset>(
            heightGuid, hp::loadTexture(device.render->device(), "models/rock_height.png"));
    }

    const hp::Guid materialGuid = hp::Guid::generate();
    {
        auto material = std::make_shared<hp::Material>();
        material->baseColourTexture = colourGuid;
        material->heightTexture = heightGuid;
        material->heightScale = heightScale;
        // Unlit, so a differing pixel means a displaced texel and nothing
        // else. Lighting would fold the normal map's response into the diff.
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
    // Oblique: yawed ~40 degrees and pushed out to z = 3. Parallax scales
    // with the tangent-plane component of the view direction, which a facing
    // quad has none of.
    hp::Transform& transform = quad.get<hp::Transform>();
    transform.position = hp::float3{0.0F, 0.0F, 3.0F};
    transform.rotation =
        hp::Quaternion::RotationFromAxisAngle(hp::float3{0.0F, 1.0F, 0.0F}, 0.7F);
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

TEST_CASE("parallax occlusion displaces texels by the height map, by heightScale") {
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
    std::vector<std::uint8_t> zeroScale;
    std::vector<std::uint8_t> displaced;
    REQUIRE(renderRock(device, /*withHeight=*/false, 0.0F, flat));
    REQUIRE(renderRock(device, /*withHeight=*/true, 0.0F, zeroScale));
    REQUIRE(renderRock(device, /*withHeight=*/true, 0.08F, displaced));

    writePpm("parallax_off", flat);
    writePpm("parallax_zero_scale", zeroScale);
    writePpm("parallax_on", displaced);

    const double zeroDiff = meanDifference(flat, zeroScale);
    const double onDiff = meanDifference(flat, displaced);
    MESSAGE("no-height vs zero-scale: " << zeroDiff << "; no-height vs displaced: " << onDiff);

    // **A zero scale must march nowhere.** The pipelines differ (the march is
    // compiled into one and not the other), so bit-equality is not owed — but
    // a zero offset times any number of layers is zero, and more than a hair
    // of difference means the march moves texels it was told not to.
    CHECK(zeroDiff >= 0.0);
    CHECK(zeroDiff < 0.5);

    // **A working scale must move them substantially.** The threshold is
    // deliberately far below the measured value and far above noise: rock
    // texel shuffling across an oblique quad changes a large share of the
    // frame's pixels.
    CHECK(onDiff > 3.0);

    hp::Vfs::shutdown();
    tearDown(device);
}
