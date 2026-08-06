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
#include <hp/Math.hpp>
#include <hp/Render.hpp>
#include <hp/Scene.hpp>
#include <hp/SceneView.hpp>
#include <hp/Vfs.hpp>
#include <hp/Window.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
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
///
/// @param withTangents also writes a `TANGENT` accessor — glTF's `VEC4`, world
///        +X with `w = 1`. What T0159.4's contract cases render with: the
///        loader drops `w` (their wire format is `float3`), and the engine
///        publishes the interpolated tangent through `HpSurfaceInput`.
void writeQuadGltf(const std::filesystem::path& directory, bool withTangents = false) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    std::vector<unsigned char> bin;
    if (withTangents) {
        // position(3) normal(3) tangent(4) = 10 floats, stride 40.
        const float vertices[] = {
            -4.0F, -4.0F, 3.0F, 0.0F, 0.0F, -1.0F, 1.0F, 0.0F, 0.0F, 1.0F,
             4.0F, -4.0F, 3.0F, 0.0F, 0.0F, -1.0F, 1.0F, 0.0F, 0.0F, 1.0F,
             4.0F,  4.0F, 3.0F, 0.0F, 0.0F, -1.0F, 1.0F, 0.0F, 0.0F, 1.0F,
            -4.0F,  4.0F, 3.0F, 0.0F, 0.0F, -1.0F, 1.0F, 0.0F, 0.0F, 1.0F,
        };
        const auto* vb = reinterpret_cast<const unsigned char*>(vertices);
        bin.insert(bin.end(), vb, vb + sizeof vertices);
    } else {
        const float vertices[] = {
            -4.0F, -4.0F, 3.0F, 0.0F, 0.0F, -1.0F,
             4.0F, -4.0F, 3.0F, 0.0F, 0.0F, -1.0F,
             4.0F,  4.0F, 3.0F, 0.0F, 0.0F, -1.0F,
            -4.0F,  4.0F, 3.0F, 0.0F, 0.0F, -1.0F,
        };
        const auto* vb = reinterpret_cast<const unsigned char*>(vertices);
        bin.insert(bin.end(), vb, vb + sizeof vertices);
    }
    const std::size_t vertexBytes = bin.size();
    const std::uint16_t indices[] = {0, 2, 1, 0, 3, 2};
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

    const std::string attributes =
        withTangents ? R"("POSITION": 0, "NORMAL": 1, "TANGENT": 2)"
                     : R"("POSITION": 0, "NORMAL": 1)";
    const std::string stride = withTangents ? "40" : "24";
    const std::string tangentAccessor =
        withTangents
            ? R"(,
    { "bufferView": 0, "byteOffset": 24, "componentType": 5126, "count": 4, "type": "VEC4" })"
            : "";
    const std::string indexAccessor = withTangents ? "3" : "2";

    const std::string json = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [ { "mesh": 0 } ],
  "meshes": [ { "primitives": [ {
      "attributes": { )" + attributes + R"( },
      "indices": )" + indexAccessor + R"(,
      "material": 0
  } ] } ],
  "materials": [ {
    "doubleSided": true,
    "pbrMetallicRoughness": { "metallicFactor": 0.0, "roughnessFactor": 1.0 }
  } ],
  "buffers": [ { "uri": "quad.bin", "byteLength": )" + std::to_string(bin.size()) + R"( } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": )" + std::to_string(vertexBytes) +
                             R"(, "byteStride": )" + stride + R"( },
    { "buffer": 0, "byteOffset": )" + std::to_string(vertexBytes) + R"(, "byteLength": 12 }
  ],
  "accessors": [
    { "bufferView": 0, "byteOffset": 0,  "componentType": 5126, "count": 4, "type": "VEC3",
      "min": [-4.0, -4.0, 3.0], "max": [4.0, 4.0, 3.0] },
    { "bufferView": 0, "byteOffset": 12, "componentType": 5126, "count": 4, "type": "VEC3" })" +
                             tangentAccessor + R"(,
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
/// @param customise runs on the material before it is stored, for the
///        declared-parameter cases (T0160). Given the placeholder texture's
///        GUID, already in the pool, so a case that wants a texture in a module
///        slot has one to bind.
/// @param worldOffset a local translation put on the quad entity, so
///        `ObjectToWorld` is something other than the identity for the vertex
///        contract's cases (T0146) — an identity matrix would let a wrong
///        matrix pass.
bool renderCustom(Device& device, const std::string& moduleSource, bool shaderInPool, bool unlit,
                  std::vector<std::uint8_t>& pixels, int frames = 1, double timeSeconds = 0.0,
                  bool withTangents = false,
                  const std::function<void(hp::Material&, hp::Guid)>& customise = {},
                  hp::float3 worldOffset = hp::float3{0.0F, 0.0F, 0.0F}) {
    std::error_code ec;
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "hp-custom-shader";
    std::filesystem::remove_all(scratch, ec);
    std::filesystem::create_directories(scratch / "models", ec);
    std::filesystem::create_directories(scratch / "materials", ec);
    writeQuadGltf(scratch / "models", withTangents);
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

    // A texture for a module slot to bind, in the pool whether a case uses it
    // or not: the checkerboard is the one image every test here already knows,
    // and its 4-pixel checks make a fixed sample coordinate a fixed colour.
    const hp::Guid textureGuid = hp::Guid::generate();
    if (auto placeholder = hp::makePlaceholderTexture(device.render->device())) {
        pool.store<hp::TextureAsset>(textureGuid, std::move(placeholder));
    }

    const hp::Guid materialGuid = hp::Guid::generate();
    {
        auto material = std::make_shared<hp::Material>();
        material->shader = shaderGuid;
        material->unlit = unlit;
        material->doubleSided = true;
        if (customise) {
            customise(*material, textureGuid);
        }
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
    if (worldOffset.x != 0.0F || worldOffset.y != 0.0F || worldOffset.z != 0.0F) {
        hp::Transform placed;
        placed.position = worldOffset;
        scene.setLocalTransform(quad, placed);
    }
    scene.propagateTransforms();

    hp::SceneView view;
    if (!view.create(device.render->device(), device.render->context(), kSize, kSize)) {
        return false;
    }
    view.setClearColour(0.0F, 0.0F, 1.0F, 1.0F);

    hp::SceneViewStats stats;
    for (int i = 0; i < frames; ++i) {
        if (view.render(device.render->context(), scene, pool, 0, &stats, timeSeconds) ==
            nullptr) {
            return false;
        }
    }
    if (stats.submitted != 1) {
        return false;
    }
    return view.readback(device.render->context(), pixels);
}

/// Captures the SPIR-V byte counts the compiler logs at debug level, keyed on
/// the pixel shader — the instrument for "did the lighting actually fold".
class SpirvSizeCapture final : public hp::ILogSink {
public:
    void write(const hp::LogRecord& record) override {
        const std::string_view message = record.message;
        // The pixel entry point specifically (T0146.5): one file now compiles
        // to two stages and logs a line for each, so the old substring
        // (`compiled 'HpSurface.slang' to `) would capture whichever came last.
        if (message.find("compiled 'HpSurface.slang' (psMain) to ") == std::string_view::npos) {
            return;
        }
        const std::size_t start = message.find(" to ") + 4;
        const std::size_t end = message.find(" bytes", start);
        if (end == std::string_view::npos) {
            return;
        }
        lastBytes_ = std::stoi(std::string(message.substr(start, end - start)));
    }
    [[nodiscard]] int lastBytes() const { return lastBytes_.load(); }
    void reset() { lastBytes_ = 0; }

private:
    std::atomic<int> lastBytes_{0};
};

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

TEST_CASE("a module can opt out of lighting, and the lighting folds away") {
    // **T0142.16: unshaded as an interface method with a default** — Godot's
    // `render_mode unshaded`, in D28's one shape for every option. Two claims,
    // both measured:
    //
    //   * the *semantics*: a module overriding `unshaded()` renders its base
    //     colour exactly, in a scene with **no light** — (255, 255, 0) can
    //     only arrive through the unshaded path, because the shaded one is
    //     black here;
    //   * the *cost*, and this measurement came back **negative**: the sizes
    //     are captured below and printed, and they are within a dozen bytes
    //     of each other — slang emits the specialised call and a real branch,
    //     and does not fold the lighting out at the SPIR-V level (optimization
    //     level none and default both measured). The claim "compile-time in
    //     effect" is therefore *not* asserted; T0151's link-time constants
    //     own the provable mechanism, and the ticket records the hand-off.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    const std::string kShadedYellow = R"(
struct HpMaterial : IHpMaterial
{
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        return float4(1.0, 1.0, 0.0, 1.0);
    }
}
)";
    const std::string kUnshadedYellow = R"(
