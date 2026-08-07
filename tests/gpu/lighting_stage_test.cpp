// The lighting stage: a game owns the response to a light (T0145, D30/D31).
//
// **This is the acceptance test for the last rung of D30's ladder**, and the
// technique it proves is the one the whole NPR family was waiting on. Cel
// shading is quantised `N·L`, and until this ticket `N·L` lived inside
// DiligentFX's `ApplyPunctualLight` with no seam to reach it — the BRDF is
// called inline at `PBR_Shading.fxh:690`. Faking it in `surface()` bands the
// *sum*, which is wrong under two lights and is exactly Godot proposal #484's
// open complaint.
//
// Four things are asserted here, each of which was a claim on the ticket:
//
//   * **rung 3 renders** — a module overriding only `light()` produces flat
//     quantised bands where the standard material produces a smooth ramp, and
//     the band *count* is asserted rather than eyeballed;
//   * **attenuation and the cone stay the engine's** — the same override
//     ignores geometry entirely (`NdotL = 1`, no normal read), and a narrow
//     spot light still leaves the quad's corners black. The only thing that
//     can have darkened them is the engine's cone;
//   * **rung 4 renders** — a module owning the whole stage replaces the loop;
//   * **the default is the standard path, to the byte** — a module whose
//     `light()` override is `return HpStandardLight(L, S);` renders a frame
//     *identical* to the standard material's, which is the D28 claim ("the
//     defaults are the standard material") measured rather than promised;
//   * **the mirror carries the lamp's vocabulary and `Index == 0` is the
//     dominant light** (D31, and the ordering contract this ticket decided).
//
// **Every asserted frame carries a magenta guard.** A failed shader renders the
// missing-material checkerboard (~127, 0, 127), which passes coverage and
// variation assertions happily — made twice in this repository, so a
// distinct-colour count is never trusted without confirming the frame is not
// the checkerboard.
//
// Bucket: gpu. Skips cleanly with no device.

#include <doctest/doctest.h>

#include <hp/Assets.hpp>
#include <hp/Camera.hpp>
#include <hp/Light.hpp>
#include <hp/Log.hpp>
#include <hp/Material.hpp>
#include <hp/Math.hpp>
#include <hp/Render.hpp>
#include <hp/Scene.hpp>
#include <hp/SceneView.hpp>
#include <hp/Vfs.hpp>
#include <hp/Window.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <set>
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
    windowConfig.title = "hp lighting stage test";
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

/// A white quad facing the camera, wound per `WindingConvention.hpp` (D33).
///
/// `{0, 2, 1, 0, 3, 2}` for a −Z-facing normal, and copying the *other* order
/// is exactly how the backwards test assets spread before T0152.
void writeQuadGltf(const std::filesystem::path& directory) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    std::vector<unsigned char> bin;
    const float vertices[] = {
        -4.0F, -4.0F, 3.0F, 0.0F, 0.0F, -1.0F,
         4.0F, -4.0F, 3.0F, 0.0F, 0.0F, -1.0F,
         4.0F,  4.0F, 3.0F, 0.0F, 0.0F, -1.0F,
        -4.0F,  4.0F, 3.0F, 0.0F, 0.0F, -1.0F,
    };
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
      "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
      "metallicFactor": 0.0, "roughnessFactor": 1.0 }
  } ],
  "buffers": [ { "uri": "quad.bin", "byteLength": )" + std::to_string(bin.size()) + R"( } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": )" + std::to_string(vertexBytes) +
                             R"(, "byteStride": 24 },
    { "buffer": 0, "byteOffset": )" + std::to_string(vertexBytes) + R"(, "byteLength": 12 }
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
    [[nodiscard]] std::string text() const {
        return "(" + std::to_string(r) + ", " + std::to_string(g) + ", " + std::to_string(b) + ")";
    }
    bool operator<(const Rgb& other) const {
        return std::tie(r, g, b) < std::tie(other.r, other.g, other.b);
    }
};

