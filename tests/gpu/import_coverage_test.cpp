// What an imported glTF's own material data is allowed to ask of the renderer
// (T0168.2), and the one warning the import owes (T0168.6).
//
// Bucket: gpu. Every case here is a feature the loader parsed correctly all
// along while the draw path dropped it on the floor — `KHR_materials_unlit`
// shaded lit, `KHR_texture_transform` silently untransformed,
// `KHR_materials_pbrSpecularGlossiness` misread as metallic-roughness, and a
// multi-scene file drawn from `Scenes[0]` instead of the file's default. Each
// assertion below fails on the code as it was before T0168.2, which is what
// makes them regression tests rather than demonstrations.
//
// **Assets are synthesised here rather than checked in** — the same argument
// as `asset_import_test.cpp`: a binary fixture is something nobody reviews.
// The PNG writer mirrors `tangent_frame_test.cpp`'s (stored deflate blocks,
// hand-rolled CRC), kept local because the suites deliberately share no
// helper library.

#include <doctest/doctest.h>

#include <hp/Assets.hpp>
#include <hp/Camera.hpp>
#include <hp/Light.hpp>
#include <hp/Log.hpp>
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

#if HP_TESTS_HAVE_DRACO
// The encoder half of the same library the engine decodes with, so the
// `KHR_draco_mesh_compression` case can synthesise its asset here instead of
// checking in an opaque binary fixture (T0168.3). Linked in
// `tests/CMakeLists.txt`, guarded so a build without draco still compiles the
// rest of this file.
#include "draco/compression/decode.h"
#include "draco/compression/encode.h"
#include "draco/mesh/triangle_soup_mesh_builder.h"
#endif

namespace {

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
    windowConfig.title = "hp import coverage test";
    windowConfig.width = 256;
    windowConfig.height = 256;

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

/// Captures warnings so the required-extension cases can assert on them.
/// Copies every view — `LogRecord`'s views die when `write` returns.
class CapturingSink final : public hp::ILogSink {
public:
    struct Entry {
        hp::LogLevel level;
        std::string category;
        std::string message;
    };
    void write(const hp::LogRecord& record) override {
        entries.push_back(Entry{record.level, std::string(record.category),
                                std::string(record.message)});
    }
    std::vector<Entry> entries;
};

// --- a tiny PNG, for the texture-transform case ------------------------------

void pushBe32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

std::uint32_t crc32Of(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

void pushChunk(std::vector<std::uint8_t>& out, const char* tag,
               const std::vector<std::uint8_t>& body) {
    pushBe32(out, static_cast<std::uint32_t>(body.size()));
    std::vector<std::uint8_t> crcInput;
    crcInput.insert(crcInput.end(), tag, tag + 4);
    crcInput.insert(crcInput.end(), body.begin(), body.end());
    out.insert(out.end(), crcInput.begin(), crcInput.end());
    pushBe32(out, crc32Of(crcInput.data(), crcInput.size()));
}

void writePng(const std::filesystem::path& path, int width, int height,
              const std::vector<std::uint8_t>& rgba) {
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(height) * (1U + static_cast<std::size_t>(width) * 4U));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0); // filter type 0 (None)
        const auto* row = rgba.data() + static_cast<std::size_t>(y) * width * 4;
        raw.insert(raw.end(), row, row + static_cast<std::size_t>(width) * 4);
    }

    std::vector<std::uint8_t> zlib{0x78, 0x01};
    std::size_t offset = 0;
    while (offset < raw.size()) {
        const std::size_t chunk = std::min<std::size_t>(65535, raw.size() - offset);
        const bool last = offset + chunk >= raw.size();
        zlib.push_back(last ? 1 : 0);
        zlib.push_back(static_cast<std::uint8_t>(chunk & 0xFFU));
        zlib.push_back(static_cast<std::uint8_t>((chunk >> 8U) & 0xFFU));
        const auto inverse = static_cast<std::uint16_t>(~static_cast<std::uint16_t>(chunk));
        zlib.push_back(static_cast<std::uint8_t>(inverse & 0xFFU));
        zlib.push_back(static_cast<std::uint8_t>((inverse >> 8U) & 0xFFU));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset + chunk));
        offset += chunk;
    }
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::uint8_t byte : raw) {
        a = (a + byte) % 65521U;
        b = (b + a) % 65521U;
    }
    pushBe32(zlib, (b << 16U) | a);

    std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<std::uint8_t> ihdr;
    pushBe32(ihdr, static_cast<std::uint32_t>(width));
    pushBe32(ihdr, static_cast<std::uint32_t>(height));
    ihdr.push_back(8); // bit depth
    ihdr.push_back(6); // colour type RGBA
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    pushChunk(png, "IHDR", ihdr);
    pushChunk(png, "IDAT", zlib);
    pushChunk(png, "IEND", {});

    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(png.data()),
               static_cast<std::streamsize>(png.size()));
}