struct HpMaterial : IHpMaterial
{
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        return float4(1.0, 1.0, 0.0, 1.0);
    }
    override bool unshaded()
    {
        return true;
    }
}
)";

    SpirvSizeCapture sizes;
    hp::logAddSink(&sizes);
    hp::logSetGlobalLevel(hp::LogLevel::Debug);

    // A unique token per run, appended as a comment: the SPIR-V cache is
    // content-keyed, so without this a second run of the suite serves both
    // modules from cache, nothing compiles, and there are no sizes to
    // capture. A comment changes the content hash and nothing else.
    const std::string probe = "// probe " + hp::Guid::generate().toString() + "\n";

    std::vector<std::uint8_t> shadedPixels;
    REQUIRE(renderCustom(device, kShadedYellow + probe, /*shaderInPool=*/true, /*unlit=*/false,
                         shadedPixels));
    const int shadedBytes = sizes.lastBytes();
    sizes.reset();

    std::vector<std::uint8_t> unshadedPixels;
    REQUIRE(renderCustom(device, kUnshadedYellow + probe, /*shaderInPool=*/true,
                         /*unlit=*/false, unshadedPixels));
    const int unshadedBytes = sizes.lastBytes();

    hp::logSetGlobalLevel(hp::LogLevel::Info);
    hp::logRemoveSink(&sizes);

    const Rgb shaded = centreOf(shadedPixels);
    const Rgb unshaded = centreOf(unshadedPixels);
    MESSAGE("shaded module, no light: (" << shaded.r << ", " << shaded.g << ", " << shaded.b
                                         << "), " << shadedBytes << " bytes; unshaded: ("
                                         << unshaded.r << ", " << unshaded.g << ", "
                                         << unshaded.b << "), " << unshadedBytes << " bytes");

    // Semantics: the same scene, the same module but for one override. Black
    // versus the authored yellow, both exact.
    CHECK(shaded.r == 0);
    CHECK(shaded.g == 0);
    CHECK(shaded.b == 0);
    CHECK(unshaded.r == 255);
    CHECK(unshaded.g == 255);
    CHECK(unshaded.b == 0);

    // Cost: both sizes captured so the measurement cannot rot silently (the
    // modules differ in content, so neither compile can be a cache hit).
    // **Deliberately no folding assertion**: measured 19348 vs 19336 bytes —
    // slang does not eliminate the untaken lighting at the SPIR-V level, and
    // asserting the wish would just be a red test. If these numbers ever
    // diverge sharply, folding arrived (a slang upgrade, or T0151's
    // link-time constants) and the ticket note should be updated.
    REQUIRE(shadedBytes > 0);
    REQUIRE(unshadedBytes > 0);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("a material keeps state between hooks, and unwritten state is zero") {
    // **T0159.2, measured on the device rather than only on the compiler.**
    // Green can arrive in `surface()` only through a member written in
    // `surfaceCoordinates` — the hooks are the first and last of the sequence,
    // with every channel getter between them — and the blue channel reads a
    // member *nothing* writes, which the engine promises starts at zero
    // (`main` constructs the material `= {}`). Exact: (0, 255, 0). A regression
    // in the `[mutating]` declarations fails to compile; a regression in the
    // zero-initialisation turns blue non-zero.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    const char* kStatefulModule = R"(
struct HpMaterial : IHpMaterial
{
    float3 stash;
    float neverWritten;

    [mutating] override VSOutput surfaceCoordinates(VSOutput VSOut, HpSurfaceInput In)
    {
        stash = float3(0.0, 1.0, 0.0);
        return VSOut;
    }

    [mutating] override void surface(HpSurfaceInput In, inout HpSurfaceOutput Out)
    {
        Out.BaseColor = float4(stash.x, stash.y, neverWritten, 1.0);
    }

    override bool unshaded()
    {
        return true;
    }
}
)";
    std::vector<std::uint8_t> pixels;
    REQUIRE(renderCustom(device, kStatefulModule, /*shaderInPool=*/true, /*unlit=*/false, pixels));
    const Rgb centre = centreOf(pixels);
    MESSAGE("stateful module: (" << centre.r << ", " << centre.g << ", " << centre.b << ")");
    CHECK(centre.r == 0);
    CHECK(centre.g == 255);
    CHECK(centre.b == 0);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("the mesh's tangent reaches the contract, and a mesh without one reads zero") {
    // **T0159.4's both halves, exact.** The quad's authored tangent is world
    // +X, so `abs(In.Tangent.xyz)` is pure red when the plumbing works and
    // pure black where the documented zero fallback applies. Before T0159 the
    // first half rendered black too — the field was assigned zero
    // unconditionally, and the contract's own doc said otherwise.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    const char* kTangentModule = R"(
struct HpMaterial : IHpMaterial
{
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        return float4(abs(In.Tangent.xyz), 1.0);
    }
    override bool unshaded()
    {
        return true;
    }
}
)";
    std::vector<std::uint8_t> withTangents;
    REQUIRE(renderCustom(device, kTangentModule, /*shaderInPool=*/true, /*unlit=*/false,
                         withTangents, /*frames=*/1, /*timeSeconds=*/0.0,
                         /*withTangents=*/true));
    const Rgb tangent = centreOf(withTangents);
    MESSAGE("with tangents: (" << tangent.r << ", " << tangent.g << ", " << tangent.b << ")");
    CHECK(tangent.r == 255);
    CHECK(tangent.g == 0);
    CHECK(tangent.b == 0);

    std::vector<std::uint8_t> withoutTangents;
    REQUIRE(renderCustom(device, kTangentModule, /*shaderInPool=*/true, /*unlit=*/false,
                         withoutTangents));
    const Rgb zero = centreOf(withoutTangents);
    MESSAGE("without tangents: (" << zero.r << ", " << zero.g << ", " << zero.b << ")");
    CHECK(zero.r == 0);
    CHECK(zero.g == 0);
    CHECK(zero.b == 0);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("the frame's time reaches the contract, and an untimed render reads zero") {
    // **T0159.5 through the whole path**: the caller's seconds into
    // `SceneView::render`, through `SceneRenderer`'s frame attribs, into
    // `g_Frame.Renderer.Time`, out through `HpSurfaceInput::Time`. The module
    // brackets the value rather than comparing floats exactly; both outcomes
    // are exact colours.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    const char* kTimeModule = R"(
struct HpMaterial : IHpMaterial
{
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        if (In.Time > 0.4 && In.Time < 0.6)
        {
            return float4(0.0, 1.0, 0.0, 1.0); // the passed 0.5 arrived
        }
        return float4(1.0, 0.0, 0.0, 1.0);     // zero, or something stale
    }
    override bool unshaded()
    {
        return true;
    }
}
)";
    std::vector<std::uint8_t> timed;
    REQUIRE(renderCustom(device, kTimeModule, /*shaderInPool=*/true, /*unlit=*/false, timed,
                         /*frames=*/1, /*timeSeconds=*/0.5));
    const Rgb at = centreOf(timed);
    MESSAGE("time 0.5: (" << at.r << ", " << at.g << ", " << at.b << ")");
    CHECK(at.r == 0);
    CHECK(at.g == 255);
    CHECK(at.b == 0);

    // No time passed: the documented zero, not undefined memory — red's
    // exactness is the assertion that it is *defined*.
    std::vector<std::uint8_t> untimed;
    REQUIRE(renderCustom(device, kTimeModule, /*shaderInPool=*/true, /*unlit=*/false, untimed));
    const Rgb zero = centreOf(untimed);
    MESSAGE("no time: (" << zero.r << ", " << zero.g << ", " << zero.b << ")");
    CHECK(zero.r == 255);
    CHECK(zero.g == 0);
    CHECK(zero.b == 0);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("a module declares its own parameters and a .hpmat gives them values") {
    // **T0160's headline, measured end to end.** The same module — one file,
    // one pipeline — renders three different colours because three materials
    // give its declared `HpMaterialParams` block three different values. The
    // spike this replaces asserted the opposite: before T0160 a module
    // declaring that block failed PSO creation by name and rendered the
    // missing-material checkerboard.
    //
    // Unshaded, so the assertions are exact rather than "roughly this colour":
    // the value written into the block is the value at the target, and any
    // channel being off by one means the offset or the width is wrong.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    CompileErrorCounter counter;
    hp::logAddSink(&counter);

    // Two parameters, a `float3` and a `float`, so the case covers the
    // interesting half of the layout: slang packs a `float3` at 16-byte
    // alignment and the scalar after it, and reading the offsets from
    // reflection rather than computing them is what makes that free.
    const char* kTintModule = R"(
cbuffer HpMaterialParams
{
    [HpColor]
    [HpTooltip("The tint this material shows.")]
    float3 tint;

    [HpRange(0.0, 1.0)]
    float level;
}

struct HpMaterial : IHpMaterial
{
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        return float4(tint * level, 1.0);
    }
    override bool unshaded()
    {
        return true;
    }
}
)";

    const auto renderWith = [&](float r, float g, float b, float level, Rgb& out) -> bool {
        std::vector<std::uint8_t> pixels;
        const bool ok = renderCustom(
            device, kTintModule, /*shaderInPool=*/true, /*unlit=*/false, pixels, /*frames=*/1,
            /*timeSeconds=*/0.0, /*withTangents=*/false,
            [&](hp::Material& material, hp::Guid) {
                material.params = {
                    hp::MaterialParam{"tint", hp::ShaderValue{{r, g, b, 0.0F}, 3}},
                    hp::MaterialParam{"level", hp::ShaderValue{{level, 0.0F, 0.0F, 0.0F}, 1}},
                };
            });
        if (ok) {
            out = centreOf(pixels);
        }
        return ok;
    };

    Rgb green{};
    Rgb blue{};
    REQUIRE(renderWith(0.0F, 1.0F, 0.0F, 1.0F, green));
    REQUIRE(renderWith(0.0F, 0.0F, 1.0F, 1.0F, blue));
    MESSAGE("declared params: green (" << green.r << ", " << green.g << ", " << green.b
                                       << "), blue (" << blue.r << ", " << blue.g << ", "
                                       << blue.b << ")");
    CHECK(green.r == 0);
    CHECK(green.g == 255);
    CHECK(green.b == 0);
    CHECK(blue.r == 0);
    CHECK(blue.g == 0);
    CHECK(blue.b == 255);

    // The scalar after the `float3`, which is the offset assertion: if `level`
    // were written at the wrong offset it would land in `tint`'s bytes and the
    // colour above would already be wrong — so this checks the *value* arrives
    // as well as the address. Full-white at 1.0 above, mid-grey here.
    Rgb dimmed{};
    REQUIRE(renderWith(1.0F, 1.0F, 1.0F, 0.5F, dimmed));
    MESSAGE("level 0.5 on white: (" << dimmed.r << ", " << dimmed.g << ", " << dimmed.b << ")");
    // sRGB-encoded: linear 0.5 is 188, not 128. Bracketed rather than exact
    // because the encode is the target's, not this test's.
    CHECK(dimmed.r > 180);
    CHECK(dimmed.r < 195);
    CHECK(dimmed.r == dimmed.g);
    CHECK(dimmed.g == dimmed.b);

    // **A material that sets nothing reads zero, not the previous draw.** The
    // buffer is mapped with DISCARD, so an unwritten block would hold whatever
    // the driver handed back — and the three renders above have just filled it
    // with white. Black here is the proof that it is cleared.
    std::vector<std::uint8_t> unset;
    REQUIRE(renderCustom(device, kTintModule, /*shaderInPool=*/true, /*unlit=*/false, unset));
    const Rgb zero = centreOf(unset);
    MESSAGE("no params set: (" << zero.r << ", " << zero.g << ", " << zero.b << ")");
    CHECK(zero.r == 0);
    CHECK(zero.g == 0);
    CHECK(zero.b == 0);

    hp::logRemoveSink(&counter);
    // **Nothing failed to compile and nothing was substituted.** Without this,
    // a module that stopped compiling would render the checkerboard and the
    // colour checks would fail for a reason nobody could see from the numbers.
    CHECK(counter.compilerErrors() == 0);
    CHECK(counter.substitutions() == 0);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("a module samples its own texture slot, and an unbound slot is white") {
    // **The second half of T0160's declaration vocabulary.** The slot names are
    // the engine's (`HpTexture0`, with an immutable `HpTexture0_sampler`), and
    // a `.hpmat` binds an asset to one by that name.
    //
    // Sampled at a fixed coordinate rather than at `In.UV0`, because this quad
    // has no texture coordinates at all — which also makes the expected colour
    // one texel of the 16x16 checkerboard rather than an average.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    const char* kTextureModule = R"(
struct HpMaterial : IHpMaterial
{
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        return HpTexture0.SampleLevel(HpTexture0_sampler, float3(0.125, 0.125, 0.0), 0.0);
    }
    override bool unshaded()
    {
        return true;
    }
}
)";

    // Unbound: the renderer's white default, exactly as an unset fixed texture
    // slot behaves. White rather than **zero** is the load-bearing half — an
    // unbound texture array samples zero, which is a black surface and looks
    // like a lighting bug (the T0141.11 finding, in a new slot).
    std::vector<std::uint8_t> unbound;
    REQUIRE(renderCustom(device, kTextureModule, /*shaderInPool=*/true, /*unlit=*/false, unbound));
    const Rgb empty = centreOf(unbound);
    MESSAGE("unbound HpTexture0: (" << empty.r << ", " << empty.g << ", " << empty.b << ")");
    CHECK(empty.r == 255);
    CHECK(empty.g == 255);
    CHECK(empty.b == 255);

    // Bound: texel (2, 2) of the checkerboard, which is a magenta check.
    std::vector<std::uint8_t> bound;
    REQUIRE(renderCustom(device, kTextureModule, /*shaderInPool=*/true, /*unlit=*/false, bound,
                         /*frames=*/1, /*timeSeconds=*/0.0, /*withTangents=*/false,
                         [](hp::Material& material, hp::Guid texture) {
                             material.textures = {hp::MaterialTexture{"HpTexture0", texture}};
                         }));
    const Rgb sampled = centreOf(bound);
    MESSAGE("bound HpTexture0: (" << sampled.r << ", " << sampled.g << ", " << sampled.b << ")");
    CHECK(sampled.r == 255);
    CHECK(sampled.g == 0);
    CHECK(sampled.b == 255);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("a module declares textures under its own names, at its own count (T0161)") {
    // **The game resource model's headline, measured end to end.** Six
    // author-named `Texture2DArray` declarations — two more than T0160's slot
    // count ever allowed — plus the parameter block, in one module. The
    // `.hpmat` binds two of them by the author's names; one sampled texture is
    // deliberately left unbound; three declared ones are never sampled at all.
    //
    // Each output channel isolates one claim, so a failure names its cause:
    //   * red   = 1 - detailAlbedo.g — 255 only if the *bound* texture arrived
    //     (the checkerboard's magenta texel has g 0; the white fallback would
    //     give 0);
    //   * green = 1 - puddleMask.g — same claim for the second binding, so two
    //     names resolve independently;
    //   * blue  = the declared parameter — params and textures share the one
    //     module signature, asserted together on purpose.
    //
    // Three different palette samplers do the sampling, which is 161.2's
    // vocabulary working. The unsampled declarations cost nothing (slang
    // strips them); the sampled-but-unbound one reads white and feeds no
    // channel — its presence asserts that an unbound author-named texture is
    // the documented white, not a validation error.
    //
    // Expected (255, 255, ~138): yellow-ish, which neither the flat-magenta
    // fallback nor the all-white unbound state can produce — and the magenta
    // guard runs on the frame as well, because that mistake has been made
    // twice.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    CompileErrorCounter counter;
    hp::logAddSink(&counter);

    const char* kNamedModule = R"(
Texture2DArray detailAlbedo;
Texture2DArray puddleMask;
Texture2DArray ringMask;
Texture2DArray neverSampledA;
Texture2DArray neverSampledB;
Texture2DArray neverSampledC;

cbuffer HpMaterialParams
{
    [HpRange(0.0, 1.0)]
    float blend;
}

struct HpMaterial : IHpMaterial
{
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        float4 a = detailAlbedo.SampleLevel(HpSamplerLinearWrap, float3(0.125, 0.125, 0.0), 0.0);
        float4 b = puddleMask.SampleLevel(HpSamplerPointClamp, float3(0.125, 0.125, 0.0), 0.0);
        float4 c = ringMask.SampleLevel(HpSamplerLinearClamp, float3(0.125, 0.125, 0.0), 0.0);
        // c is unbound white: (1 - c.g) contributes zero, asserting the
        // documented fallback without disturbing the channels above.
        return float4(1.0 - a.g, 1.0 - b.g, blend + (1.0 - c.g), 1.0);
    }
    override bool unshaded()
    {
        return true;
    }
}
)";

    std::vector<std::uint8_t> pixels;
    REQUIRE(renderCustom(device, kNamedModule, /*shaderInPool=*/true, /*unlit=*/false, pixels,
                         /*frames=*/1, /*timeSeconds=*/0.0, /*withTangents=*/false,
                         [](hp::Material& material, hp::Guid texture) {
                             material.textures = {
                                 hp::MaterialTexture{"detailAlbedo", texture},
                                 hp::MaterialTexture{"puddleMask", texture},
                             };
                             material.params = {hp::MaterialParam{
                                 "blend", hp::ShaderValue{{0.25F, 0.0F, 0.0F, 0.0F}, 1}}};
                         }));
    const Rgb centre = centreOf(pixels);
    MESSAGE("author-named textures: (" << centre.r << ", " << centre.g << ", " << centre.b
                                       << ")");
    CHECK(centre.r == 255);
    CHECK(centre.g == 255);
    // The declared parameter, sRGB-encoded: linear 0.25 is ~138 at the target.
    CHECK(centre.b > 132);
    CHECK(centre.b < 145);

    // The magenta guard: the exact channels above would already fail on the
    // fallback, but the guard is the standing rule because a frame that is
    // secretly the checkerboard has fooled a summary statistic here twice.
    int magenta = 0;
    int black = 0;
    countChecks(pixels, magenta, black);
    CHECK(magenta == 0);

    hp::logRemoveSink(&counter);
    CHECK(counter.compilerErrors() == 0);
    CHECK(counter.substitutions() == 0);

    hp::Vfs::shutdown();
    tearDown(device);
}