Rgb pixelAt(const std::vector<std::uint8_t>& rgba, int x, int y) {
    const std::size_t i = (static_cast<std::size_t>(y) * kSize + x) * 4;
    if (i + 3 >= rgba.size()) {
        return Rgb{};
    }
    return Rgb{rgba[i], rgba[i + 1], rgba[i + 2]};
}

/// The distinct colours in the middle half of the frame.
///
/// The instrument for "is this banded or smooth": a quantised falloff produces
/// a handful of flat plateaus, an unquantised one produces a value per
/// gradient step.
std::set<Rgb> paletteOf(const std::vector<std::uint8_t>& rgba) {
    std::set<Rgb> palette;
    for (int y = kSize / 4; y < kSize * 3 / 4; ++y) {
        for (int x = kSize / 4; x < kSize * 3 / 4; ++x) {
            palette.insert(pixelAt(rgba, x, y));
        }
    }
    return palette;
}

/// Loud-magenta pixels in the middle half — the checkerboard's signature.
///
/// **Called on every asserted frame.** A shader that fails to compile renders
/// (~127, 0, 127) checks and still satisfies a coverage or variation
/// assertion, which has been mistaken for a pass twice here.
int magentaChecks(const std::vector<std::uint8_t>& rgba) {
    int magenta = 0;
    for (int y = kSize / 4; y < kSize * 3 / 4; ++y) {
        for (int x = kSize / 4; x < kSize * 3 / 4; ++x) {
            const Rgb p = pixelAt(rgba, x, y);
            if (p.r > 90 && p.b > 90 && p.g < 60) {
                ++magenta;
            }
        }
    }
    return magenta;
}

/// What lights a scene carries for one case.
struct Lamp {
    hp::LightType type = hp::LightType::Directional;
    hp::float3 colour{1.0F, 1.0F, 1.0F};
    float intensity = 1.0F;
    float range = 10.0F;
    float innerCone = 0.4363F;
    float outerCone = 0.6109F;
    hp::float3 position{0.0F, 0.0F, 0.0F};
    /// Half-turn about Y, so the lamp travels **+Z** onto the quad's −Z face.
    /// A light travels down its local negative Z (glTF), and leaving this off
    /// lights the quad from behind — the sign error the convention invites.
    bool faceTheQuad = true;
};