// --- glTF synthesis ----------------------------------------------------------

/// The camera-facing quad every case draws: `scene_draws_mesh_test`'s geometry
/// (camera looks down its −Z, quad at z = −3, +Z normals, winding to match),
/// with UVs added because two cases sample a texture. UV `u` spans 0 … 0.5 so
/// the untransformed texture read stays inside the left half of the image.
std::vector<unsigned char> quadBin() {
    const float vertices[] = {
        // position            normal              uv
        -4.0F, -4.0F, -3.0F,   0.0F, 0.0F, 1.0F,   0.0F, 1.0F,
         4.0F, -4.0F, -3.0F,   0.0F, 0.0F, 1.0F,   0.5F, 1.0F,
         4.0F,  4.0F, -3.0F,   0.0F, 0.0F, 1.0F,   0.5F, 0.0F,
        -4.0F,  4.0F, -3.0F,   0.0F, 0.0F, 1.0F,   0.0F, 0.0F,
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
    return bin;
}

/// One quad, one material, mesh 0 in scene 0 — the material JSON and the
/// top-level extension declarations are the parameters, everything else is
/// shared. Writes `<name>.bin` beside `<name>.gltf`.
void writeQuadGltf(const std::filesystem::path& directory, const std::string& name,
                   const std::string& materialJson, const std::string& topLevelJson,
                   bool withTexture = false) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    const std::vector<unsigned char> bin = quadBin();
    {
        std::ofstream file(directory / (name + ".bin"), std::ios::binary);
        file.write(reinterpret_cast<const char*>(bin.data()),
                   static_cast<std::streamsize>(bin.size()));
    }

    std::string json = R"({
  "asset": { "version": "2.0" },
)" + topLevelJson + R"(
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [ { "mesh": 0 } ],
  "meshes": [ { "primitives": [ {
      "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 },
      "indices": 3,
      "material": 0
  } ] } ],
  "materials": [ )" + materialJson + R"( ],
)";
    if (withTexture) {
        json += R"(  "textures": [ { "source": 0 } ],
  "images": [ { "uri": "halves.png" } ],
)";
    }
    json += R"(  "buffers": [ { "uri": ")" + name + R"(.bin", "byteLength": )" +
            std::to_string(bin.size()) + R"( } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 128, "byteStride": 32 },
    { "buffer": 0, "byteOffset": 128, "byteLength": 12 }
  ],
  "accessors": [
    { "bufferView": 0, "byteOffset": 0,  "componentType": 5126, "count": 4, "type": "VEC3",
      "min": [-4.0, -4.0, -3.0], "max": [4.0, 4.0, -3.0] },
    { "bufferView": 0, "byteOffset": 12, "componentType": 5126, "count": 4, "type": "VEC3" },
    { "bufferView": 0, "byteOffset": 24, "componentType": 5126, "count": 4, "type": "VEC2" },
    { "bufferView": 1, "byteOffset": 0,  "componentType": 5123, "count": 6, "type": "SCALAR" }
  ]
})";
    std::ofstream file(directory / (name + ".gltf"), std::ios::binary);
    file << json;
}

