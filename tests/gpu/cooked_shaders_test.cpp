// Cooked shaders render a frame with nothing compiling (T0142.7, D34).
//
// **This is the acceptance test for T0142's "a shipped game links no Slang and
// reads cooked output only".** The link half was already true — the engine
// `dlopen`s the compiler and no consumer inherits an edge to it — and the
// *reads* half is what could only be claimed after cooking existed. So the
// assertions are about absence as much as about pixels:
//
//   * the same scene, rendered from a cooked archive with `cookedShadersOnly`
//     on, produces the **byte-identical** frame it produced when it compiled.
//     Not "similar": the same bytes, because it is the same SPIR-V;
//   * **zero** compiles happen while doing it, counted from the compiler's own
//     per-compile log line. A cooked frame that quietly recompiled would pass a
//     pixel check and prove nothing;
//   * a variant the cook missed is **loud and unrecoverable**, not a silent
//     fallback — the message says the shader was not cooked, and says it once.
//
// The last one is the whole reason this ticket carried a decision rather than
// just work: `Cook.hpp`'s invariant says a missing cook is re-cookable, and
// here it is not.
//
// Bucket: gpu. Skips cleanly with no device.

#include <doctest/doctest.h>

#include <hp/Assets.hpp>
#include <hp/Camera.hpp>
#include <hp/Guid.hpp>
#include <hp/Log.hpp>
#include <hp/Material.hpp>
#include <hp/Render.hpp>
#include <hp/Scene.hpp>
#include <hp/SceneView.hpp>
#include <hp/ShaderCook.hpp>
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
    windowConfig.title = "hp cooked shaders test";
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
        -4.0F, -4.0F,-3.0F, 0.0F, 0.0F, 1.0F,
         4.0F, -4.0F,-3.0F, 0.0F, 0.0F, 1.0F,
         4.0F,  4.0F,-3.0F, 0.0F, 0.0F, 1.0F,
        -4.0F,  4.0F,-3.0F, 0.0F, 0.0F, 1.0F,
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

/// Counts the compiler's own per-compile line, and the cook layer's
/// unrecoverable one. Both are the instrument, not the assertion's subject:
/// "the frame came out right" is worthless here without "and nothing compiled".
class CompileWatch final : public hp::ILogSink {
public:
    void write(const hp::LogRecord& record) override {
        if (record.message.find(" bytes of SPIR-V") != std::string_view::npos) {
            ++compiles_;
        }
        if (record.message.find("was not cooked for this variant") != std::string_view::npos) {
            ++notCooked_;
        }
    }
    [[nodiscard]] int compiles() const { return compiles_.load(); }
    [[nodiscard]] int notCooked() const { return notCooked_.load(); }
    void reset() {
        compiles_ = 0;
        notCooked_ = 0;
    }

private:
    std::atomic<int> compiles_{0};
    std::atomic<int> notCooked_{0};
};

/// The module under test — the same three-line shape T0142.15 pins, plus a
/// per-run token so the cook cannot be satisfied by an entry another test or
/// another run left in the developer cache.
std::string redModule(const std::string& token) {
    return R"(
struct HpMaterial : IHpMaterial
{
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        return float4(1.0, 0.0, 0.0, 1.0);
    }
}
// )" + token + "\n";
}