/// Renders the quad under `lamps`, with an optional `.slang` module.
///
/// @param moduleSource the module text; empty renders the **standard
///        material**, which is what the byte-identity case compares against.
/// @param lamps the scene's lights, created in the order given — which matters
///        for the dominant-light case, where entity order is deliberately the
///        *opposite* of intensity order.
bool renderLit(Device& device, const std::string& moduleSource, const std::vector<Lamp>& lamps,
               std::vector<std::uint8_t>& pixels) {
    std::error_code ec;
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "hp-lighting-stage";
    std::filesystem::remove_all(scratch, ec);
    std::filesystem::create_directories(scratch / "models", ec);
    std::filesystem::create_directories(scratch / "materials", ec);
    writeQuadGltf(scratch / "models");
    if (!moduleSource.empty()) {
        std::ofstream file(scratch / "materials" / "lighting.slang");
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

    hp::Scene scene;
    hp::Entity cameraEntity = scene.create("camera");
    cameraEntity.add<hp::Camera>(hp::Camera{});

    hp::Entity quad = scene.create("quad");
    hp::MeshRenderer renderer;
    renderer.mesh = meshGuid;

    if (!moduleSource.empty()) {
        const hp::Guid shaderGuid = hp::Guid::generate();
        auto shader = hp::loadShader("materials/lighting.slang");
        if (!shader || !shader->valid()) {
            return false;
        }
        pool.store<hp::ShaderAsset>(shaderGuid, shader);

        const hp::Guid materialGuid = hp::Guid::generate();
        auto material = std::make_shared<hp::Material>();
        material->shader = shaderGuid;
        material->doubleSided = true;
        // **Matched to the glTF's own factors, and leaving them at the struct
        // defaults cost the byte-identity case its first run.** `Material`
        // defaults `metallic` to 1, the glTF above declares 0, and the
        // standard-material control renders through the *glTF* material while
        // a module renders through this one -- so the two frames differed in
        // every pixel for a reason that had nothing to do with the light loop.
        material->metallic = 0.0F;
        material->roughness = 1.0F;
        pool.store<hp::Material>(materialGuid, material);
        renderer.materials = {materialGuid};
    }
    quad.add<hp::MeshRenderer>(renderer);

    for (std::size_t i = 0; i < lamps.size(); ++i) {
        const Lamp& lamp = lamps[i];
        hp::Entity entity = scene.create(("lamp" + std::to_string(i)).c_str());
        hp::Light light;
        light.type = lamp.type;
        light.colour = lamp.colour;
        light.intensity = lamp.intensity;
        light.range = lamp.range;
        light.innerConeAngle = lamp.innerCone;
        light.outerConeAngle = lamp.outerCone;
        entity.add<hp::Light>(light);
        hp::Transform placed;
        placed.position = lamp.position;
        if (lamp.faceTheQuad) {
            placed.rotation = hp::Quaternion::RotationFromAxisAngle(
                hp::float3{0.0F, 1.0F, 0.0F}, 3.14159265F);
        }
        scene.setLocalTransform(entity, placed);
    }
    scene.propagateTransforms();

    hp::SceneView view;
    if (!view.create(device.render->device(), device.render->context(), kSize, kSize)) {
        return false;
    }
    // Blue, so "nothing drew" is distinguishable from every shading outcome.
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

// --- the modules under test -------------------------------------------------
//
// Written as a game would write them: `struct HpMaterial : IHpMaterial`,
// `override` mandatory, no includes, nothing but the contract.

/// The domed normal every falloff case shares.
///
/// **Scaffolding, not the technique.** A flat quad has one `N·L` for every
/// fragment, so there is nothing for a quantiser to band. Bending the normal
/// into a dome sweeps `N·L` from 1 at the frame's centre to 1/3 at the corners
/// of the sampled region, which is a full falloff inside a single draw.
///
/// Built from `In.ScreenUV` rather than from the world position **so the sweep
/// is frame-relative**: driving it from `WorldPos` made the gradient depend on
/// how much of the quad the camera happened to show, and the first run
/// quantised to a single band because the sampled region only spanned
/// `N·L ∈ [0.85, 1]`.
constexpr const char* kDome = R"(
    [mutating] override float3 shadingNormal(VSOutput VSOut, HpSurfaceInput In,
                                             float3 geometricNormal, bool isFrontFace)
    {
        float2 fromCentre = (In.ScreenUV - 0.5) * 8.0;
        return normalize(float3(fromCentre.x, fromCentre.y, -1.0));
    }
)";

/// **Cel shading — the whole of it is `R.NdotL = ceil(R.NdotL * 4) / 4`.**
///
/// Diffuse and specular are written outright so a band is exactly flat and the
/// palette count is an exact number rather than a threshold: what remains is
/// `BaseColor * Radiance * quantised`, and `BaseColor` and `Radiance` are both
/// constant across this quad.
std::string celModule() {
    return std::string(R"(
struct HpMaterial : IHpMaterial
{)") + kDome + R"(
    [mutating] override HpLightResponse light(HpLight L, HpShadedSurface S, HpSurfaceInput In)
    {
        HpLightResponse R;
        R.Diffuse   = S.BaseColor;
        R.Specular  = float3(0.0, 0.0, 0.0);
        R.Intensity = L.Radiance;
        R.NdotL     = ceil(saturate(dot(S.Normal, L.ToLight)) * 4.0) / 4.0;
        return R;
    }
}
)";
}

/// The same material with the quantiser removed — the control.
std::string smoothModule() {
    return std::string(R"(
struct HpMaterial : IHpMaterial
{)") + kDome + R"(
    [mutating] override HpLightResponse light(HpLight L, HpShadedSurface S, HpSurfaceInput In)
    {
        HpLightResponse R;
        R.Diffuse   = S.BaseColor;
        R.Specular  = float3(0.0, 0.0, 0.0);
        R.Intensity = L.Radiance;
        R.NdotL     = saturate(dot(S.Normal, L.ToLight));
        return R;
    }
}
)";
}

/// Ignores geometry entirely and paints the engine's own attenuation.
///
/// **The instrument for "the engine still owns attenuation and the cone".**
/// Nothing here reads a normal, a position or a direction, so anything that
/// varies across the quad came from `HpGetLight`.
constexpr const char* kAttenuationModule = R"(
struct HpMaterial : IHpMaterial
{
    [mutating] override HpLightResponse light(HpLight L, HpShadedSurface S, HpSurfaceInput In)
    {
        HpLightResponse R;
        R.Diffuse   = float3(L.Attenuation, L.Attenuation, L.Attenuation);
        R.Specular  = float3(0.0, 0.0, 0.0);
        R.Intensity = float3(1.0, 1.0, 1.0);
        R.NdotL     = 1.0;
        return R;
    }
}
)";