/// Two scenes, two quads, two unlit materials — red in scene 0, green in
/// scene 1 — and `"scene": 1`. A renderer that draws `Scenes[0]` shows red;
/// one that honours the file's default shows green.
void writeTwoSceneGltf(const std::filesystem::path& directory, const std::string& name) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    const std::vector<unsigned char> bin = quadBin();
    {
        std::ofstream file(directory / (name + ".bin"), std::ios::binary);
        file.write(reinterpret_cast<const char*>(bin.data()),
                   static_cast<std::streamsize>(bin.size()));
    }

    const std::string json = R"({
  "asset": { "version": "2.0" },
  "extensionsUsed": [ "KHR_materials_unlit" ],
  "scene": 1,
  "scenes": [ { "nodes": [ 0 ] }, { "nodes": [ 1 ] } ],
  "nodes": [ { "mesh": 0 }, { "mesh": 1 } ],
  "meshes": [
    { "primitives": [ { "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 },
        "indices": 3, "material": 0 } ] },
    { "primitives": [ { "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 },
        "indices": 3, "material": 1 } ] }
  ],
  "materials": [
    { "doubleSided": true, "extensions": { "KHR_materials_unlit": {} },
      "pbrMetallicRoughness": { "baseColorFactor": [ 0.8, 0.05, 0.05, 1.0 ] } },
    { "doubleSided": true, "extensions": { "KHR_materials_unlit": {} },
      "pbrMetallicRoughness": { "baseColorFactor": [ 0.05, 0.8, 0.05, 1.0 ] } }
  ],
  "buffers": [ { "uri": ")" + name + R"(.bin", "byteLength": )" +
                             std::to_string(bin.size()) + R"( } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 128, "byteStride": 32 },
    { "buffer": 0, "byteOffset": 128, "byteLength": 12 }
  ],
  "accessors": [
    { "bufferView": 0, "byteOffset": 0,  "componentType": 5126, "count": 4, "type": "VEC3",
      "min": [-4.0, -4.0, -3.0], "max": [4.0, 4.0, -3.0] },
    { "bufferView": 0, "byteOffset": 12, "componentType": 5126, "count": 4, "type": "VEC3" },
    { "bufferView": 0, "byteOffset": 24, "componentType": 5126, "count": 4, "type": "VEC2" },
    { "bufferView": 1, "byteOffset": 0,  "componentType": 5123, "count": 6, "type": "SCALAR" }
  ]
})";
    std::ofstream file(directory / (name + ".gltf"), std::ios::binary);
    file << json;
}

// --- measurement -------------------------------------------------------------

struct Rgb {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    [[nodiscard]] double luma() const { return 0.2126 * r + 0.7152 * g + 0.0722 * b; }
};

/// Mean RGB over the pixels that are not the pure-blue clear colour.
Rgb meanOfCovered(const std::vector<std::uint8_t>& rgba, long long& covered) {
    Rgb sum;
    covered = 0;
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        if (rgba[i] == 0 && rgba[i + 1] == 0 && rgba[i + 2] == 255) {
            continue;
        }
        sum.r += rgba[i];
        sum.g += rgba[i + 1];
        sum.b += rgba[i + 2];
        ++covered;
    }
    if (covered > 0) {
        sum.r /= static_cast<double>(covered);
        sum.g /= static_cast<double>(covered);
        sum.b /= static_cast<double>(covered);
    }
    return sum;
}

/// The magenta guard (T0159.7): fraction of covered pixels that are the
/// missing-material checkerboard's loud magenta. A failed shader passes
/// coverage assertions while rendering exactly this, measured twice.
double magentaShareOfCovered(const std::vector<std::uint8_t>& rgba) {
    long long magenta = 0;
    long long covered = 0;
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        if (rgba[i] == 0 && rgba[i + 1] == 0 && rgba[i + 2] == 255) {
            continue;
        }
        ++covered;
        if (rgba[i] > 200 && rgba[i + 2] > 200 && rgba[i + 1] < 60) {
            ++magenta;
        }
    }
    return covered > 0 ? static_cast<double>(magenta) / static_cast<double>(covered) : 0.0;
}

/// One rendered frame of one imported model, with the shared guards applied.
struct Frame {
    std::vector<std::uint8_t> pixels;
    Rgb mean;
    long long covered = 0;
    bool ok = false;
};