namespace {

/// Counts pixels the unlit red quad covers — anything strongly red. The clear
/// colour is pure blue, so the two never overlap.
int redPixels(const std::vector<std::uint8_t>& rgba) {
    int red = 0;
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        if (rgba[i] > 200 && rgba[i + 1] < 60 && rgba[i + 2] < 60) {
            ++red;
        }
    }
    return red;
}

/// Pixels red in @p after that were **not** red in @p before, and vice versa —
/// the two halves of "the silhouette moved" (T0146.3).
void silhouetteDelta(const std::vector<std::uint8_t>& before,
                     const std::vector<std::uint8_t>& after, int& gained, int& lost) {
    gained = 0;
    lost = 0;
    const std::size_t n = std::min(before.size(), after.size());
    for (std::size_t i = 0; i + 3 < n; i += 4) {
        const bool wasRed = before[i] > 200 && before[i + 1] < 60 && before[i + 2] < 60;
        const bool isRed = after[i] > 200 && after[i + 1] < 60 && after[i + 2] < 60;
        if (isRed && !wasRed) {
            ++gained;
        } else if (wasRed && !isRed) {
            ++lost;
        }
    }
}

/// The unlit red material, with whatever vertex hook the case wants spliced in.
std::string redWith(const std::string& vertexHook) {
    return R"(
struct HpMaterial : IHpMaterial
{
)" + vertexHook + R"(
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        return float4(1.0, 0.0, 0.0, 1.0);
    }
    override bool unshaded()
    {
        return true;
    }
}
)";
}

} // namespace