/// Rung 3's default, spelled out. Must render identically to no module at all.
constexpr const char* kPassThroughModule = R"(
struct HpMaterial : IHpMaterial
{
    [mutating] override HpLightResponse light(HpLight L, HpShadedSurface S, HpSurfaceInput In)
    {
        return HpStandardLight(L, S);
    }
}
)";

/// **Rung 4** — the whole stage. Counts the lights the engine handed over and
/// paints the count, ignoring the loop the default would have run.
constexpr const char* kWholeStageModule = R"(
struct HpMaterial : IHpMaterial
{
    [mutating] override float3 lighting(HpShadedSurface S, HpSurfaceInput In)
    {
        // Red carries how many lights reached this fragment, green the sum of
        // their attenuations -- both facts only the loop's owner can see.
        float count = float(HpLightCount()) * 0.25;
        float total = 0.0;
        for (int i = 0; i < HpLightCount(); ++i)
        {
            total += HpGetLight(i, S.Position).Attenuation * 0.5;
        }
        return HpResolveLighting(S, float3(count, total, 0.0));
    }
}
)";

/// D31's mirror, read back as colour: the lamp's type, its range in metres, and
/// its cone — none of which DiligentFX's packed struct hands over in those
/// units, and none of which Godot's `light()` exposes at all.
constexpr const char* kVocabularyModule = R"(
struct HpMaterial : IHpMaterial
{
    [mutating] override float3 lighting(HpShadedSurface S, HpSurfaceInput In)
    {
        HpLight L = HpGetLight(0, S.Position);
        return HpResolveLighting(S, float3(
            L.Type == HpLightSpot ? 1.0 : 0.0,   // the type, which Godot cannot ask
            L.Range * 0.1,                       // metres, not Range^4
            L.SpotOuterCos));                    // a cosine, not a scale/offset pair
    }
}
)";

/// `Index == 0` is the dominant light: red when the brightest lamp is first.
constexpr const char* kDominantModule = R"(
struct HpMaterial : IHpMaterial
{
    [mutating] override float3 lighting(HpShadedSurface S, HpSurfaceInput In)
    {
        // Whichever lamp the engine put first, reported by its own brightness.
        // 0.2 for the fill, 0.8 for the key -- so the pixel says which arrived.
        HpLight first = HpGetLight(0, S.Position);
        float luminance = dot(first.Color, float3(0.2126, 0.7152, 0.0722));
        return HpResolveLighting(S, float3(luminance, float(HpLightCount()) * 0.25, 0.0));
    }
}
)";


// --- the fill-cost bench (145.8) -------------------------------------------

/// The bench's resolution. Large enough that the fragment shader dominates a
/// frame whose geometry is two triangles, which is the whole point: this
/// measures the light loop, not submission.
constexpr int kBenchSize = 512;