/// Renders the quad with @p moduleSource as its material module.
///
/// Deliberately does **not** wipe the scratch directory: the cooked archive
/// lives there between calls, and a helper that cleared it would be testing
/// nothing.
bool render(Device& device, const std::filesystem::path& scratch,
            const std::string& moduleSource, std::vector<std::uint8_t>& pixels) {
    {
        std::ofstream file(scratch / "materials" / "custom.slang");
        file << moduleSource;
    }
    hp::Vfs::shutdown();
    if (!hp::Vfs::init(nullptr) || !hp::Vfs::mount(scratch.string())) {
        return false;
    }
    hp::loadCookedShaders();

    hp::AssetPool pool;
    const hp::Guid meshGuid = hp::Guid::generate();
    auto mesh =
        hp::loadMesh(device.render->device(), device.render->context(), "models/quad.gltf");
    if (!mesh || !mesh->valid()) {
        return false;
    }
    pool.store<hp::MeshAsset>(meshGuid, mesh);

    const hp::Guid shaderGuid = hp::Guid::generate();
    auto shader = hp::loadShader("materials/custom.slang");
    if (!shader || !shader->valid()) {
        return false;
    }
    pool.store<hp::ShaderAsset>(shaderGuid, shader);

    const hp::Guid materialGuid = hp::Guid::generate();
    {
        auto material = std::make_shared<hp::Material>();
        material->shader = shaderGuid;
        // Unlit, so the assertion can be exact: the override's red reaches the
        // sRGB target untouched, and any other value means a different shader
        // ran.
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

/// Restores the process-wide switches whatever happens, **including a failed
/// `REQUIRE`**, which unwinds straight past the end of the test.
///
/// This is not tidiness. Leaving `cookedShadersOnly` on leaks into every later
/// case in the bucket, where nothing can compile and the first failure is a
/// segfault in an unrelated file — measured once, while writing this test, and
/// exactly the kind of cascade that makes a real failure unreadable.
struct CookPolicyGuard {
    ~CookPolicyGuard() {
        hp::setCookedShadersOnly(false);
        hp::logSetGlobalLevel(hp::LogLevel::Info);
    }
};

/// Releases the device even when a `REQUIRE` unwinds past `tearDown`. A
/// half-torn-down `RenderLayer` is what the next test case's `bringUp` then
/// crashes in.
struct DeviceGuard {
    Device& device;
    ~DeviceGuard() { tearDown(device); }
};

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

int magentaPixels(const std::vector<std::uint8_t>& rgba) {
    int magenta = 0;
    for (int y = kSize / 4; y < kSize * 3 / 4; ++y) {
        for (int x = kSize / 4; x < kSize * 3 / 4; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * kSize + x) * 4;
            if (rgba[i] > 200 && rgba[i + 2] > 200 && rgba[i + 1] < 60) {
                ++magenta;
            }
        }
    }
    return magenta;
}

} // namespace

TEST_CASE("a cooked archive renders the same frame with nothing compiling") {
    Device device = bringUp();
    DeviceGuard deviceGuard{device};
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        return;
    }

    std::error_code ec;
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "hp-cooked-shaders";
    std::filesystem::remove_all(scratch, ec);
    std::filesystem::create_directories(scratch / "models", ec);
    std::filesystem::create_directories(scratch / "materials", ec);
    writeQuadGltf(scratch / "models");
    const std::string token = hp::Guid::generate().toString();

    CookPolicyGuard policyGuard;
    CompileWatch watch;
    hp::logAddSink(&watch);
    hp::logSetGlobalLevel(hp::LogLevel::Debug);

    // --- 1. the cook run: compile, then seal ---------------------------------
    hp::setCookedShadersOnly(false);
    std::vector<std::uint8_t> compiled;
    REQUIRE(render(device, scratch, redModule(token), compiled));
    const int compilesWhileCooking = watch.compiles();
    MESSAGE("cook run: " << compilesWhileCooking << " compile(s)");
    // The module carries a per-run GUID, so its pixel shader cannot have been
    // served from a cache an earlier run filled. If this is ever zero the rest
    // of the test is measuring nothing.
    REQUIRE(compilesWhileCooking > 0);

    const std::filesystem::path archive =
        scratch / std::string(hp::kCookedShaderDirectory) /
        (std::string("test") + std::string(hp::kCookedShaderExtension));
    REQUIRE(hp::cookShaders(archive.string()));
    REQUIRE(std::filesystem::exists(archive));
    MESSAGE("cooked archive: " << std::filesystem::file_size(archive) << " bytes");

    // --- 2. the shipped run: cooked output is the only source ----------------
    // Everything the compiler could have been reached through is still present
    // on this machine, which is exactly why the switch exists: the shipped
    // behaviour is otherwise only ever exercised on a machine nobody is
    // debugging on.
    watch.reset();
    hp::setCookedShadersOnly(true);
    std::vector<std::uint8_t> fromCook;
    REQUIRE(render(device, scratch, redModule(token), fromCook));
    const int compilesWhileShipped = watch.compiles();

    const Rgb centre = centreOf(fromCook);
    MESSAGE("cooked run: (" << centre.r << ", " << centre.g << ", " << centre.b << "), "
                            << compilesWhileShipped << " compile(s), " << watch.notCooked()
                            << " missing");

    // Nothing compiled, and the frame is the same frame.
    CHECK(compilesWhileShipped == 0);
    CHECK(watch.notCooked() == 0);
    CHECK(centre.r == 255);
    CHECK(centre.g == 0);
    CHECK(centre.b == 0);
    CHECK(fromCook == compiled);

    hp::logRemoveSink(&watch);
    hp::Vfs::shutdown();
    std::filesystem::remove_all(scratch, ec);
}

