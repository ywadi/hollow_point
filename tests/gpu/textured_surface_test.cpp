// A textured surface, with its pixels asserted (T0141.11).
//
// **This is the guard T0134 could not write.** That ticket fixed an unwritten
// `PBRFrameAttribs::Renderer`, whose observable symptom is a garbage `MipBias`
// fed into every texture sample — and it went unnoticed because nothing in the
// engine had ever drawn a *textured* material. Every mesh so far sampled the
// renderer's 1x1 default textures, where a garbage mip bias selects the only mip
// there is.
//
// It is also the first test to exercise the surface stage against real texture
// data rather than constant factors (T0141.10, D26).
//
// Bucket: gpu. Skips cleanly with no device.
//
// **It writes the frame out as a PPM.** Not decoration: a shading bug is
// enormously easier to recognise by eye than by three channel comparisons, and
// the assertions below deliberately check *relationships* rather than exact
// values, so the image is what turns "this passed" into "this is right".

#include <doctest/doctest.h>

#include <hp/Assets.hpp>
#include <hp/Camera.hpp>
#include <hp/Light.hpp>
#include <hp/Log.hpp>
#include <hp/Render.hpp>
#include <hp/RenderStack.hpp>
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
    windowConfig.title = "hp textured surface test";
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

/// Where the committed 512px texture set lives, relative to the repository root.
///
/// Resolved by walking up from the working directory rather than assuming one:
/// the harness runs a test binary from the build tree, and hard-coding a
/// relative depth breaks the moment that layout changes.
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

/// A quad with texture coordinates and tangents, referencing external images.
///
/// **Tangents are written explicitly.** A normal map without them has no basis
/// to be transformed into world space, and `HpSurfaceInput::Tangent` documents
/// zero as the "mesh has no tangents" case — so a test meaning to exercise
/// normal mapping has to supply them or it silently exercises the fallback.
void writeTexturedQuadGltf(const std::filesystem::path& directory, const std::string& prefix) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    // position(3) normal(3) tangent(4) uv(2) = 12 floats per vertex.
    const float vertices[] = {
        -4.0F, -4.0F, 3.0F,  0.0F, 0.0F, -1.0F,  1.0F, 0.0F, 0.0F, 1.0F,  0.0F, 1.0F,
         4.0F, -4.0F, 3.0F,  0.0F, 0.0F, -1.0F,  1.0F, 0.0F, 0.0F, 1.0F,  1.0F, 1.0F,
         4.0F,  4.0F, 3.0F,  0.0F, 0.0F, -1.0F,  1.0F, 0.0F, 0.0F, 1.0F,  1.0F, 0.0F,
        -4.0F,  4.0F, 3.0F,  0.0F, 0.0F, -1.0F,  1.0F, 0.0F, 0.0F, 1.0F,  0.0F, 0.0F,
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
      "attributes": { "POSITION": 0, "NORMAL": 1, "TANGENT": 2, "TEXCOORD_0": 3 },
      "indices": 4,
      "material": 0
  } ] } ],
  "materials": [ {
    "doubleSided": true,
    "normalTexture": { "index": 1 },
    "occlusionTexture": { "index": 2 },
    "pbrMetallicRoughness": {
      "baseColorTexture": { "index": 0 },
      "metallicRoughnessTexture": { "index": 2 },
      "metallicFactor": 1.0,
      "roughnessFactor": 1.0
  } } ],
  "images": [
    { "uri": ")" + prefix + R"(_basecolour.png" },
    { "uri": ")" + prefix + R"(_normal.png" },
    { "uri": ")" + prefix + R"(_orm.png" }
  ],
  "textures": [ { "source": 0 }, { "source": 1 }, { "source": 2 } ],
  "buffers": [ { "uri": "quad.bin", "byteLength": )" + std::to_string(bin.size()) + R"( } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 192, "byteStride": 48 },
    { "buffer": 0, "byteOffset": 192, "byteLength": 12 }
  ],
  "accessors": [
    { "bufferView": 0, "byteOffset": 0,  "componentType": 5126, "count": 4, "type": "VEC3",
      "min": [-4.0, -4.0, 3.0], "max": [4.0, 4.0, 3.0] },
    { "bufferView": 0, "byteOffset": 12, "componentType": 5126, "count": 4, "type": "VEC3" },
    { "bufferView": 0, "byteOffset": 24, "componentType": 5126, "count": 4, "type": "VEC4" },
    { "bufferView": 0, "byteOffset": 40, "componentType": 5126, "count": 4, "type": "VEC2" },
    { "bufferView": 1, "byteOffset": 0,  "componentType": 5123, "count": 6, "type": "SCALAR" }
  ]
})";
    std::ofstream file(directory / "quad.gltf", std::ios::binary);
    file << json;
}

/// Writes the frame as a binary PPM, next to the test binary.
///
/// **A shading bug is far easier to recognise by eye than by channel
/// arithmetic**, and the assertions here deliberately check relationships rather
/// than exact values — so the image is what turns "this passed" into "this is
/// right". PPM because it is eight lines to write and needs no encoder; anything
/// that reads images reads it.
/// @returns the path written, or empty on failure.
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

struct Rgb {
    int r = 0;
    int g = 0;
    int b = 0;
    [[nodiscard]] std::string text() const {
        return "(" + std::to_string(r) + ", " + std::to_string(g) + ", " + std::to_string(b) + ")";
    }
};