/// A scene held across many draws, for the cost loop.
struct Bench {
    hp::AssetPool pool;
    hp::Scene scene;
    hp::SceneView view;

    /// @p repeats draws before the single readback — the device-overhead
    /// amortiser the triplanar bench established. A readback-synchronised
    /// frame costs about a millisecond and one light loop over half a
    /// megapixel costs far less, so timing one draw per sync measures the
    /// sync.
    bool render(Device& device, int repeats) {
        for (int i = 0; i < repeats; ++i) {
            hp::SceneViewStats stats;
            if (view.render(device.render->context(), scene, pool, 0, &stats) == nullptr ||
                stats.submitted != 1) {
                return false;
            }
        }
        std::vector<std::uint8_t> sink;
        const bool ok = view.readback(device.render->context(), sink);
        // **Every iteration must be a real engine frame.** `onRender` presents,
        // and presenting is what recycles Diligent's dynamic heap; without it a
        // few hundred offscreen renders exhaust it and every draw afterwards
        // silently writes nothing — which turned half the triplanar bench's
        // first numbers into measurements of a blank frame.
        device.render->onRender();
        return ok;
    }
};

/// Builds the bench: the standard material under `lampCount` point lights, all
/// in range of the quad so every one of them is real work.
bool setUpBench(Device& device, Bench& bench, int lampCount) {
    std::error_code ec;
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "hp-lighting-bench";
    std::filesystem::remove_all(scratch, ec);
    std::filesystem::create_directories(scratch / "models", ec);
    writeQuadGltf(scratch / "models");

    hp::Vfs::shutdown();
    if (!hp::Vfs::init(nullptr) || !hp::Vfs::mount(scratch.string())) {
        return false;
    }

    const hp::Guid meshGuid = hp::Guid::generate();
    auto mesh =
        hp::loadMesh(device.render->device(), device.render->context(), "models/quad.gltf");
    if (!mesh || !mesh->valid()) {
        return false;
    }
    bench.pool.store<hp::MeshAsset>(meshGuid, mesh);

    hp::Entity cameraEntity = bench.scene.create("camera");
    cameraEntity.add<hp::Camera>(hp::Camera{});

    hp::Entity quad = bench.scene.create("quad");
    hp::MeshRenderer renderer;
    renderer.mesh = meshGuid;
    quad.add<hp::MeshRenderer>(renderer);

    for (int i = 0; i < lampCount; ++i) {
        hp::Entity entity = bench.scene.create(("lamp" + std::to_string(i)).c_str());
        hp::Light light;
        // **Point lights, deliberately.** A directional light skips the whole
        // range-and-cone block, which is exactly the part this ticket mirrored;
        // timing the loop against lights that take the cheap branch would
        // measure nothing.
        light.type = hp::LightType::Point;
        light.intensity = 1.0F;
        light.range = 30.0F;
        entity.add<hp::Light>(light);
        hp::Transform placed;
        // Spread across the quad's front so the attenuation varies and no two
        // lights share a divergence-free path through the loop.
        placed.position = hp::float3{static_cast<float>(i % 4) - 1.5F,
                                     static_cast<float>(i / 4) - 1.5F, 0.0F};
        bench.scene.setLocalTransform(entity, placed);
    }
    bench.scene.propagateTransforms();

    if (!bench.view.create(device.render->device(), device.render->context(), kBenchSize,
                           kBenchSize)) {
        return false;
    }
    bench.view.setClearColour(0.0F, 0.0F, 1.0F, 1.0F);
    return true;
}

} // namespace

