// A game's `.slang` material renders, overriding only what it overrides
// (T0142.15 — and with it, T0142's headline Done-when).
//
// The module under test is three lines of game code:
//
//     struct HpMaterial : IHpMaterial
//     {
//         override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
//         { return float4(1.0, 0.0, 0.0, 1.0); }
//     }
//
// and the assertions are exact, because unshaded output is exact:
//
//   * with `unlit = true`, the frame is **(255, 0, 0)** — the override's red,
//     straight to the sRGB target. Any other value means the module did not
//     reach the pixels.
//   * with `unlit = false` and **no light**, the frame is **(0, 0, 0)** — the
//     override changed base colour and *nothing else*, so the standard
//     lighting default still governs and an unlit scene is black. This is the
//     partial-override claim D28 makes, measured.
//   * a material naming a shader GUID that is not in the pool renders the
//     missing-material checkerboard (T0141.12's convention, third cause).
//
// Bucket: gpu. Skips cleanly with no device.

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

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr int kSize = 128;

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
    windowConfig.title = "hp custom shader material test";
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

/// A quad with positions and normals, wound per WindingConvention (T0152).
void writeQuadGltf(const std::filesystem::path& directory) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    const float vertices[] = {
        -4.0F, -4.0F, 3.0F, 0.0F, 0.0F, -1.0F,
         4.0F, -4.0F, 3.0F, 0.0F, 0.0F, -1.0F,
         4.0F,  4.0F, 3.0F, 0.0F, 0.0F, -1.0F,
        -4.0F,  4.0F, 3.0F, 0.0F, 0.0F, -1.0F,
    };
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
};

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

/// Counts loud magenta and near-black centre pixels — the checkerboard's
/// signature, as pinned by the material-assignment test.
void countChecks(const std::vector<std::uint8_t>& rgba, int& magenta, int& black) {
    magenta = 0;
    black = 0;
    for (int y = kSize / 4; y < kSize * 3 / 4; ++y) {
        for (int x = kSize / 4; x < kSize * 3 / 4; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * kSize + x) * 4;
            const int r = rgba[i];
            const int g = rgba[i + 1];
            const int b = rgba[i + 2];
            if (r > 200 && b > 200 && g < 60) {
                ++magenta;
            } else if (r < 40 && g < 40 && b < 40) {
                ++black;
            }
        }
    }
}

/// Renders the quad with a custom-shader material.
///
/// @param moduleSource the `.slang` module text, written into the mount; empty
///        writes no module file at all.
/// @param shaderInPool whether the shader asset is loaded into the pool — false
///        exercises the material-names-a-missing-shader path.
/// @param unlit the material's unlit flag.
bool renderCustom(Device& device, const std::string& moduleSource, bool shaderInPool, bool unlit,
                  std::vector<std::uint8_t>& pixels, int frames = 1) {
    std::error_code ec;
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "hp-custom-shader";
    std::filesystem::remove_all(scratch, ec);
    std::filesystem::create_directories(scratch / "models", ec);
    std::filesystem::create_directories(scratch / "materials", ec);
    writeQuadGltf(scratch / "models");
    if (!moduleSource.empty()) {
        std::ofstream file(scratch / "materials" / "custom.slang");
        file << moduleSource;
    }

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

    const hp::Guid shaderGuid = hp::Guid::generate();
    if (shaderInPool) {
        auto shader = hp::loadShader("materials/custom.slang");
        if (!shader || !shader->valid()) {
            return false;
        }
        pool.store<hp::ShaderAsset>(shaderGuid, shader);
    }

    const hp::Guid materialGuid = hp::Guid::generate();
    {
        auto material = std::make_shared<hp::Material>();
        material->shader = shaderGuid;
        material->unlit = unlit;
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
    for (int i = 0; i < frames; ++i) {
        if (view.render(device.render->context(), scene, pool, 0, &stats) == nullptr) {
            return false;
        }
    }
    if (stats.submitted != 1) {
        return false;
    }
    return view.readback(device.render->context(), pixels);
}

/// Counts compiler-failure log lines (the once-per-shader rule of T0141.4).
class CompileErrorCounter final : public hp::ILogSink {
public:
    void write(const hp::LogRecord& record) override {
        if (record.message.find("slang failed to compile") != std::string_view::npos) {
            ++compilerErrors_;
        }
        if (record.message.find("did not compile; rendering the missing-material") !=
            std::string_view::npos) {
            ++substitutions_;
        }
    }
    [[nodiscard]] int compilerErrors() const { return compilerErrors_.load(); }
    [[nodiscard]] int substitutions() const { return substitutions_.load(); }

private:
    std::atomic<int> compilerErrors_{0};
    std::atomic<int> substitutions_{0};
};

/// The module under test: overrides base colour, inherits everything else.
const char* kRedModule = R"(
struct HpMaterial : IHpMaterial
{
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        return float4(1.0, 0.0, 0.0, 1.0);
    }
}
)";

} // namespace

