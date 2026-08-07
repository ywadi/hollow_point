// The engine's screen intermediates, proved with pixels (T0147.5).
//
// **The screen-space family was the largest blocked block in the capability
// matrix** — refraction, glass, soft particles, heat haze, frosted glass,
// fog-of-war, all of them waiting on one thing: a material shader cannot sample
// a depth or colour buffer that is not bound to the pipeline. These are the
// worked examples that say it can now, and each one asserts something a
// "renders something plausible" test could not.
//
// The claims, in order:
//
//   * **refraction** — what the pane shows at `x` is what the *scene* showed at
//     `x + offset`, compared against a render of the scene alone. Not "the pane
//     is reddish": the same pixel, within the sRGB round trip.
//   * **the snapshot is the opaque image** — the pane entity is created
//     **before** the wall on purpose, so the draw list hands the blended
//     surface over first. Before T0147 that pane wrote depth, the wall behind
//     it failed the reverse-Z test, and the wall was invisible. Getting the
//     wall's colour out of the refraction is therefore also the assertion that
//     the two-pass split happened.
//   * **depth fade** — one frame, two places: over a wall a known distance
//     behind, and over cleared background at the far plane. The two must differ
//     by the amount the arithmetic predicts.
//   * **an opaque read is refused** — loudly, by name, with the checkerboard,
//     rather than reading last frame.
//   * **a game-fed texture** — a texture no `.hpmat` mentions, bound by name at
//     run time, exactly as T0093's fog-of-war will need.
//   * **nothing is copied when nothing reads it** — the absence of the
//     snapshot's own log line in a scene of blended geometry that ignores the
//     screen.
//
// The magenta guard runs on every asserted frame. That is the standing rule
// here: a failed shader renders the checkerboard and passes coverage and
// variation assertions, which has been measured twice.
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

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
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
    windowConfig.title = "hp screen inputs test";
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

/// A camera-facing quad of the given half-extent, at the origin of its own
/// space, positions and normals only.
///
/// **Wound `{0, 2, 1, 0, 3, 2}` for a −Z-facing normal**, per D33 and the
/// winding convention header. Copying the older `{0, 1, 2, 0, 2, 3}` order is
/// exactly how the T0152 asset bug spread, and a quad whose indices and normals
/// disagree renders as an invisible surface rather than as a shading error.
void writeQuadGltf(const std::filesystem::path& directory, const char* stem, float half) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    const float vertices[] = {
        -half, -half, 0.0F, 0.0F, 0.0F, -1.0F, //
        half,  -half, 0.0F, 0.0F, 0.0F, -1.0F, //
        half,  half,  0.0F, 0.0F, 0.0F, -1.0F, //
        -half, half,  0.0F, 0.0F, 0.0F, -1.0F,
    };
    std::vector<unsigned char> bin;
    const auto* vb = reinterpret_cast<const unsigned char*>(vertices);
    bin.insert(bin.end(), vb, vb + sizeof vertices);
    const std::size_t vertexBytes = bin.size();
    const std::uint16_t indices[] = {0, 2, 1, 0, 3, 2};
    const auto* ib = reinterpret_cast<const unsigned char*>(indices);
    bin.insert(bin.end(), ib, ib + sizeof indices);
    while (bin.size() % 4 != 0) {
        bin.push_back(0);
    }
    {
        std::ofstream file(directory / (std::string(stem) + ".bin"), std::ios::binary);
        file.write(reinterpret_cast<const char*>(bin.data()),
                   static_cast<std::streamsize>(bin.size()));
    }

    const std::string extent = std::to_string(half);
    const std::string name{stem};
    const std::string json = std::string(R"({
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
  "buffers": [ { "uri": ")") + name + R"(.bin", "byteLength": )" +
                             std::to_string(bin.size()) + R"( } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": )" +
                             std::to_string(vertexBytes) + R"(, "byteStride": 24 },
    { "buffer": 0, "byteOffset": )" + std::to_string(vertexBytes) + R"(, "byteLength": 12 }
  ],
  "accessors": [
    { "bufferView": 0, "byteOffset": 0,  "componentType": 5126, "count": 4, "type": "VEC3",
      "min": [-)" + extent + ", -" + extent + R"(, 0.0], "max": [)" + extent + ", " + extent +
                             R"(, 0.0] },
    { "bufferView": 0, "byteOffset": 12, "componentType": 5126, "count": 4, "type": "VEC3" },
    { "bufferView": 1, "byteOffset": 0,  "componentType": 5123, "count": 6, "type": "SCALAR" }
  ]
})";
    std::ofstream file(directory / (std::string(stem) + ".gltf"), std::ios::binary);
    file << json;
}