TEST_CASE("a per-light override cel-shades, and attenuation stays the engine's (T0145.3)") {
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    const std::vector<Lamp> sun{Lamp{}};

    std::vector<std::uint8_t> smooth;
    REQUIRE(renderLit(device, smoothModule(), sun, smooth));
    CHECK(magentaChecks(smooth) == 0);
    const std::set<Rgb> smoothPalette = paletteOf(smooth);

    std::vector<std::uint8_t> cel;
    REQUIRE(renderLit(device, celModule(), sun, cel));
    CHECK(magentaChecks(cel) == 0);
    const std::set<Rgb> celPalette = paletteOf(cel);

    MESSAGE("cel: " << celPalette.size() << " distinct colours; smooth control: "
                    << smoothPalette.size());

    // **The assertion the NPR row was blocked on.** The same material, the same
    // light, the same surface — one line of difference inside `light()` — and
    // the falloff goes from a continuous ramp to a countable set of plateaus.
    // `ceil(x * 4) / 4` over an `N·L` that never reaches 0 on this dome can
    // produce at most four values.
    CHECK(celPalette.size() <= 4);
    CHECK(smoothPalette.size() >= 16);
    CHECK(smoothPalette.size() > celPalette.size() * 4);

    // The bands are *flat*, not merely fewer: a horizontal scan through the
    // centre crosses plateaus, so neighbouring pixels are equal far more often
    // than they differ.
    int equalNeighbours = 0;
    int steps = 0;
    for (int x = kSize / 4; x + 1 < kSize * 3 / 4; ++x) {
        (pixelAt(cel, x, kSize / 2) < pixelAt(cel, x + 1, kSize / 2) ||
         pixelAt(cel, x + 1, kSize / 2) < pixelAt(cel, x, kSize / 2))
            ? ++steps
            : ++equalNeighbours;
    }
    MESSAGE("cel centre scanline: " << equalNeighbours << " flat neighbours, " << steps
                                    << " steps");
    CHECK(steps <= 6);
    CHECK(equalNeighbours > steps * 5);

    // --- attenuation and the cone remain engine-computed --------------------
    //
    // The module below reads no geometry at all. If the corners of the quad go
    // dark under a narrow spot and stay lit under a wide one, the only thing
    // that can have done it is `HpGetLight`'s cone.
    Lamp spot;
    spot.type = hp::LightType::Spot;
    spot.intensity = 20.0F;
    spot.range = 20.0F;
    spot.position = hp::float3{0.0F, 0.0F, 0.0F};
    spot.innerCone = 0.15F;
    spot.outerCone = 0.25F;

    std::vector<std::uint8_t> narrow;
    REQUIRE(renderLit(device, kAttenuationModule, {spot}, narrow));
    CHECK(magentaChecks(narrow) == 0);

    Lamp wide = spot;
    wide.innerCone = 1.0F;
    wide.outerCone = 1.3F;
    std::vector<std::uint8_t> broad;
    REQUIRE(renderLit(device, kAttenuationModule, {wide}, broad));
    CHECK(magentaChecks(broad) == 0);

    const Rgb narrowCentre = pixelAt(narrow, kSize / 2, kSize / 2);
    const Rgb narrowCorner = pixelAt(narrow, kSize / 4 + 2, kSize / 4 + 2);
    const Rgb broadCorner = pixelAt(broad, kSize / 4 + 2, kSize / 4 + 2);
    MESSAGE("engine-computed cone: narrow centre " << narrowCentre.text() << ", narrow corner "
                                                   << narrowCorner.text() << ", wide corner "
                                                   << broadCorner.text());
    CHECK(narrowCentre.r > 0);
    // Exactly zero: outside the outer cone the engine's attenuation is 0, and
    // a zero-attenuation light is skipped before the override is ever called.
    CHECK(narrowCorner.r == 0);
    CHECK(broadCorner.r > 0);

    tearDown(device);
}