TEST_CASE("a vertex hook moves the geometry, and doing nothing moves nothing (T0146)") {
    // **The headline, and the thing 141.7's parallax explicitly cannot do.**
    // Parallax moves where a texel is sampled from; the outline never shifts by
    // a pixel. This shifts the outline, and the assertion is exactly that —
    // pixels that were covered and are not, and pixels that were not and are.
    //
    // Three renders of one 8 m quad at z = 3, which more than fills the frame
    // at rest:
    //   * no `vertex()` override at all — the baseline silhouette, and the
    //     proof that a module which says nothing gets today's geometry;
    //   * scaled to a quarter in x and y — a silhouette strictly inside the
    //     first, so `lost` is large and `gained` is zero;
    //   * the same quarter-size quad translated in x — so `gained` is large
    //     too, which no amount of "the shader renders something" can fake.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    CompileErrorCounter counter;
    hp::logAddSink(&counter);

    std::vector<std::uint8_t> rest;
    REQUIRE(renderCustom(device, redWith(""), /*shaderInPool=*/true, /*unlit=*/false, rest));
    const int restCovered = redPixels(rest);

    // z is left alone deliberately: scaling it would move the quad toward the
    // camera and make it *larger* on screen, which would confuse the very
    // thing being measured.
    std::vector<std::uint8_t> shrunk;
    REQUIRE(renderCustom(device, redWith(R"(
    [mutating] override void vertex(HpVertexInput In, inout HpVertexOutput Out)
    {
        Out.Position.xy = In.Position.xy * 0.25;
    }
)"),
                         /*shaderInPool=*/true, /*unlit=*/false, shrunk));
    const int shrunkCovered = redPixels(shrunk);

    std::vector<std::uint8_t> shifted;
    REQUIRE(renderCustom(device, redWith(R"(
    [mutating] override void vertex(HpVertexInput In, inout HpVertexOutput Out)
    {
        Out.Position.xy = In.Position.xy * 0.25;
        Out.Position.x += 1.2;
    }
)"),
                         /*shaderInPool=*/true, /*unlit=*/false, shifted));

    int gainedByShrink = 0;
    int lostByShrink = 0;
    silhouetteDelta(rest, shrunk, gainedByShrink, lostByShrink);
    int gainedByShift = 0;
    int lostByShift = 0;
    silhouetteDelta(shrunk, shifted, gainedByShift, lostByShift);

    MESSAGE("silhouette: rest " << restCovered << " px, quarter " << shrunkCovered
                                << " px (lost " << lostByShrink << ", gained " << gainedByShrink
                                << "); shifted vs quarter: gained " << gainedByShift << ", lost "
                                << lostByShift);

    // The rest quad fills the frame, so the baseline is every pixel.
    CHECK(restCovered == kSize * kSize);
    // A quarter-size quad is strictly inside it: a large loss and no gain.
    CHECK(shrunkCovered > 0);
    CHECK(shrunkCovered < restCovered / 2);
    CHECK(lostByShrink > restCovered / 2);
    CHECK(gainedByShrink == 0);
    // The translation is the "a pixel far from the rest position" half: the
    // shifted quad covers pixels the resting one never touched.
    CHECK(gainedByShift > 100);
    CHECK(lostByShift > 100);

    // The magenta guard, on every asserted frame. A failed shader renders the
    // checkerboard, which is not red, so `redPixels` would read zero and the
    // *shape* of the failure would be indistinguishable from a hook that ran
    // and did nothing. This names it.
    for (const std::vector<std::uint8_t>* frame : {&rest, &shrunk, &shifted}) {
        int magenta = 0;
        int black = 0;
        countChecks(*frame, magenta, black);
        CHECK(magenta == 0);
    }

    hp::logRemoveSink(&counter);
    CHECK(counter.compilerErrors() == 0);
    CHECK(counter.substitutions() == 0);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("the vertex hook's inputs are what the contract promises (T0146.2, D36)") {
    // **D36's decision, measured rather than documented alone.** Each channel
    // isolates one promise, so a failure names its cause:
    //
    //   * red   — `In.WorldPos` really is `mul(float4(Position, 1),
    //     ObjectToWorld)`. The quad is *translated* for this case, so an
    //     identity matrix or a world-space `Position` both fail it.
    //   * green — `In.Position` is **object** space: the quad's own corners are
    //     at |x| = 4, and the entity sits 2 m away in x, so object and world
    //     disagree and only one of them passes.
    //   * blue  — `In.Time` arrived, and `In.CameraPos` is the camera's.
    //
    // Carried to the pixel stage through `Custom0`, which is also the first
    // half of T0146.4 working.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    CompileErrorCounter counter;
    hp::logAddSink(&counter);

    const std::string kProbe = R"(
struct HpMaterial : IHpMaterial
{
    [mutating] override void vertex(HpVertexInput In, inout HpVertexOutput Out)
    {
        float3 fromMatrix = mul(float4(In.Position, 1.0), In.ObjectToWorld).xyz;
        float worldOk = distance(fromMatrix, In.WorldPos) < 1e-3 ? 1.0 : 0.0;
        // The quad's own corners: |x| = 4 in object space, |x - 2| in world.
        float objectOk = abs(abs(In.Position.x) - 4.0) < 1e-3 ? 1.0 : 0.0;
        float frameOk = (In.Time > 0.4 && In.Time < 0.6 &&
                         length(In.CameraPos) < 1e-3) ? 1.0 : 0.0;
        Out.Custom0 = float4(worldOk, objectOk, frameOk, 1.0);
    }

    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        return float4(In.Custom0.rgb, 1.0);
    }
    override bool unshaded()
    {
        return true;
    }
}
)";

    std::vector<std::uint8_t> pixels;
    REQUIRE(renderCustom(device, kProbe, /*shaderInPool=*/true, /*unlit=*/false, pixels,
                         /*frames=*/1, /*timeSeconds=*/0.5, /*withTangents=*/false, {},
                         hp::float3{2.0F, 0.0F, 0.0F}));
    const Rgb centre = centreOf(pixels);
    MESSAGE("vertex contract: (" << centre.r << ", " << centre.g << ", " << centre.b << ")");
    CHECK(centre.r == 255);
    CHECK(centre.g == 255);
    CHECK(centre.b == 255);

    int magenta = 0;
    int black = 0;
    countChecks(pixels, magenta, black);
    CHECK(magenta == 0);

    hp::logRemoveSink(&counter);
    CHECK(counter.compilerErrors() == 0);
    CHECK(counter.substitutions() == 0);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("custom interpolators carry a value from vertex to pixel (T0146.4)") {
    // **The channel between the stages, and the three things about it worth
    // pinning.** `VSOutput` is generated per permutation by `PBR_Renderer`, so
    // a module cannot declare a varying; the engine appends four fixed `float4`
    // slots to the generated struct for custom-material permutations, and this
    // is what that buys:
    //
    //   * red   — a *varying* value: written per vertex from the object-space
    //     x, so the frame is a left-to-right ramp rather than a flat colour.
    //     Sampled at two columns and required to differ, which a constant
    //     cannot fake.
    //   * green — a constant, carried intact.
    //   * blue  — `Custom1`, never written by the hook, arriving as the
    //     documented **zero** rather than as whatever was in the register.
    //
    // The quad has no texture coordinates and no interpolated attribute the
    // pixel stage reads, so before T0146.4 there was no way to move a
    // per-vertex value across at all.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    CompileErrorCounter counter;
    hp::logAddSink(&counter);

    const std::string kVarying = R"(
struct HpMaterial : IHpMaterial
{
    [mutating] override void vertex(HpVertexInput In, inout HpVertexOutput Out)
    {
        // -4..4 in x maps to 0..1, so the frame is a ramp.
        Out.Custom0 = float4(saturate(In.Position.x * 0.125 + 0.5), 1.0, 0.0, 1.0);
        // Custom1 deliberately untouched.
    }

    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        return float4(In.Custom0.r, In.Custom0.g, In.Custom1.x, 1.0);
    }
    override bool unshaded()
    {
        return true;
    }
}
)";

    std::vector<std::uint8_t> pixels;
    REQUIRE(renderCustom(device, kVarying, /*shaderInPool=*/true, /*unlit=*/false, pixels));

    const auto at = [&](int x, int y) {
        const std::size_t i = (static_cast<std::size_t>(y) * kSize + x) * 4;
        return Rgb{pixels[i], pixels[i + 1], pixels[i + 2]};
    };
    const Rgb left = at(kSize / 8, kSize / 2);
    const Rgb right = at(kSize * 7 / 8, kSize / 2);
    MESSAGE("interpolators: left (" << left.r << ", " << left.g << ", " << left.b << "), right ("
                                    << right.r << ", " << right.g << ", " << right.b << ")");

    // Interpolated, not constant: the ramp really varies across the quad.
    CHECK(right.r > left.r + 40);
    // The constant channel survives both ends untouched.
    CHECK(left.g == 255);
    CHECK(right.g == 255);
    // The unwritten slot is zero at both ends, not noise.
    CHECK(left.b == 0);
    CHECK(right.b == 0);

    int magenta = 0;
    int black = 0;
    countChecks(pixels, magenta, black);
    CHECK(magenta == 0);

    hp::logRemoveSink(&counter);
    CHECK(counter.compilerErrors() == 0);
    CHECK(counter.substitutions() == 0);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("a module's texture and parameters reach its vertex hook (T0146, T0161)") {
    // **The game resource model, one stage up.** A module declares one
    // texture and one parameter and reads **both from the vertex stage only** —
    // so the per-module signature has to carry them for `SHADER_TYPE_VERTEX`,
    // which the T0161 walk could not express until this ticket made the
    // request's stage list plural.
    //
    // If the signature were still built from the pixel shader alone, the
    // vertex stage's use of `windMap` would fail PSO creation by name and the
    // frame would be the checkerboard — which the magenta guard below would
    // catch, and which is why this case exists rather than being assumed from
    // the pixel-stage one.
    //
    // Sampled with `SampleLevel`: the vertex stage has no implicit
    // derivatives, so an explicit level is not a nicety there.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    CompileErrorCounter counter;
    hp::logAddSink(&counter);

    const std::string kVertexResources = R"(
Texture2DArray windMap;

cbuffer HpMaterialParams
{
    [HpRange(0.0, 4.0)]
    float gain;
}

struct HpMaterial : IHpMaterial
{
    [mutating] override void vertex(HpVertexInput In, inout HpVertexOutput Out)
    {
        // Texel (2, 2) of the 16x16 checkerboard is magenta: (1, 0, 1).
        float4 wind = windMap.SampleLevel(HpSamplerPointClamp, float3(0.125, 0.125, 0.0), 0.0);
        Out.Custom0 = float4(wind.r * gain, wind.g, wind.b * gain, 1.0);
    }

    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        return float4(In.Custom0.rgb, 1.0);
    }
    override bool unshaded()
    {
        return true;
    }
}
)";

    std::vector<std::uint8_t> pixels;
    REQUIRE(renderCustom(device, kVertexResources, /*shaderInPool=*/true, /*unlit=*/false, pixels,
                         /*frames=*/1, /*timeSeconds=*/0.0, /*withTangents=*/false,
                         [](hp::Material& material, hp::Guid texture) {
                             material.textures = {hp::MaterialTexture{"windMap", texture}};
                             material.params = {hp::MaterialParam{
                                 "gain", hp::ShaderValue{{1.0F, 0.0F, 0.0F, 0.0F}, 1}}};
                         }));
    const Rgb centre = centreOf(pixels);
    MESSAGE("vertex-stage resources: (" << centre.r << ", " << centre.g << ", " << centre.b
                                        << ")");
    // The magenta texel times a gain of 1: (255, 0, 255) at the target. That
    // is also the checkerboard's colour, so the guard below cannot be the one
    // that distinguishes them — the *unbound* case does, and it is what makes
    // this assertion mean the texture arrived.
    CHECK(centre.r == 255);
    CHECK(centre.g == 0);
    CHECK(centre.b == 255);

    // Unbound and unvalued: the texture reads white and the parameter reads
    // zero, so the frame is green — a colour neither the checkerboard nor the
    // bound case can produce.
    std::vector<std::uint8_t> unbound;
    REQUIRE(renderCustom(device, kVertexResources, /*shaderInPool=*/true, /*unlit=*/false,
                         unbound));
    const Rgb empty = centreOf(unbound);
    MESSAGE("vertex-stage resources unbound: (" << empty.r << ", " << empty.g << ", " << empty.b
                                                << ")");
    CHECK(empty.r == 0);
    CHECK(empty.g == 255);
    CHECK(empty.b == 0);

    hp::logRemoveSink(&counter);
    CHECK(counter.compilerErrors() == 0);
    CHECK(counter.substitutions() == 0);

    hp::Vfs::shutdown();
    tearDown(device);
}