struct Rgb {
    int r = 0;
    int g = 0;
    int b = 0;
};

Rgb at(const std::vector<std::uint8_t>& rgba, int x, int y) {
    const std::size_t i = (static_cast<std::size_t>(y) * kSize + x) * 4;
    if (i + 2 >= rgba.size()) {
        return Rgb{};
    }
    return Rgb{rgba[i], rgba[i + 1], rgba[i + 2]};
}

Rgb centreOf(const std::vector<std::uint8_t>& rgba) {
    return at(rgba, kSize / 2, kSize / 2);
}

/// Loud magenta pixels anywhere in the frame — the missing-material
/// checkerboard's signature.
int magentaPixels(const std::vector<std::uint8_t>& rgba) {
    int magenta = 0;
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        if (rgba[i] > 200 && rgba[i + 2] > 200 && rgba[i + 1] < 60) {
            ++magenta;
        }
    }
    return magenta;
}

/// Counts log lines containing a phrase.
class MessageCatcher final : public hp::ILogSink {
public:
    explicit MessageCatcher(std::string phrase) : phrase_(std::move(phrase)) {}
    void write(const hp::LogRecord& record) override {
        if (record.message.find(phrase_) != std::string::npos) {
            ++hits_;
        }
    }
    [[nodiscard]] int hits() const { return hits_.load(); }
    void reset() { hits_ = 0; }

private:
    std::string phrase_;
    std::atomic<int> hits_{0};
};

/// One surface in the scene under test.
struct Surface {
    /// The quad's half-extent, in metres.
    float half = 1.0F;

    /// Where it sits on the view axis.
    float z = 5.0F;

    /// Its `.slang` module, or empty for the standard material.
    std::string module;

    /// Its alpha mode. `Blend` is what the screen intermediates require.
    hp::AlphaMode alphaMode = hp::AlphaMode::Opaque;

    /// Whether a game-fed texture named `visibility` should be declared as
    /// bound by the material. Left alone, nothing is bound and the module's
    /// own declaration falls through to the game feed.
    bool feedVisibility = false;
};