TEST_CASE("the per-light default is the standard path, to the pixel (T0145.1)") {
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    Lamp sun;
    sun.intensity = 3.0F;

    std::vector<std::uint8_t> standard;
    REQUIRE(renderLit(device, "", {sun}, standard));
    CHECK(magentaChecks(standard) == 0);

    std::vector<std::uint8_t> viaHook;
    REQUIRE(renderLit(device, kPassThroughModule, {sun}, viaHook));
    CHECK(magentaChecks(viaHook) == 0);

    // **The claim D28 makes about every rung, measured on the newest one.** A
    // module that routes the per-light method straight back to the engine's own
    // default must be indistinguishable from having written no module at all —
    // not close, identical. If the mirror's arithmetic had drifted by one
    // multiply's associativity this would be a handful of least-significant
    // bits, which is exactly the failure a "looks right" comparison misses.
    REQUIRE(standard.size() == viaHook.size());
    std::size_t differing = 0;
    for (std::size_t i = 0; i < standard.size(); ++i) {
        if (standard[i] != viaHook[i]) {
            ++differing;
        }
    }
    const Rgb lit = pixelAt(standard, kSize / 2, kSize / 2);
    MESSAGE("standard vs HpStandardLight through the hook: " << differing
                                                             << " differing bytes; centre "
                                                             << lit.text());
    CHECK(differing == 0);
    // And the frame is a lit surface rather than two identical black ones,
    // which would satisfy the comparison above and prove nothing.
    CHECK(lit.r > 60);

    tearDown(device);
}

TEST_CASE("a whole-stage override owns the loop, and the mirror carries the lamp (T0145.4)") {
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    // Two directional lamps, so the count is something the default could not
    // have produced by accident.
    Lamp key;
    key.intensity = 1.0F;
    Lamp fill;
    fill.intensity = 1.0F;

    std::vector<std::uint8_t> owned;
    REQUIRE(renderLit(device, kWholeStageModule, {key, fill}, owned));
    CHECK(magentaChecks(owned) == 0);
    const Rgb counted = pixelAt(owned, kSize / 2, kSize / 2);
    MESSAGE("rung 4, two directional lamps: " << counted.text());
    // 2 lights * 0.25 = 0.5 linear -> 188 in sRGB; both attenuations are 1, so
    // green is 2 * 0.5 = 1.0 -> 255. The exact numbers matter: they say the
    // override replaced the loop rather than running beside it.
    CHECK(counted.r == 188);
    CHECK(counted.g == 255);
    CHECK(counted.b == 0);

    // --- D31's mirror, in the units it promises ----------------------------
    Lamp spot;
    spot.type = hp::LightType::Spot;
    spot.intensity = 5.0F;
    spot.range = 7.0F;
    spot.innerCone = 0.4363F;
    spot.outerCone = 0.6109F;

    std::vector<std::uint8_t> vocabulary;
    REQUIRE(renderLit(device, kVocabularyModule, {spot}, vocabulary));
    CHECK(magentaChecks(vocabulary) == 0);
    const Rgb mirrored = pixelAt(vocabulary, kSize / 2, kSize / 2);
    MESSAGE("HpLight mirror (type, range/10, outer cone cos): " << mirrored.text());
    // Red: the type is `HpLightSpot`, which is the fact Godot's `light()` has
    // no way to ask for.
    CHECK(mirrored.r == 255);
    // Green: 7 m as 0.7 linear, which is `sqrt(sqrt(Range4))` unpacked — the
    // authored number, not the fourth power the constant buffer carries.
    // 0.7 linear is 218 in sRGB; one step either way for the round trip.
    CHECK(mirrored.g >= 216);
    CHECK(mirrored.g <= 220);
    // Blue: cos(0.6109) = 0.8192 linear -> 234 sRGB. A cosine, recovered from
    // the scale/offset pair the packing stores.
    CHECK(mirrored.b >= 232);
    CHECK(mirrored.b <= 236);

    tearDown(device);
}