/// Imports `modelPath`, draws it once and reads the frame back. `lit` adds the
/// tilted directional lamp the spec-gloss case shades by; unlit cases leave the
/// scene lamp-free on purpose, because "bright with zero lights" *is* their
/// assertion.
Frame renderModel(Device& device, const std::string& modelPath, bool lit) {
    Frame frame;

    hp::AssetPool pool;
    const hp::Guid meshGuid = hp::Guid::generate();
    auto mesh = hp::loadMesh(device.render->device(), device.render->context(), modelPath);
    if (!mesh || !mesh->valid()) {
        return frame;
    }
    pool.store<hp::MeshAsset>(meshGuid, mesh);

    hp::Scene scene;
    hp::Entity cameraEntity = scene.create("camera");
    cameraEntity.add<hp::Camera>(hp::Camera{});

    hp::Entity quad = scene.create("quad");
    hp::MeshRenderer renderer;
    renderer.mesh = meshGuid;
    quad.add<hp::MeshRenderer>(renderer);

    if (lit) {
        // Tilted ~35° about X, deliberately off the camera axis: with the SG
        // materials' glossiness defaulting to 1 (roughness 0), a lamp straight
        // down the view axis puts the whole quad at the specular delta's peak,
        // and the case would measure a blown highlight instead of the diffuse
        // term it asserts on.
        hp::Entity lightEntity = scene.create("sun");
        hp::Light sun;
        sun.type = hp::LightType::Directional;
        sun.colour = hp::float3{1.0F, 1.0F, 1.0F};
        sun.intensity = 3.0F;
        lightEntity.add<hp::Light>(sun);
        hp::Transform placement;
        const float half = 0.3054F; // sin(35°/2)
        placement.rotation = hp::Quaternion{half, 0.0F, 0.0F, std::sqrt(1.0F - half * half)};
        scene.setLocalTransform(lightEntity, placement);
    }
    scene.propagateTransforms();

    hp::SceneView view;
    if (!view.create(device.render->device(), device.render->context(), 256, 256)) {
        return frame;
    }
    view.setClearColour(0.0F, 0.0F, 1.0F, 1.0F);

    hp::SceneViewStats stats;
    Diligent::ITextureView* published =
        view.render(device.render->context(), scene, pool, 0, &stats);
    if (published == nullptr || stats.submitted != 1) {
        view.release();
        return frame;
    }
    if (!view.readback(device.render->context(), frame.pixels)) {
        view.release();
        return frame;
    }
    view.release();

    frame.mean = meanOfCovered(frame.pixels, frame.covered);
    frame.ok = true;
    return frame;
}

} // namespace