/// Renders a list of surfaces through one `SceneView` and reads it back.
///
/// **Surfaces are created as entities in list order**, and that matters for
/// more than convenience: `parseScene` walks the registry in creation order, so
/// putting a blended pane first is how a case proves the renderer's own
/// opaque-then-blend split rather than relying on the draw list to be
/// conveniently sorted.
///
/// @param device the brought-up device.
/// @param surfaces what to draw, in creation order.
/// @param pixels receives the readback.
/// @param screenInputs whether the view declares its snapshot targets.
/// @param feedTexture a texture to feed as `visibility`, or null.
/// @returns whether the frame rendered and every surface was submitted.
bool renderScene(Device& device, const std::vector<Surface>& surfaces,
                 std::vector<std::uint8_t>& pixels, bool screenInputs = true,
                 bool feedVisibilityTexture = false, int frames = 1) {
    std::error_code ec;
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "hp-screen-inputs";
    std::filesystem::remove_all(scratch, ec);
    std::filesystem::create_directories(scratch / "models", ec);
    std::filesystem::create_directories(scratch / "materials", ec);

    for (std::size_t i = 0; i < surfaces.size(); ++i) {
        writeQuadGltf(scratch / "models", ("quad" + std::to_string(i)).c_str(), surfaces[i].half);
        if (!surfaces[i].module.empty()) {
            std::ofstream file(scratch / "materials" / ("module" + std::to_string(i) + ".slang"));
            file << surfaces[i].module;
        }
    }

    hp::Vfs::shutdown();
    if (!hp::Vfs::init(nullptr) || !hp::Vfs::mount(scratch.string())) {
        return false;
    }

    hp::AssetPool pool;
    hp::Scene scene;
    hp::Entity cameraEntity = scene.create("camera");
    cameraEntity.add<hp::Camera>(hp::Camera{});

    // A texture for the game feed to hand over: the 16x16 checkerboard, whose
    // texel (2, 2) is a known magenta and whose white checks are a known white.
    const hp::Guid textureGuid = hp::Guid::generate();
    std::shared_ptr<hp::TextureAsset> placeholder =
        hp::makePlaceholderTexture(device.render->device());
    if (placeholder) {
        pool.store<hp::TextureAsset>(textureGuid, placeholder);
    }

    for (std::size_t i = 0; i < surfaces.size(); ++i) {
        const Surface& surface = surfaces[i];
        const hp::Guid meshGuid = hp::Guid::generate();
        auto mesh = hp::loadMesh(device.render->device(), device.render->context(),
                                 "models/quad" + std::to_string(i) + ".gltf");
        if (!mesh || !mesh->valid()) {
            return false;
        }
        pool.store<hp::MeshAsset>(meshGuid, mesh);

        auto material = std::make_shared<hp::Material>();
        material->doubleSided = true;
        material->alphaMode = surface.alphaMode;
        if (!surface.module.empty()) {
            const hp::Guid shaderGuid = hp::Guid::generate();
            auto shader = hp::loadShader("materials/module" + std::to_string(i) + ".slang");
            if (!shader || !shader->valid()) {
                return false;
            }
            pool.store<hp::ShaderAsset>(shaderGuid, shader);
            material->shader = shaderGuid;
        }
        if (surface.feedVisibility) {
            material->textures = {hp::MaterialTexture{"visibility", textureGuid}};
        }
        const hp::Guid materialGuid = hp::Guid::generate();
        pool.store<hp::Material>(materialGuid, material);

        hp::Entity entity = scene.create(("surface" + std::to_string(i)).c_str());
        hp::MeshRenderer renderer;
        renderer.mesh = meshGuid;
        renderer.materials = {materialGuid};
        entity.add<hp::MeshRenderer>(renderer);
        hp::Transform placed;
        placed.position = hp::float3{0.0F, 0.0F, surface.z};
        scene.setLocalTransform(entity, placed);
    }
    scene.propagateTransforms();

    hp::SceneView view;
    if (!view.create(device.render->device(), device.render->context(), kSize, kSize,
                     hp::TargetFormat::Colour, screenInputs)) {
        return false;
    }
    // Pure blue, so the cleared background is never mistaken for anything a
    // module could have produced.
    view.setClearColour(0.0F, 0.0F, 1.0F, 1.0F);
    if (feedVisibilityTexture && placeholder && placeholder->valid()) {
        view.setGameTexture("visibility", placeholder->shaderResource());
    }

    hp::SceneViewStats stats;
    for (int frame = 0; frame < frames; ++frame) {
        if (view.render(device.render->context(), scene, pool, 0, &stats, 0.0) == nullptr) {
            return false;
        }
        if (stats.submitted != surfaces.size()) {
            return false;
        }
    }
    return view.readback(device.render->context(), pixels);
}

/// An opaque wall whose colour is a horizontal ramp in screen space.
///
/// The ramp is what makes the refraction case an *identity* assertion rather
/// than a colour one: a flat wall would look the same however the sampling
/// coordinate was displaced, so a broken offset would pass.
const char* kRampWall = R"(
struct HpMaterial : IHpMaterial
{
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        return float4(In.ScreenUV.x, 1.0 - In.ScreenUV.x, 0.0, 1.0);
    }
    override bool unshaded()
    {
        return true;
    }
}
)";

/// A refractive pane: the scene behind it, displaced along x.
std::string refractivePane(const char* offset) {
    return std::string(R"(
struct HpMaterial : IHpMaterial
{
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        float2 uv = In.ScreenUV + float2()") +
           offset + R"(, 0.0);
        return float4(HpSceneColour(uv).rgb, 1.0);
    }
    override bool unshaded()
    {
        return true;
    }
}
)";
}

} // namespace