namespace {

/// Captures log lines containing a substring — for asserting a refusal
/// happened *by name*, which is the loud half of every T0161 policy check.
class MessageCatcher final : public hp::ILogSink {
public:
    explicit MessageCatcher(std::string needle) : needle_(std::move(needle)) {}
    void write(const hp::LogRecord& record) override {
        if (record.message.find(needle_) != std::string_view::npos) {
            ++hits_;
        }
    }
    [[nodiscard]] int hits() const { return hits_.load(); }

private:
    std::string needle_;
    std::atomic<int> hits_{0};
};

} // namespace

TEST_CASE("both stages come from one file, and the compiler says so (T0146.5)") {
    // **The single-module claim, instrumented.** The engine used to issue two
    // compile requests per pipeline — one for DiligentFX's `RenderPBR.vsh`,
    // one for `HpSurface.slang`. It now issues one with two entry points, and
    // the compiler's own per-entry-point debug line is the evidence: two lines
    // for the same file, one naming `vsMain` and one naming `psMain`, and
    // **no** line naming `RenderPBR.vsh`.
    //
    // The module carries a per-run GUID so neither entry point can be served
    // from the content-keyed cache, which would leave nothing to count.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    MessageCatcher vertex("compiled 'HpSurface.slang' (vsMain)");
    MessageCatcher pixel("compiled 'HpSurface.slang' (psMain)");
    MessageCatcher theirs("RenderPBR.vsh");
    hp::logAddSink(&vertex);
    hp::logAddSink(&pixel);
    hp::logAddSink(&theirs);
    hp::logSetGlobalLevel(hp::LogLevel::Debug);

    const std::string probe = "// probe " + hp::Guid::generate().toString() + "\n";
    std::vector<std::uint8_t> pixels;
    REQUIRE(renderCustom(device, redWith("") + probe, /*shaderInPool=*/true, /*unlit=*/false,
                         pixels));

    hp::logSetGlobalLevel(hp::LogLevel::Info);
    hp::logRemoveSink(&theirs);
    hp::logRemoveSink(&pixel);
    hp::logRemoveSink(&vertex);

    MESSAGE("one module, two entry points: vsMain " << vertex.hits() << ", psMain "
                                                    << pixel.hits() << ", RenderPBR.vsh "
                                                    << theirs.hits());
    CHECK(vertex.hits() == 1);
    CHECK(pixel.hits() == 1);
    CHECK(theirs.hits() == 0);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("a Texture2D declaration is refused by name, not left for Vulkan (T0161.3)") {
    // Diligent's `ShaderResourceDesc` cannot see resource dimension, so a
    // module declaring `Texture2D` where the engine binds one-slice
    // `Texture2DArray` views would sail into a cooked build and die as a raw
    // Vulkan validation error naming nothing. Dev-time slang reflection sees
    // the shape; the pipeline build must refuse it **naming the resource**,
    // and the material must fall back to the loud checkerboard.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    MessageCatcher catcher("wrongShape");
    hp::logAddSink(&catcher);

    const char* kFlatTextureModule = R"(
Texture2D wrongShape;

struct HpMaterial : IHpMaterial
{
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        return wrongShape.SampleLevel(HpSamplerLinearWrap, float2(0.5, 0.5), 0.0);
    }
    override bool unshaded()
    {
        return true;
    }
}
)";
    std::vector<std::uint8_t> pixels;
    REQUIRE(renderCustom(device, kFlatTextureModule, /*shaderInPool=*/true, /*unlit=*/false,
                         pixels, /*frames=*/2));
    hp::logRemoveSink(&catcher);

    // Refused by name, once — the pipeline null is cached, so frame two must
    // not repeat it.
    CHECK(catcher.hits() == 1);

    // The loud convention: the checkerboard fallback, flat magenta on this
    // UV-less quad.
    int magenta = 0;
    int black = 0;
    countChecks(pixels, magenta, black);
    MESSAGE("Texture2D refusal: magenta " << magenta << ", named-log lines " << catcher.hits());
    const int centrePixels = (kSize / 2) * (kSize / 2);
    CHECK(magenta > (centrePixels * 3) / 4);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("an author-declared SamplerState is refused by name, palette in the log (T0161.2)") {
    // D35's one deliberate limit, enforced where the author can see it:
    // sampler state is the engine's palette, and a module declaring its own
    // `SamplerState` fails the pipeline with a line naming the sampler and
    // listing the palette — never a mysteriously unbound descriptor.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    MessageCatcher named("myCustomSampler");
    MessageCatcher palette("HpSamplerLinearWrap");
    hp::logAddSink(&named);
    hp::logAddSink(&palette);

    const char* kOwnSamplerModule = R"(
Texture2DArray someMap;
SamplerState myCustomSampler;

struct HpMaterial : IHpMaterial
{
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        return someMap.SampleLevel(myCustomSampler, float3(0.5, 0.5, 0.0), 0.0);
    }
    override bool unshaded()
    {
        return true;
    }
}
)";
    std::vector<std::uint8_t> pixels;
    REQUIRE(renderCustom(device, kOwnSamplerModule, /*shaderInPool=*/true, /*unlit=*/false,
                         pixels));
    hp::logRemoveSink(&palette);
    hp::logRemoveSink(&named);

    CHECK(named.hits() == 1);
    CHECK(palette.hits() >= 1);

    int magenta = 0;
    int black = 0;
    countChecks(pixels, magenta, black);
    MESSAGE("own-sampler refusal: magenta " << magenta << ", named " << named.hits()
                                            << ", palette lines " << palette.hits());
    const int centrePixels = (kSize / 2) * (kSize / 2);
    CHECK(magenta > (centrePixels * 3) / 4);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("the base signature's descriptor budget dropped, and it is pinned (T0161.7)") {
    // T0160 counted 10 sampled images and 11 immutable samplers in the shared
    // signature — four of each belonging to module slots every plain glTF
    // material paid for without ever reading. The migration moved those into
    // the per-module signature, so the base holds the engine's own vocabulary
    // plus the sampler palette and nothing else:
    //
    //   6 sampled images   — 5 glTF maps + g_HeightMap (was 10)
    //   13 immutable samplers — their 7 + the 6-entry palette (was 11)
    //
    // Pinned from the renderer's own creation-time count, so the budget the
    // capability matrix quotes cannot drift from the code silently — the
    // T0160 audit had to reconstruct these numbers out of the source once,
    // which is exactly the job a log line and an assertion do better.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    MessageCatcher images("6 sampled images");
    MessageCatcher samplers("13 immutable samplers");
    MessageCatcher line("base signature:");
    hp::logAddSink(&images);
    hp::logAddSink(&samplers);
    hp::logAddSink(&line);
    hp::logSetGlobalLevel(hp::LogLevel::Debug);

    {
        hp::SceneView view;
        REQUIRE(view.create(device.render->device(), device.render->context(), kSize, kSize));
        view.release();
    }

    hp::logSetGlobalLevel(hp::LogLevel::Info);
    hp::logRemoveSink(&line);
    hp::logRemoveSink(&samplers);
    hp::logRemoveSink(&images);

    REQUIRE(line.hits() == 1);
    CHECK(images.hits() == 1);
    CHECK(samplers.hits() == 1);

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