TEST_CASE("imported glTF material features reach the pixels they were parsed for"
          * doctest::test_suite("gpu")) {
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no device available -- skipping (this is expected on CI)");
        tearDown(device);
        return;
    }

    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "hp_import_coverage_test";
    std::error_code ec;
    std::filesystem::remove_all(scratch, ec);
    const std::filesystem::path models = scratch / "models";

    // ---- the synthesised corpus --------------------------------------------
    writeQuadGltf(models, "unlit_red",
                  R"({ "doubleSided": true,
      "extensions": { "KHR_materials_unlit": {} },
      "pbrMetallicRoughness": { "baseColorFactor": [ 0.8, 0.05, 0.05, 1.0 ],
                                "metallicFactor": 0.0, "roughnessFactor": 1.0 } })",
                  R"(  "extensionsUsed": [ "KHR_materials_unlit" ],
)");
    writeQuadGltf(models, "lit_red",
                  R"({ "doubleSided": true,
      "pbrMetallicRoughness": { "baseColorFactor": [ 0.8, 0.05, 0.05, 1.0 ],
                                "metallicFactor": 0.0, "roughnessFactor": 1.0 } })",
                  "");
    writeQuadGltf(models, "sg_diffuse",
                  R"({ "doubleSided": true,
      "extensions": { "KHR_materials_pbrSpecularGlossiness": {
          "diffuseFactor": [ 0.1, 0.5, 0.9, 1.0 ],
          "specularFactor": [ 0.0, 0.0, 0.0 ] } } })",
                  R"(  "extensionsUsed": [ "KHR_materials_pbrSpecularGlossiness" ],
  "extensionsRequired": [ "KHR_materials_pbrSpecularGlossiness" ],
)");
    writeQuadGltf(models, "sg_specular",
                  R"({ "doubleSided": true,
      "extensions": { "KHR_materials_pbrSpecularGlossiness": {
          "diffuseFactor": [ 0.1, 0.5, 0.9, 1.0 ],
          "specularFactor": [ 1.0, 1.0, 1.0 ] } } })",
                  R"(  "extensionsUsed": [ "KHR_materials_pbrSpecularGlossiness" ],
  "extensionsRequired": [ "KHR_materials_pbrSpecularGlossiness" ],
)");
    // Unlit + textured, so the transform's effect is the frame's colour with
    // no lighting in the way. UV u spans 0 … 0.5 — the left (red) half of the
    // image — and the transform's offset walks the read into the green half.
    writeQuadGltf(models, "uv_plain",
                  R"({ "doubleSided": true,
      "extensions": { "KHR_materials_unlit": {} },
      "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } } })",
                  R"(  "extensionsUsed": [ "KHR_materials_unlit" ],
)",
                  /*withTexture=*/true);
    writeQuadGltf(models, "uv_offset",
                  R"({ "doubleSided": true,
      "extensions": { "KHR_materials_unlit": {} },
      "pbrMetallicRoughness": { "baseColorTexture": { "index": 0,
          "extensions": { "KHR_texture_transform": { "offset": [ 0.5, 0.0 ] } } } } })",
                  R"(  "extensionsUsed": [ "KHR_materials_unlit", "KHR_texture_transform" ],
)",
                  /*withTexture=*/true);
    writeQuadGltf(models, "requires_fake",
                  R"({ "doubleSided": true,
      "pbrMetallicRoughness": { "baseColorFactor": [ 1.0, 1.0, 1.0, 1.0 ] } })",
                  R"(  "extensionsUsed": [ "HP_nonexistent_compression" ],
  "extensionsRequired": [ "HP_nonexistent_compression" ],
)");
    writeTwoSceneGltf(models, "two_scenes");

    // Left half red, right half green, 8×8.
    {
        std::vector<std::uint8_t> rgba(8 * 8 * 4, 0);
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                std::uint8_t* px = rgba.data() + (static_cast<std::size_t>(y) * 8 + x) * 4;
                px[0] = x < 4 ? 255 : 0;
                px[1] = x < 4 ? 0 : 255;
                px[3] = 255;
            }
        }
        writePng(models / "halves.png", 8, 8, rgba);
    }

    REQUIRE(hp::Vfs::init(nullptr));
    REQUIRE(hp::Vfs::mount(scratch.string()));

    CapturingSink sink;
    hp::logAddSink(&sink);

    SUBCASE("KHR_materials_unlit renders full base colour with zero lights") {
        // The scene has no lamp at all. Before T0168.2 the unlit quad compiled
        // the lit permutation and came out as black as its metallic-roughness
        // twin; the twin is rendered too, to prove darkness is what the lit
        // path does here rather than a failure to draw.
        const Frame unlit = renderModel(device, "models/unlit_red.gltf", /*lit=*/false);
        const Frame lit = renderModel(device, "models/lit_red.gltf", /*lit=*/false);
        CHECK(unlit.ok);
        CHECK(lit.ok);
        if (unlit.ok && lit.ok) {
            CHECK(magentaShareOfCovered(unlit.pixels) < 0.05);
            MESSAGE("unlit mean " << unlit.mean.r << "," << unlit.mean.g << "," << unlit.mean.b
                                  << "  lit-no-lamp mean " << lit.mean.r << "," << lit.mean.g
                                  << "," << lit.mean.b);
            // Full base colour: red-dominant and bright. Measured 186 on the
            // RTX 2080 (the render path applies its own output transfer), so
            // the bound is set to separate "bright red" from "black" rather
            // than to pin one rasterizer's exact transfer curve.
            CHECK(unlit.mean.r > 150.0);
            CHECK(unlit.mean.g < 100.0);
            CHECK(unlit.mean.b < 100.0);
            // The lit twin under zero lamps is nearly black -- and much darker
            // than the unlit quad, which is the regression assertion.
            CHECK(lit.mean.luma() < unlit.mean.luma() * 0.25);
        }
    }

    SUBCASE("spec-gloss shades from diffuse and specular factors, not their MR misread") {
        const Frame diffuse = renderModel(device, "models/sg_diffuse.gltf", /*lit=*/true);
        const Frame specular = renderModel(device, "models/sg_specular.gltf", /*lit=*/true);
        CHECK(diffuse.ok);
        CHECK(specular.ok);
        if (diffuse.ok && specular.ok) {
            CHECK(magentaShareOfCovered(diffuse.pixels) < 0.05);
            CHECK(magentaShareOfCovered(specular.pixels) < 0.05);
            MESSAGE("sg zero-specular mean " << diffuse.mean.r << "," << diffuse.mean.g << ","
                                             << diffuse.mean.b << "  white-specular mean "
                                             << specular.mean.r << "," << specular.mean.g << ","
                                             << specular.mean.b);
            // The zero-specular material is pure diffuse: lit, and carrying
            // `diffuseFactor`'s blue-over-red ordering. Under the old MR
            // misread the factor was never mapped and the quad shaded from
            // BaseColorFactor's default white.
            CHECK(diffuse.mean.luma() > 25.0);
            CHECK(diffuse.mean.b > diffuse.mean.r * 1.5);
            // White specular at glossiness 1: `oneMinusSpecularStrength` is 0,
            // so the diffuse term dies and the off-peak delta lobe leaves the
            // frame far darker. Before T0168.2 `SpecularFactor` was unread and
            // these two files rendered identically.
            CHECK(specular.mean.luma() < diffuse.mean.luma() * 0.5);
        }
    }

    SUBCASE("KHR_texture_transform moves the base colour read") {
        const Frame plain = renderModel(device, "models/uv_plain.gltf", /*lit=*/false);
        const Frame offset = renderModel(device, "models/uv_offset.gltf", /*lit=*/false);
        CHECK(plain.ok);
        CHECK(offset.ok);
        if (plain.ok && offset.ok) {
            CHECK(magentaShareOfCovered(plain.pixels) < 0.05);
            CHECK(magentaShareOfCovered(offset.pixels) < 0.05);
            MESSAGE("untransformed mean " << plain.mean.r << "," << plain.mean.g
                                          << "  offset mean " << offset.mean.r << ","
                                          << offset.mean.g);
            // The control reads the left (red) half of the image…
            CHECK(plain.mean.r > plain.mean.g * 2.0);
            // …and the 0.5 offset walks the same UVs into the green half.
            // Before T0168.2 the flag was never raised for imported materials
            // and both frames came out red.
            CHECK(offset.mean.g > offset.mean.r * 2.0);
        }
    }

    SUBCASE("a multi-scene file draws its default scene, not scene zero") {
        const Frame frame = renderModel(device, "models/two_scenes.gltf", /*lit=*/false);
        CHECK(frame.ok);
        if (frame.ok) {
            CHECK(magentaShareOfCovered(frame.pixels) < 0.05);
            MESSAGE("two-scene mean " << frame.mean.r << "," << frame.mean.g << ","
                                      << frame.mean.b);
            // Scene 1 is the file's default and its quad is green; scene 0's
            // is red, and red here means `Scenes[0]` is back. Same bound
            // reasoning as the unlit case: 186 measured, 150 asserted.
            CHECK(frame.mean.g > 150.0);
            CHECK(frame.mean.r < 100.0);
        }
    }

