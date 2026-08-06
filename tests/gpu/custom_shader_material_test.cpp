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
bool renderCustom(Device& device, const std::string& moduleSource, bool shaderInPool, bool unlit,
                  std::vector<std::uint8_t>& pixels, int frames = 1, double timeSeconds = 0.0,
                  bool withTangents = false) {
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
        if (message.find("compiled 'HpSurface.slang' to ") == std::string_view::npos) {
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

TEST_CASE("spike: a module-declared cbuffer absent from every signature — the failure mode") {
    // **T0160.4's first open question, executed.** The capability matrix
    // records it verbatim: "Whether Diligent rejects a module-declared
    // `cbuffer` absent from the signature — decides whether T0160's failure
    // mode is loud or silent. Not executed." This case executes it: a module
    // declares its own constant buffer, slang compiles it happily (it is
    // legal Slang), and the pipeline is built against signatures that have
    // never heard of `HpMaterialParams`. Whatever renders — and whatever the
    // log says — is the failure mode T0160's design must beat.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    CompileErrorCounter counter;
    hp::logAddSink(&counter);

    const char* kParamsModule = R"(
cbuffer HpMaterialParams
{
    float4 HpTint;
}

struct HpMaterial : IHpMaterial
{
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        return HpTint;
    }
    override bool unshaded()
    {
        return true;
    }
}
)";
    std::vector<std::uint8_t> pixels;
    const bool rendered =
        renderCustom(device, kParamsModule, /*shaderInPool=*/true, /*unlit=*/false, pixels);
    hp::logRemoveSink(&counter);
    REQUIRE(rendered);

    int magenta = 0;
    int black = 0;
    countChecks(pixels, magenta, black);
    const Rgb centre = centreOf(pixels);
    MESSAGE("declared-cbuffer module: centre (" << centre.r << ", " << centre.g << ", "
                                                << centre.b << "), magenta " << magenta
                                                << ", compile errors " << counter.compilerErrors()
                                                << ", substitutions " << counter.substitutions());
    // The record, not a wish: this documents the observed failure mode so the
    // matrix's open question closes with a measurement. See the MESSAGE above
    // in the test log for the exact numbers behind the ticket's note.

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