TEST_CASE("a blended material refracts the scene behind it (T0147.2)") {
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    MessageCatcher compileErrors("slang failed to compile");
    MessageCatcher substitutions("did not compile; rendering the missing-material");
    hp::logAddSink(&compileErrors);
    hp::logAddSink(&substitutions);

    // The wall alone, which is the reference image every assertion below is
    // measured against.
    std::vector<std::uint8_t> wallOnly;
    REQUIRE(renderScene(device, {Surface{6.0F, 6.0F, kRampWall, hp::AlphaMode::Opaque}},
                        wallOnly));

    // **The pane is created first, deliberately.** Before T0147 the draw list
    // handed it over first, it wrote depth at z = 3, and the wall behind it at
    // z = 6 then failed the reverse-Z test outright — so this arrangement did
    // not merely refract wrongly, it hid the wall. Any colour of the wall
    // arriving through the pane is therefore also the split working.
    std::vector<std::uint8_t> straight;
    REQUIRE(renderScene(device,
                        {
                            Surface{6.0F, 3.0F, refractivePane("0.0"), hp::AlphaMode::Blend},
                            Surface{6.0F, 6.0F, kRampWall, hp::AlphaMode::Opaque},
                        },
                        straight));

    std::vector<std::uint8_t> displaced;
    REQUIRE(renderScene(device,
                        {
                            Surface{6.0F, 3.0F, refractivePane("0.25"), hp::AlphaMode::Blend},
                            Surface{6.0F, 6.0F, kRampWall, hp::AlphaMode::Opaque},
                        },
                        displaced));

    const Rgb reference = centreOf(wallOnly);
    const Rgb refracted = centreOf(straight);
    const Rgb shifted = centreOf(displaced);
    // Where a +0.25 screen displacement lands: three quarters across.
    const Rgb referenceShifted = at(wallOnly, (kSize * 3) / 4, kSize / 2);

    MESSAGE("wall alone at centre: (" << reference.r << ", " << reference.g << ", " << reference.b
                                      << "); through the pane: (" << refracted.r << ", "
                                      << refracted.g << ", " << refracted.b << ")");
    MESSAGE("wall alone at 3/4: (" << referenceShifted.r << ", " << referenceShifted.g << ", "
                                   << referenceShifted.b << "); pane displaced +0.25: ("
                                   << shifted.r << ", " << shifted.g << ", " << shifted.b << ")");

    // The wall is a ramp, so its own centre is neither black nor white — which
    // is what makes the comparisons below mean something.
    CHECK(reference.r > 150);
    CHECK(reference.r < 220);

    // **The identity claim**: an undisplaced refraction is the scene, pixel for
    // pixel, within the sRGB decode-and-re-encode round trip.
    CHECK(refracted.r >= reference.r - 2);
    CHECK(refracted.r <= reference.r + 2);
    CHECK(refracted.g >= reference.g - 2);
    CHECK(refracted.g <= reference.g + 2);

    // **The displacement claim**: with the coordinate moved a quarter of the
    // screen, the pane shows what the scene showed a quarter of the screen
    // across. A shader that ignored the offset would fail this and pass the one
    // above, which is why both are here.
    CHECK(shifted.r >= referenceShifted.r - 3);
    CHECK(shifted.r <= referenceShifted.r + 3);
    CHECK(shifted.r > refracted.r + 20);

    CHECK(magentaPixels(straight) == 0);
    CHECK(magentaPixels(displaced) == 0);

    hp::logRemoveSink(&substitutions);
    hp::logRemoveSink(&compileErrors);
    CHECK(compileErrors.hits() == 0);
    CHECK(substitutions.hits() == 0);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("a blended material fades against sampled scene depth (T0147.1)") {
    // **The soft-particle read, proved before T0106 needs it.** One frame, two
    // places, and the arithmetic is checkable by hand: the pane sits at z = 5
    // and a 2 m wall sits at z = 6, so over the wall the gap is 1 m and the
    // fade over a 2 m range is 0.5 — sRGB 188 at the target. Everywhere else
    // the depth buffer holds the clear value, which under reverse-Z is the far
    // plane, so the gap is ~995 m and the fade saturates at white.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    MessageCatcher compileErrors("slang failed to compile");
    MessageCatcher substitutions("did not compile; rendering the missing-material");
    hp::logAddSink(&compileErrors);
    hp::logAddSink(&substitutions);

    const char* kDepthFade = R"(
struct HpMaterial : IHpMaterial
{
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        float behind = HpSceneViewDepth(In.ScreenUV);
        float here   = HpViewDepth(In.ScreenPos.z);
        float fade   = saturate((behind - here) / 2.0);
        return float4(fade, fade, fade, 1.0);
    }
    override bool unshaded()
    {
        return true;
    }
}
)";

    const char* kRedWall = R"(
struct HpMaterial : IHpMaterial
{
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

    std::vector<std::uint8_t> pixels;
    REQUIRE(renderScene(device,
                        {
                            // 2 m wall at z = 6: with a 60-degree vertical
                            // field of view it covers the middle 29% of the
                            // frame, so the centre pixel is on it and a pixel
                            // near the edge is not.
                            Surface{1.0F, 6.0F, kRedWall, hp::AlphaMode::Opaque},
                            Surface{6.0F, 5.0F, kDepthFade, hp::AlphaMode::Blend},
                        },
                        pixels));