#if HP_TESTS_HAVE_DRACO
    SUBCASE("a KHR_draco_mesh_compression mesh decodes, renders, and does not warn (168.3)") {
        // **The Done-when's "Draco loads", measured end to end**: encode the
        // same camera-facing quad with draco's own encoder, write a glTF that
        // *requires* the extension, and import it through the engine — the
        // decode happens inside tinygltf behind `TINYGLTF_ENABLE_DRACO`,
        // which existed all along and was never compiled in until the
        // `draco` submodule supplied the target (T0168.3).
        //
        // The JSON's counts come from a **decode round-trip here**, not from
        // the input mesh: the encoder is free to reorder and weld points, and
        // an accessor count that disagrees with the decoded point count is a
        // malformed file, not a test.
        draco::TriangleSoupMeshBuilder builder;
        builder.Start(2);
        const int posAttrib =
            builder.AddAttribute(draco::GeometryAttribute::POSITION, 3, draco::DT_FLOAT32);
        const float bl[3] = {-4.0F, -4.0F, -3.0F};
        const float br[3] = {4.0F, -4.0F, -3.0F};
        const float tr[3] = {4.0F, 4.0F, -3.0F};
        const float tl[3] = {-4.0F, 4.0F, -3.0F};
        builder.SetAttributeValuesForFace(posAttrib, draco::FaceIndex(0), bl, br, tr);
        builder.SetAttributeValuesForFace(posAttrib, draco::FaceIndex(1), bl, tr, tl);
        std::unique_ptr<draco::Mesh> soup = builder.Finalize();
        REQUIRE(soup != nullptr);

        draco::Encoder encoder;
        draco::EncoderBuffer encoded;
        REQUIRE(encoder.EncodeMeshToBuffer(*soup, &encoded).ok());

        draco::DecoderBuffer decoderBuffer;
        decoderBuffer.Init(encoded.data(), encoded.size());
        draco::Decoder decoder;
        auto roundTrip = decoder.DecodeMeshFromBuffer(&decoderBuffer);
        REQUIRE(roundTrip.ok());
        const draco::Mesh& decoded = *roundTrip.value();
        const auto* decodedPosition =
            decoded.GetNamedAttribute(draco::GeometryAttribute::POSITION);
        REQUIRE(decodedPosition != nullptr);
        MESSAGE("draco round trip: " << encoded.size() << " bytes, "
                                     << decoded.num_points() << " points, "
                                     << decoded.num_faces() << " faces");

        {
            std::ofstream file(models / "quad_draco.bin", std::ios::binary);
            file.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
        }
        const std::string json = R"({
  "asset": { "version": "2.0" },
  "extensionsUsed": [ "KHR_draco_mesh_compression", "KHR_materials_unlit" ],
  "extensionsRequired": [ "KHR_draco_mesh_compression" ],
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [ { "mesh": 0 } ],
  "meshes": [ { "primitives": [ {
      "attributes": { "POSITION": 0 },
      "indices": 1,
      "material": 0,
      "extensions": { "KHR_draco_mesh_compression": {
          "bufferView": 0,
          "attributes": { "POSITION": )" +
                                 std::to_string(decodedPosition->unique_id()) + R"( } } }
  } ] } ],
  "materials": [ { "doubleSided": true,
      "extensions": { "KHR_materials_unlit": {} },
      "pbrMetallicRoughness": { "baseColorFactor": [ 0.8, 0.05, 0.05, 1.0 ] } } ],
  "buffers": [ { "uri": "quad_draco.bin", "byteLength": )" +
                                 std::to_string(encoded.size()) + R"( } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": )" +
                                 std::to_string(encoded.size()) + R"( } ],
  "accessors": [
    { "componentType": 5126, "count": )" +
                                 std::to_string(decoded.num_points()) + R"(, "type": "VEC3",
      "min": [-4.0, -4.0, -3.0], "max": [4.0, 4.0, -3.0] },
    { "componentType": 5125, "count": )" +
                                 std::to_string(decoded.num_faces() * 3) + R"(, "type": "SCALAR" }
  ]
})";
        {
            std::ofstream file(models / "quad_draco.gltf", std::ios::binary);
            file << json;
        }

        sink.entries.clear();
        const Frame frame = renderModel(device, "models/quad_draco.gltf", /*lit=*/false);
        CHECK(frame.ok);
        if (frame.ok) {
            CHECK(magentaShareOfCovered(frame.pixels) < 0.05);
            MESSAGE("draco quad mean " << frame.mean.r << "," << frame.mean.g << ","
                                       << frame.mean.b << " over " << frame.covered
                                       << " covered pixels");
            // The unlit red quad, decoded from a compressed stream: bright,
            // red-dominant, and covering a real share of the frame.
            CHECK(frame.mean.r > 150.0);
            CHECK(frame.mean.g < 100.0);
            CHECK(frame.covered > 1000);
        }
        // Draco is end-to-end now, so a file *requiring* it must not warn.
        for (const auto& entry : sink.entries) {
            const bool dracoWarning =
                entry.level == hp::LogLevel::Warning &&
                entry.message.find("KHR_draco_mesh_compression") != std::string::npos;
            CHECK_FALSE(dracoWarning);
        }
    }