/// The mean of the middle half, so a single hot texel cannot decide the result.
Rgb centreOf(const std::vector<std::uint8_t>& rgba) {
    long long r = 0;
    long long g = 0;
    long long b = 0;
    long long n = 0;
    for (int y = kSize / 4; y < kSize * 3 / 4; ++y) {
        for (int x = kSize / 4; x < kSize * 3 / 4; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * kSize + x) * 4;
            r += rgba[i];
            g += rgba[i + 1];
            b += rgba[i + 2];
            ++n;
        }
    }
    return Rgb{static_cast<int>(r / n), static_cast<int>(g / n), static_cast<int>(b / n)};
}

/// How much the frame varies, as the mean absolute deviation of luminance.
///
/// **This is the assertion a textured render actually needs.** An average colour
/// cannot tell a photograph of rock from a flat brown square, so it cannot tell a
/// working texture path from a broken one that samples a single texel — which is
/// precisely what a garbage `MipBias` produces (T0134). Detail is the signal.
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

/// Renders one textured quad and returns the frame.
bool renderTextured(Device& device, const std::string& prefix, std::vector<std::uint8_t>& pixels) {
    const std::filesystem::path textures = findTextureDir();
    if (textures.empty()) {
        return false;
    }

    std::error_code ec;
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / ("hp-textured-" + prefix);
    std::filesystem::remove_all(scratch, ec);
    std::filesystem::create_directories(scratch / "models", ec);

    // Beside the glTF, because its image URIs are relative to it.
    for (const char* suffix : {"_basecolour.png", "_normal.png", "_orm.png"}) {
        std::filesystem::copy_file(textures / (prefix + suffix),
                                   scratch / "models" / (prefix + suffix),
                                   std::filesystem::copy_options::overwrite_existing, ec);
    }
    writeTexturedQuadGltf(scratch / "models", prefix);

    hp::Vfs::shutdown();
    if (!hp::Vfs::init(nullptr) || !hp::Vfs::mount(scratch.string())) {
        return false;
    }

    hp::AssetPool pool;
    const hp::Guid meshGuid = hp::Guid::generate();
    auto mesh = hp::loadMesh(device.render->device(), device.render->context(), "models/quad.gltf");
    if (!mesh || !mesh->valid()) {
        return false;
    }
    pool.store<hp::MeshAsset>(meshGuid, mesh);

    hp::Scene scene;
    hp::Entity cameraEntity = scene.create("camera");
    cameraEntity.add<hp::Camera>(hp::Camera{});

    hp::Entity quad = scene.create("quad");
    hp::MeshRenderer renderer;
    renderer.mesh = meshGuid;
    quad.add<hp::MeshRenderer>(renderer);

    hp::Entity lightEntity = scene.create("sun");
    hp::Light sun;
    sun.type = hp::LightType::Directional;
    sun.colour = hp::float3{1.0F, 1.0F, 1.0F};
    sun.intensity = 3.0F;
    lightEntity.add<hp::Light>(sun);
    scene.propagateTransforms();

    hp::SceneView view;
    if (!view.create(device.render->device(), device.render->context(), kSize, kSize)) {
        return false;
    }
    // Blue, so "nothing drew" stays distinguishable from every shading outcome.
    view.setClearColour(0.0F, 0.0F, 1.0F, 1.0F);

    hp::SceneViewStats stats;
    if (view.render(device.render->context(), scene, pool, device.render->clipSpace(), 0, &stats) ==
        nullptr) {
        return false;
    }
    if (stats.submitted != 1) {
        return false;
    }
    return view.readback(device.render->context(), pixels);
}

} // namespace

TEST_CASE("a textured surface renders its texture, not an average of it") {
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

    SUBCASE("rock, a dielectric") {
        std::vector<std::uint8_t> pixels;
        REQUIRE(renderTextured(device, "rock", pixels));

        const Rgb centre = centreOf(pixels);
        const double variation = variationOf(pixels);
        const std::filesystem::path image = writePpm("rock", pixels);
        MESSAGE("rock: centre " << centre.text() << ", variation " << variation << " -> "
                                << image.string());

        // Something drew, and it is not the blue clear colour.
        CHECK(centre.b < centre.r);
        // **The assertion this test exists for.** A garbage `MipBias` (T0134)
        // samples one mip for every fragment, and the result is flat -- so
        // detail, not colour, is what distinguishes a working texture path from
        // a broken one. A photograph of rock has plenty; a flat fill has none.
        CHECK(variation > 8.0);
    }

    SUBCASE("metal, which the rock set cannot test") {
        // **A texture set that is entirely dielectric cannot catch a metallic
        // term wired to the wrong channel**, because zero is the right answer
        // either way. This one has real metalness in its ORM blue channel.
        std::vector<std::uint8_t> pixels;
        REQUIRE(renderTextured(device, "metal", pixels));

        const Rgb centre = centreOf(pixels);
        const double variation = variationOf(pixels);
        const std::filesystem::path image = writePpm("metal", pixels);
        MESSAGE("metal: centre " << centre.text() << ", variation " << variation << " -> "
                                 << image.string());

        CHECK(centre.b < 250);
        CHECK(variation > 8.0);
    }

    tearDown(device);
}