    const Rgb overWall = centreOf(pixels);
    const Rgb overBackground = at(pixels, kSize / 16, kSize / 2);
    MESSAGE("depth fade over the wall: (" << overWall.r << ", " << overWall.g << ", "
                                          << overWall.b << "); over background: ("
                                          << overBackground.r << ", " << overBackground.g << ", "
                                          << overBackground.b << ")");

    // 1 m of gap over a 2 m range is linear 0.5, which the sRGB target encodes
    // as 188. Bracketed rather than exact because the encode is the target's.
    CHECK(overWall.r > 178);
    CHECK(overWall.r < 198);
    CHECK(overWall.r == overWall.g);
    CHECK(overWall.g == overWall.b);

    // The far plane: saturated white, and nothing else in this scene is white.
    CHECK(overBackground.r > 250);
    CHECK(overBackground.g > 250);
    CHECK(overBackground.b > 250);

    // The two together are the claim: the fade *varies with what is behind*,
    // which a constant cannot fake.
    CHECK(overBackground.r > overWall.r + 40);

    CHECK(magentaPixels(pixels) == 0);

    hp::logRemoveSink(&substitutions);
    hp::logRemoveSink(&compileErrors);
    CHECK(compileErrors.hits() == 0);
    CHECK(substitutions.hits() == 0);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("an opaque material reading the screen is refused by name (T0147)") {
    // **The Done-when's "fails loudly, not garbage".** The snapshot is taken
    // after the opaque pass, so an opaque read would see the previous frame —
    // which works perfectly on the second frame of a static scene and is
    // therefore exactly the kind of bug that ships. The pipeline is not
    // created at all: one log line names the module and the rule, and the
    // surface renders the missing-material checkerboard.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    MessageCatcher refusal("which only a material with alphaMode: Blend may do");
    hp::logAddSink(&refusal);

    std::vector<std::uint8_t> pixels;
    REQUIRE(renderScene(device,
                        {Surface{6.0F, 5.0F, refractivePane("0.0"), hp::AlphaMode::Opaque}},
                        pixels));

    hp::logRemoveSink(&refusal);

    const int magenta = magentaPixels(pixels);
    MESSAGE("opaque screen read: refusals " << refusal.hits() << ", magenta " << magenta
                                            << " px");
    // Once, on the compile attempt — not once per draw, not once per frame.
    CHECK(refusal.hits() == 1);
    // The checkerboard covers the quad, which more than fills the frame; half
    // its checks are magenta.
    CHECK(magenta > (kSize * kSize) / 4);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("a game layer feeds a texture a module declared, by name (T0147.4)") {
    // **T0093's fog-of-war dim, expressible today.** The module declares
    // `Texture2DArray visibility;` — an ordinary T0161 declaration, no engine
    // name and no new syntax — and the *game* supplies the bytes at run time
    // through `setGameTexture`. Nothing in the `.hpmat` mentions it.
    //
    // Channels are chosen so neither outcome is magenta: the checkerboard's
    // texel (2, 2) is (1, 0, 1) and the unbound fallback is (1, 1, 1), so
    //   fed      -> (1 - 0, 1, 0) = yellow
    //   unbound  -> (1 - 1, 1, 0) = green
    // which the magenta guard can then police honestly.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    MessageCatcher compileErrors("slang failed to compile");
    hp::logAddSink(&compileErrors);

    const char* kVisibilityModule = R"(
Texture2DArray visibility;

struct HpMaterial : IHpMaterial
{
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        float4 v = visibility.SampleLevel(HpSamplerPointClamp,
                                          float3(0.125, 0.125, 0.0), 0.0);
        return float4(1.0 - v.g, 1.0, 0.0, 1.0);
    }
    override bool unshaded()
    {
        return true;
    }
}
)";

    std::vector<std::uint8_t> unfed;
    REQUIRE(renderScene(device,
                        {Surface{6.0F, 5.0F, kVisibilityModule, hp::AlphaMode::Opaque}}, unfed,
                        /*screenInputs=*/true, /*feedVisibilityTexture=*/false));
    const Rgb without = centreOf(unfed);
    MESSAGE("no game feed: (" << without.r << ", " << without.g << ", " << without.b << ")");
    // White fallback: `1 - 1` is black in red.
    CHECK(without.r == 0);
    CHECK(without.g == 255);
    CHECK(without.b == 0);