TEST_CASE("a shader the cook missed is loud and unrecoverable, not a silent fallback") {
    // **The decision, made visible.** `Cook.hpp` would call this a cache miss
    // and re-cook; there is nothing to re-cook with. The module's text is
    // edited *after* the archive is written, so its content hash no longer
    // matches anything in it — which is exactly the shape of a game shipping a
    // shader change without re-running the cook.
    Device device = bringUp();
    DeviceGuard deviceGuard{device};
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        return;
    }

    std::error_code ec;
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "hp-cooked-shaders-miss";
    std::filesystem::remove_all(scratch, ec);
    std::filesystem::create_directories(scratch / "models", ec);
    std::filesystem::create_directories(scratch / "materials", ec);
    writeQuadGltf(scratch / "models");
    const std::string token = hp::Guid::generate().toString();
    const std::string good = redModule(token);

    CookPolicyGuard policyGuard;
    CompileWatch watch;
    hp::logAddSink(&watch);
    hp::logSetGlobalLevel(hp::LogLevel::Debug);
    hp::setCookedShadersOnly(false);

    // --- the cook run has to cook *everything the content needs* -------------
    // **Including the fallback**, and getting that wrong is what this comment
    // is for. The first version of this test cooked only the good module and
    // then asserted the fallback appeared when the module went missing — which
    // passed for a while purely because an earlier test in the bucket had left
    // the fallback's variant in the shared developer cache, and failed the
    // moment a source change invalidated it. A cook that has never rendered the
    // fallback cannot ship it, and then the missed variant produces *no draw at
    // all* rather than magenta. So the broken module is rendered first, on
    // purpose: it is what makes the fallback a cooked variant.
    std::vector<std::uint8_t> viaFallback;
    REQUIRE(render(device, scratch, "struct HpMaterial : IHpMaterial { this does not parse }\n",
                   viaFallback));
    REQUIRE(magentaPixels(viaFallback) > 0);

    std::vector<std::uint8_t> compiled;
    REQUIRE(render(device, scratch, good, compiled));

    const std::filesystem::path archive =
        scratch / std::string(hp::kCookedShaderDirectory) /
        (std::string("test") + std::string(hp::kCookedShaderExtension));
    REQUIRE(hp::cookShaders(archive.string()));

    // --- the shipped run, against a module that changed after the cook -------
    // One comment is enough: the key is a content hash over the resolved
    // source, which is the property that makes the archive honest and the
    // property that makes this a miss.
    watch.reset();
    hp::setCookedShadersOnly(true);
    std::vector<std::uint8_t> pixels;
    REQUIRE(render(device, scratch, good + "// edited after the cook\n", pixels));

    MESSAGE("uncooked variant: " << watch.compiles() << " compile(s), " << watch.notCooked()
                                 << " unrecoverable line(s), magenta "
                                 << magentaPixels(pixels));

    // Nothing was compiled to paper over it, and the log said the
    // unrecoverable thing exactly once for the shader.
    CHECK(watch.compiles() == 0);
    CHECK(watch.notCooked() == 1);

    // The frame is T0141.12's fallback rather than a wrong image: a pipeline
    // that cannot be built is the same emergency whatever caused it, and the
    // console is where the cause is said.
    const int centrePixels = (kSize / 2) * (kSize / 2);
    CHECK(magentaPixels(pixels) > (centrePixels * 3) / 4);

    hp::logRemoveSink(&watch);
    hp::Vfs::shutdown();
    std::filesystem::remove_all(scratch, ec);
}