TEST_CASE("a .slang material module overrides one method and inherits the rest") {
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    // Unshaded: the override's red goes straight to the target. Exact.
    std::vector<std::uint8_t> unlitPixels;
    REQUIRE(renderCustom(device, kRedModule, /*shaderInPool=*/true, /*unlit=*/true, unlitPixels));
    const Rgb unlitCentre = centreOf(unlitPixels);
    MESSAGE("custom red module, unlit: (" << unlitCentre.r << ", " << unlitCentre.g << ", "
                                          << unlitCentre.b << ")");
    CHECK(unlitCentre.r == 255);
    CHECK(unlitCentre.g == 0);
    CHECK(unlitCentre.b == 0);

    // Shaded with no light: black — the module changed base colour and
    // *nothing else*, so the standard lighting default still governs. The
    // partial-override claim, measured rather than promised.
    std::vector<std::uint8_t> shadedPixels;
    REQUIRE(
        renderCustom(device, kRedModule, /*shaderInPool=*/true, /*unlit=*/false, shadedPixels));
    const Rgb shadedCentre = centreOf(shadedPixels);
    MESSAGE("same module, shaded, no light: (" << shadedCentre.r << ", " << shadedCentre.g
                                               << ", " << shadedCentre.b << ")");
    CHECK(shadedCentre.r == 0);
    CHECK(shadedCentre.g == 0);
    CHECK(shadedCentre.b == 0);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("a shader that fails to compile renders the checkerboard and logs once") {
    // **T0141.4, and the trap it names.** The pattern is 141.12's — one
    // visual convention, three causes, the console says which — and the log
    // discipline is the part that gets ruined first: a failed shader logged
    // per frame is 3,600 lines a minute. Three frames, one compiler error,
    // one substitution line naming the module.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    CompileErrorCounter counter;
    hp::logAddSink(&counter);

    const char* kBrokenModule = R"(
struct HpMaterial : IHpMaterial
{
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        return this does not parse;
    }
}
)";
    std::vector<std::uint8_t> pixels;
    const bool rendered =
        renderCustom(device, kBrokenModule, /*shaderInPool=*/true, /*unlit=*/false, pixels,
                     /*frames=*/3);
    hp::logRemoveSink(&counter);
    REQUIRE(rendered);

    int magenta = 0;
    int black = 0;
    countChecks(pixels, magenta, black);
    MESSAGE("broken shader: magenta " << magenta << ", compiler errors "
                                      << counter.compilerErrors() << ", substitutions "
                                      << counter.substitutions());

    // The fallback, loud and unlit (there is no light in this scene). Flat
    // magenta rather than checks, because this quad has no UVs — the same
    // documented degradation the missing-shader case pins.
    const int centrePixels = (kSize / 2) * (kSize / 2);
    CHECK(magenta > (centrePixels * 3) / 4);

    // Logged on the transition: one compiler error and one renderer line
    // across three frames — never per draw, never per frame.
    CHECK(counter.compilerErrors() == 1);
    CHECK(counter.substitutions() == 1);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("a material whose shader is missing renders the checkerboard") {
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    // The shader GUID is set on the material but never loaded into the pool.
    // No light in the scene, so the magenta arriving bright is also the proof
    // the fallback stayed unlit through this path.
    //
    // **Flat magenta, not checks, and that is the convention working**: this
    // quad has no texture coordinates, so the checkerboard texture has
    // nothing to tile by and `GetBaseColor` returns the fallback material's
    // magenta factor — the documented UV-less degradation of T0141.12's
    // pattern. The material-assignment test covers the textured checks; this
    // one pins that the *shader*-missing cause reaches the same fallback.
    std::vector<std::uint8_t> pixels;
    REQUIRE(renderCustom(device, "", /*shaderInPool=*/false, /*unlit=*/false, pixels));

    int magenta = 0;
    int black = 0;
    countChecks(pixels, magenta, black);
    MESSAGE("missing shader: magenta " << magenta << ", black " << black);
    const int centrePixels = (kSize / 2) * (kSize / 2);
    CHECK(magenta > (centrePixels * 3) / 4);

    hp::Vfs::shutdown();
    tearDown(device);
}
