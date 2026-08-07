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

/// How far in front of the camera the quad sits, in metres.
///
/// A **distance**, so it stays positive; the camera looks down -Z since T0165,
/// so the quad's plane is at `-kQuadDistance`. Keeping the sign out of the
/// constant is what lets `kQuadHalf`'s `distance * tan(fov / 2)` below stay the
/// framing formula it is meant to be rather than acquiring an `abs`.
constexpr float kQuadDistance = 3.0F;

/// Half the quad's width, chosen so it **exactly fills the square frame**.
///
/// **Not a round number, and it must not become one.** A quad larger than the
/// frame is not a rendering error, so nothing fails — but the image then shows a
/// *crop* of the texture, and every judgement made by eye against the source is
/// silently comparing different regions. The first version of this test used a
/// half-size of 4 at this distance, which shows the middle 43% at 2.3x, and it
/// read as a sampler bug for exactly as long as it took to notice.
///
/// Derived from the camera rather than hard-coded, so changing the default field
/// of view cannot quietly reintroduce the crop. Vertical half-height equals
/// horizontal half-width here because the target is square.
const float kQuadHalf = kQuadDistance * std::tan(hp::Camera{}.verticalFov * 0.5F);

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
    const float h = kQuadHalf;
    const float z = -kQuadDistance;
    const float vertices[] = {
        -h, -h, z,  0.0F, 0.0F, 1.0F,  1.0F, 0.0F, 0.0F, 1.0F,  0.0F, 1.0F,
         h, -h, z,  0.0F, 0.0F, 1.0F,  1.0F, 0.0F, 0.0F, 1.0F,  1.0F, 1.0F,
         h,  h, z,  0.0F, 0.0F, 1.0F,  1.0F, 0.0F, 0.0F, 1.0F,  1.0F, 0.0F,
        -h,  h, z,  0.0F, 0.0F, 1.0F,  1.0F, 0.0F, 0.0F, 1.0F,  0.0F, 0.0F,
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
      "min": [)" + std::to_string(-kQuadHalf) + R"(, )" + std::to_string(-kQuadHalf) +
        R"(, )" + std::to_string(-kQuadDistance) + R"(],
      "max": [)" + std::to_string(kQuadHalf) + R"(, )" + std::to_string(kQuadHalf) +
        R"(, )" + std::to_string(-kQuadDistance) + R"(] },
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
/// @param device the brought-up device.
/// @param prefix which texture set: "rock" or "metal".
/// @param pixels receives the frame as RGBA.
/// @param debugView which channel to draw instead of the shaded image.
bool renderTextured(Device& device, const std::string& prefix, std::vector<std::uint8_t>& pixels,
                    hp::SurfaceDebugView debugView = hp::SurfaceDebugView::None) {
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
    hp::Camera camera;
    camera.debugView = debugView;
    cameraEntity.add<hp::Camera>(camera);

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
    // **Raked, and a head-on light is why this needed saying.** A light entity
    // with an identity transform points straight down -Z (`Light.cpp` takes the
    // local Z axis and negates it), which since T0165 is coincident with both
    // the view direction and the quad's normal — so `N·L` is 1 across the whole
    // surface.
    //
    // That is the **worst possible angle for judging a normal map**: the visible
    // effect of perturbing a normal scales with how much it changes `N·L`, and at
    // perpendicular incidence that derivative is at its minimum. A bumpy surface
    // and a flat one render nearly identically, and the frame comes back evenly
    // veiled with no directional cue — which reads as a broken sampler and is
    // not one. It cost an afternoon of looking for a bug in the shader.
    //
    // Rotated about X and then Y so the light arrives from above and to one
    // side, the way a product shot is lit. Relief now casts short shadows on the
    // side away from it, which is the thing a normal-map test has to be able to
    // see.
    // **Sign checked by measurement, not by eye.** `ResolvedLight::direction` is
    // the direction light *travels* (`PBR_Shading.fxh` computes `L = -lightDir`),
    // so a **positive Y** component means light heading upward — that is, coming
    // from *below*. The first version of this had exactly that, and produced the
    // under-lit look of a torch held at floor level. The probe below asserts the
    // sign so it cannot silently invert again.
    constexpr float kRake = 0.7F; // ~40 degrees
    // **The pi yaw is gone with T0165, and the yaw's sign flipped to keep the
    // light where it was.** T0152 needed the half-turn because a lamp's -Z and
    // the camera's +Z disagreed; they agree now. Dropping it alone would have
    // left the rake arriving from the opposite side (measured: travel x goes
    // -0.49 -> +0.49), which is a different scene and would move every recorded
    // value for no reason -- so the Y rake is negated with it. The resulting
    // travel is (-0.493, -0.644, -0.585): the exact mirror in z of the
    // pre-T0165 (-0.493, -0.644, +0.585), same elevation, same side. The
    // `direction.y < 0` assert below is unchanged and still guards "from
    // above".
    lightEntity.get<hp::Transform>().rotation =
        hp::Quaternion::RotationFromAxisAngle(hp::float3{0.0F, 1.0F, 0.0F}, kRake) *
        hp::Quaternion::RotationFromAxisAngle(hp::float3{1.0F, 0.0F, 0.0F}, -kRake);
    scene.propagateTransforms();
    {
        // **The sign is asserted, not eyeballed.** `direction` is the way light
        // *travels* (`PBR_Shading.fxh` does `L = -lightDir`), so a positive Y
        // means it comes from *below* — which is what the first version of this
        // did, and which under-lights every surface in a way that reads as a
        // broken normal map. One assertion is cheaper than rediscovering it.
        const hp::LightList probe = hp::gatherLights(scene);
        REQUIRE(probe.size() == 1);
        CHECK(probe[0].direction.y < 0.0F);
    }

    hp::SceneView view;
    if (!view.create(device.render->device(), device.render->context(), kSize, kSize)) {
        return false;
    }
    // Blue, so "nothing drew" stays distinguishable from every shading outcome.
    view.setClearColour(0.0F, 0.0F, 1.0F, 1.0F);

    hp::SceneViewStats stats;
    if (view.render(device.render->context(), scene, pool, 0, &stats) ==
        nullptr) {
        return false;
    }
    if (stats.submitted != 1) {
        return false;
    }
    return view.readback(device.render->context(), pixels);
}

} // namespace