TEST_CASE("light index 0 is the dominant light, whatever order the scene built them (T0145)") {
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    // **The dim lamp is created first, deliberately.** Both are directional, so
    // both carry the same (zero) distance key, and before this ticket
    // `std::sort`'s instability decided which reached index 0 — measured, on
    // this machine it was whichever the registry happened to hold first, which
    // is this one. Only the luminance rule can put the bright lamp first.
    Lamp fill;
    fill.colour = hp::float3{1.0F, 1.0F, 1.0F};
    fill.intensity = 0.2F;
    Lamp keyLight;
    keyLight.colour = hp::float3{1.0F, 1.0F, 1.0F};
    keyLight.intensity = 0.8F;

    std::vector<std::uint8_t> pixels;
    REQUIRE(renderLit(device, kDominantModule, {fill, keyLight}, pixels));
    CHECK(magentaChecks(pixels) == 0);
    const Rgb first = pixelAt(pixels, kSize / 2, kSize / 2);
    MESSAGE("Lights[0] luminance painted as red, light count as green: " << first.text());

    // 0.8 linear is 231 in sRGB; the fill's 0.2 would be 124. The gap is far
    // wider than any round-trip error, which is why intensities this far apart
    // were chosen.
    CHECK(first.r > 200);
    // Both lights are present — so this is an ordering result, not a selection
    // one. 2 * 0.25 = 0.5 linear -> 188.
    CHECK(first.g == 188);

    tearDown(device);
}

TEST_CASE("the interface-shaped light loop's cost is measured, not assumed (T0145.8)") {
    // **D30 named this as unmeasured and owed here**, in as many words: *"an
    // interface-heavy main with per-light methods may cost occupancy against
    // the fused original. Unmeasured; T0145 measures before/after on the
    // byte-identical baseline."*
    //
    // The before/after against the pre-T0145 shader is on the ticket — it needs
    // two builds and cannot live in one test run. What lives here is the
    // *instrument*, so the number is reproducible rather than a note in a
    // commit message: the standard material's per-light fill cost, at a
    // resolution where the fragment shader dominates.
    //
    // Point lights, because a directional light skips the range-and-cone block
    // that is the mirrored part.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    constexpr int kRepeats = 32;
    double msByCount[2] = {0.0, 0.0};
    long long covered = 0;
    int slot = 0;
    for (const int lampCount : {1, 9}) {
        Bench bench;
        REQUIRE(setUpBench(device, bench, lampCount));

        {
            std::vector<std::uint8_t> pixels;
            hp::SceneViewStats stats;
            REQUIRE(bench.view.render(device.render->context(), bench.scene, bench.pool, 0,
                                      &stats) != nullptr);
            REQUIRE(bench.view.readback(device.render->context(), pixels));
            if (slot == 0) {
                for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
                    const bool clear =
                        pixels[i] == 0 && pixels[i + 1] == 0 && pixels[i + 2] == 255;
                    covered += clear ? 0 : 1;
                }
            }
        }

        for (int warm = 0; warm < 3; ++warm) {
            REQUIRE(bench.render(device, kRepeats));
        }
        const auto start = std::chrono::steady_clock::now();
        int frames = 0;
        double elapsed = 0.0;
        while ((elapsed < 1.5 && frames < 40) || frames < 6) {
            REQUIRE(bench.render(device, kRepeats));
            ++frames;
            elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        }
        msByCount[slot] = elapsed * 1000.0 / (static_cast<double>(frames) * kRepeats);
        ++slot;
        hp::Vfs::shutdown();
    }

    // A configuration that rasterises nothing measures nothing — the failure
    // the triplanar bench hit with an edge-on quad.
    REQUIRE(covered > 0);
    const double perLightMs = (msByCount[1] - msByCount[0]) / 8.0;
    const double nsPerPixelPerLight =
        perLightMs * 1.0e6 / static_cast<double>(covered);
    MESSAGE("light loop: 1 lamp " << msByCount[0] << " ms/draw, 9 lamps " << msByCount[1]
                                  << " ms/draw over " << covered << " covered pixels = "
                                  << nsPerPixelPerLight << " ns/pixel/light");

    // Deliberately loose, and it is a *sanity* bound rather than a budget: a
    // punctual light on a rough dielectric is a few dozen ALU, so a per-pixel
    // per-light cost in the tens of nanoseconds would mean the loop is not
    // being specialised at all.
    CHECK(nsPerPixelPerLight > 0.0);
    CHECK(nsPerPixelPerLight < 20.0);

    tearDown(device);
}