    std::vector<std::uint8_t> fed;
    REQUIRE(renderScene(device,
                        {Surface{6.0F, 5.0F, kVisibilityModule, hp::AlphaMode::Opaque}}, fed,
                        /*screenInputs=*/true, /*feedVisibilityTexture=*/true));
    const Rgb with = centreOf(fed);
    MESSAGE("game feed bound: (" << with.r << ", " << with.g << ", " << with.b << ")");
    // The checkerboard's magenta texel has green 0, so `1 - 0` is full red.
    CHECK(with.r == 255);
    CHECK(with.g == 255);
    CHECK(with.b == 0);

    CHECK(magentaPixels(fed) == 0);
    CHECK(magentaPixels(unfed) == 0);

    hp::logRemoveSink(&compileErrors);
    CHECK(compileErrors.hits() == 0);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("the snapshot is not taken when nothing reads it (T0147.2)") {
    // **The ticket's "it should cost nothing when none does", made observable
    // — and the one place the answer is not simply zero.**
    //
    // Demand is a fact about a module's compiled bytecode, so it is *unknown*
    // until a pipeline has been built for that module, and unknown counts as
    // wanting. That is a deliberate trade and this case measures its exact
    // price over two frames of three scenes:
    //
    //   * a blended **standard** material — no module, so the answer is known
    //     without compiling anything: **0** copies, ever.
    //   * a blended **module that does not read the screen** — **1** copy, on
    //     the first frame only, before the compile that answers the question.
    //   * a blended module that **does** read it — **2**, one per frame, and
    //     the first of them is what makes the very first frame correct rather
    //     than a frame late.
    //
    // The alternative was to let demand lag by a frame, which costs nothing and
    // renders the first frame of every refraction against an uninitialised
    // texture. One speculative copy per module is the cheaper mistake.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    const char* kPlainPane = R"(
struct HpMaterial : IHpMaterial
{
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        return float4(0.0, 1.0, 0.0, 0.5);
    }
    override bool unshaded()
    {
        return true;
    }
}
)";

    MessageCatcher snapshots("scene snapshot:");
    hp::logAddSink(&snapshots);
    hp::logSetGlobalLevel(hp::LogLevel::Debug);

    std::vector<std::uint8_t> standard;
    REQUIRE(renderScene(device,
                        {
                            Surface{6.0F, 6.0F, kRampWall, hp::AlphaMode::Opaque},
                            Surface{6.0F, 3.0F, "", hp::AlphaMode::Blend},
                        },
                        standard, /*screenInputs=*/true, /*feedVisibilityTexture=*/false,
                        /*frames=*/2));
    const int withStandard = snapshots.hits();
    snapshots.reset();

    std::vector<std::uint8_t> quiet;
    REQUIRE(renderScene(device,
                        {
                            Surface{6.0F, 6.0F, kRampWall, hp::AlphaMode::Opaque},
                            Surface{6.0F, 3.0F, kPlainPane, hp::AlphaMode::Blend},
                        },
                        quiet, /*screenInputs=*/true, /*feedVisibilityTexture=*/false,
                        /*frames=*/2));
    const int withoutRead = snapshots.hits();
    snapshots.reset();

    std::vector<std::uint8_t> reading;
    REQUIRE(renderScene(device,
                        {
                            Surface{6.0F, 6.0F, kRampWall, hp::AlphaMode::Opaque},
                            Surface{6.0F, 3.0F, refractivePane("0.0"), hp::AlphaMode::Blend},
                        },
                        reading, /*screenInputs=*/true, /*feedVisibilityTexture=*/false,
                        /*frames=*/2));
    const int withRead = snapshots.hits();

    hp::logSetGlobalLevel(hp::LogLevel::Info);
    hp::logRemoveSink(&snapshots);

    MESSAGE("snapshot copies over two frames: standard blended material " << withStandard
            << ", module that does not read " << withoutRead << ", module that reads "
            << withRead);
    // Known without compiling anything, so not even the first frame pays.
    CHECK(withStandard == 0);
    // One speculative copy, then silence: the compile of frame 1 answered it.
    CHECK(withoutRead == 1);
    // Every frame, including the first — which is the point.
    CHECK(withRead == 2);

    CHECK(magentaPixels(standard) == 0);
    CHECK(magentaPixels(quiet) == 0);
    CHECK(magentaPixels(reading) == 0);

    hp::Vfs::shutdown();
    tearDown(device);
}