/// Every surface channel, drawn on its own (T0141).
///
/// **This is the test that makes the other one trustworthy.** A shaded frame
/// mixes base colour, normal, roughness, metalness and occlusion into three
/// numbers per pixel, so a channel wired to the wrong texture, the wrong
/// component, or nothing at all still produces something that looks like a lit
/// material. Every bug found in this ticket had exactly that shape — the
/// `TextureAttribIndices` array that defaulted to -1 disabled three subsystems
/// and changed the image not at all.
///
/// Each channel is written out as a PPM as well as asserted, because the
/// assertions can only check relationships and the image is what shows *which*
/// relationship is wrong.
TEST_CASE("each surface channel can be inspected on its own") {
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

    struct Channel {
        const char* name;
        hp::SurfaceDebugView view;
    };
    const Channel channels[] = {
        {"basecolor", hp::SurfaceDebugView::BaseColor},
        {"roughness", hp::SurfaceDebugView::Roughness},
        {"metallic", hp::SurfaceDebugView::Metallic},
        {"occlusion", hp::SurfaceDebugView::Occlusion},
        {"meshnormal", hp::SurfaceDebugView::MeshNormal},
        {"shadingnormal", hp::SurfaceDebugView::ShadingNormal},
        {"texcoord0", hp::SurfaceDebugView::Texcoord0},
    };

    std::vector<std::uint8_t> meshNormal;
    std::vector<std::uint8_t> shadingNormal;

    for (const Channel& channel : channels) {
        std::vector<std::uint8_t> pixels;
        REQUIRE(renderTextured(device, "rock", pixels, channel.view));
        const Rgb centre = centreOf(pixels);
        const double variation = variationOf(pixels);
        const std::filesystem::path image =
            writePpm(std::string("channel_") + channel.name, pixels);
        MESSAGE(channel.name << ": centre " << centre.text() << ", variation " << variation
                             << " -> " << image.string());

        if (channel.view == hp::SurfaceDebugView::MeshNormal) {
            meshNormal = pixels;
        } else if (channel.view == hp::SurfaceDebugView::ShadingNormal) {
            shadingNormal = pixels;
        }
    }

    SUBCASE("occlusion comes from the texture, not the material factor") {
        // **This regressed silently for the entire ticket.** `EnableAO` was off,
        // so `USE_AO_MAP` was 0, `GetOcclusion` was never compiled in, and
        // `Occlusion` sat at `basic.OcclusionFactor` = 1.0 — a flat white channel
        // while a real AO map sat bound and unread. Zero variation is the
        // signature, and nothing else in the engine could show it.
        std::vector<std::uint8_t> pixels;
        REQUIRE(renderTextured(device, "rock", pixels, hp::SurfaceDebugView::Occlusion));
        const double variation = variationOf(pixels);
        MESSAGE("occlusion variation " << variation);
        CHECK(variation > 1.0);
    }

    SUBCASE("metalness comes from the blue channel, and only the metal set has any") {
        // **The check the shaded frame could never make.** Rock is a dielectric,
        // so its ORM blue channel is zero and a metallic term wired to green, to
        // red, or to nothing at all produces the same black image. Metal has real
        // metalness. Asserting one is zero and the other is not is what pins the
        // channel down.
        std::vector<std::uint8_t> rockMetal;
        std::vector<std::uint8_t> metalMetal;
        REQUIRE(renderTextured(device, "rock", rockMetal, hp::SurfaceDebugView::Metallic));
        REQUIRE(renderTextured(device, "metal", metalMetal, hp::SurfaceDebugView::Metallic));
        writePpm("channel_metallic_metal", metalMetal);
        const Rgb rockCentre = centreOf(rockMetal);
        const Rgb metalCentre = centreOf(metalMetal);
        MESSAGE("metallic: rock " << rockCentre.text() << ", metal " << metalCentre.text());
        CHECK(rockCentre.r == 0);
        CHECK(metalCentre.r > 0);
    }

    SUBCASE("the normal map is actually applied") {
        // **The decisive check, and it needs both frames.** The quad is flat, so
        // its mesh normal is one constant colour across every pixel. If the
        // shading normal is *also* flat, the normal map is not reaching the
        // lighting — which is invisible in a shaded frame and is exactly what a
        // head-on light hides.
        REQUIRE_FALSE(meshNormal.empty());
        REQUIRE_FALSE(shadingNormal.empty());
        const double flat = variationOf(meshNormal);
        const double perturbed = variationOf(shadingNormal);
        MESSAGE("mesh normal variation " << flat << ", shading normal variation " << perturbed);
        CHECK(flat < 1.0);
        CHECK(perturbed > 5.0);
    }

    tearDown(device);
}

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
        // **Deliberately lower than rock's, and not a fudged number.** A metal
        // has no diffuse response, so with `EnableIBL = false` there is no
        // environment for it to reflect and the frame is dominated by near-black
        // with specular glints — genuinely less detail than a dielectric, not a
        // worse texture path. The original 8.0 here was calibrated against a
        // frame that turned out to be 2.3x magnified, which inflates variation;
        // it failed the moment the framing was corrected.
        //
        // **The real metalness check is not here.** "Each surface channel can be
        // inspected on its own" asserts rock's metallic channel is exactly 0 and
        // metal's is not, which is the assertion a shaded frame cannot make.
        // T0087's environment lighting is what will make this one meaningful.
        CHECK(variation > 3.0);
    }

    tearDown(device);
}