#endif

    SUBCASE("pirate.glb, genuine DCC output, fails loudly rather than silently (166.7)") {
        // `third_party/meshoptimizer/demo/pirate.glb` was T0166.7's "render
        // something nobody here authored" — and reading its container answered
        // differently: gltfpack 0.14 output, `extensionsRequired` carries
        // **both** `KHR_mesh_quantization` and `EXT_meshopt_compression`, and
        // meshopt decode is absent at every layer of this stack
        // (14-asset-import-matrix.md). So the honest assertion is not a
        // render: it is that the import **says so** — each required extension
        // named in a warning — instead of the silent wrong-geometry failure
        // D35 exists to prevent. The first real file to exercise T0168.6's
        // warning, and it is exactly the file class (a Sketchfab-style
        // compressed download) the warning was built for.
        std::filesystem::path repo = std::filesystem::current_path();
        bool found = false;
        for (int up = 0; up < 6 && !found; ++up) {
            if (std::filesystem::exists(repo / "third_party" / "meshoptimizer" / "demo" /
                                        "pirate.glb")) {
                found = true;
                break;
            }
            if (!repo.has_parent_path()) {
                break;
            }
            repo = repo.parent_path();
        }
        if (!found) {
            MESSAGE("pirate.glb not found from the working directory; skipping");
        } else {
            REQUIRE(hp::Vfs::mount((repo / "third_party" / "meshoptimizer" / "demo").string()));
            sink.entries.clear();
            auto mesh = hp::loadMesh(device.render->device(), device.render->context(),
                                     "pirate.glb");
            bool warnedMeshopt = false;
            bool warnedQuantization = false;
            for (const auto& entry : sink.entries) {
                if (entry.level != hp::LogLevel::Warning) {
                    continue;
                }
                warnedMeshopt = warnedMeshopt ||
                                entry.message.find("EXT_meshopt_compression") != std::string::npos;
                warnedQuantization =
                    warnedQuantization ||
                    entry.message.find("KHR_mesh_quantization") != std::string::npos;
            }
            CHECK(warnedMeshopt);
            CHECK(warnedQuantization);
            const std::string verdict = (mesh != nullptr && mesh->valid()) ? "yes" : "no";
            MESSAGE("pirate.glb loaded: " << verdict
                                          << " (either is acceptable; the warning is the "
                                             "contract, rendering it is not)");
        }
    }

    SUBCASE("an unsupported required extension warns by name; a supported one is silent") {
        sink.entries.clear();
        const Frame fake = renderModel(device, "models/requires_fake.gltf", /*lit=*/false);
        CHECK(fake.ok); // it loads and renders -- the warning is advice, not a refusal
        bool warned = false;
        for (const auto& entry : sink.entries) {
            if (entry.level == hp::LogLevel::Warning &&
                entry.message.find("HP_nonexistent_compression") != std::string::npos) {
                warned = true;
            }
        }
        CHECK(warned);

        sink.entries.clear();
        const Frame sg = renderModel(device, "models/sg_diffuse.gltf", /*lit=*/true);
        CHECK(sg.ok);
        for (const auto& entry : sink.entries) {
            const bool sgWarning =
                entry.level == hp::LogLevel::Warning &&
                entry.message.find("KHR_materials_pbrSpecularGlossiness") != std::string::npos;
            CHECK_FALSE(sgWarning);
        }
    }

    hp::logRemoveSink(&sink);
    hp::Vfs::shutdown();
    tearDown(device);
    std::filesystem::remove_all(scratch, ec);
}
