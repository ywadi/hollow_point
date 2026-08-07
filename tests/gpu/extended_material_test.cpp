// The extended material features reach the pixels (T0143, amending D24).
//
// Clearcoat, sheen, anisotropy, iridescence, transmission and volume — each
// authored on a `hp::Material`, each through its own PSO permutation, each
// asserted twice: once in the shaded frame (does the feature change what a
// lamp does) and once through its debug view (is the *channel* wired, which
// the shaded frame cannot prove — the occlusion lesson).
//
// **What these cases can and cannot claim is stated per case, deliberately.**
// There is no IBL and no ambient in this engine yet (T0087 is open), and
// clearcoat, sheen and the volume family are largely *reflection* phenomena:
// under a single head-on lamp, clearcoat is one more specular lobe and a
// Fresnel dim, sheen is an energy trade, and volume is nothing at all. The
// assertions here pin that the features are wired and change the punctual
// response the way upstream's math says; they do not — cannot — validate
// that a coat looks like lacquer. That claim waits for an environment.
//
// The scene mirrors lit_surface_test: a red quad facing the camera, one
// white directional lamp yawed pi so it travels +Z onto the quad's front
// (post-T0152 aim), assigned materials through the T0060 override path.
//
// Bucket: gpu. Skips cleanly with no device.

#include <doctest/doctest.h>

#include <hp/Assets.hpp>
#include <hp/Camera.hpp>
#include <hp/Light.hpp>
#include <hp/Log.hpp>
#include <hp/Material.hpp>
#include <hp/Render.hpp>
#include <hp/Scene.hpp>
#include <hp/SceneView.hpp>
#include <hp/Vfs.hpp>
#include <hp/Window.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
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
    windowConfig.title = "hp extended material test";
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

/// The suite's camera-facing quad: -Z normals, wound to match (D33).
void writeQuadGltf(const std::filesystem::path& directory) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    // position(3) normal(3) = 6 floats per vertex; BL, BR, TR, TL.
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
    "pbrMetallicRoughness": {
      "baseColorFactor": [ 0.5, 0.5, 0.5, 1.0 ],
      "metallicFactor": 0.0,
      "roughnessFactor": 1.0
  } } ],
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
    [[nodiscard]] std::string text() const {
        return "(" + std::to_string(r) + ", " + std::to_string(g) + ", " + std::to_string(b) + ")";
    }
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
    return n == 0 ? Rgb{}
                  : Rgb{static_cast<int>(r / n), static_cast<int>(g / n), static_cast<int>(b / n)};
}

/// The magenta guard, applied to every asserted frame: a failed shader
/// renders (~127, 0, 127) and passes most difference assertions. Made twice
/// before this suite existed; never again.
bool looksMagenta(const Rgb& c) {
    return c.r > 100 && c.r < 160 && c.g < 40 && c.b > 100 && c.b < 160;
}

/// One fixture: quad + camera + the post-T0152 lamp, rendered with a given
/// material assigned. The scene is rebuilt per render so every case reads as
/// its own scene; the mesh is loaded once per fixture.
struct Fixture {
    hp::AssetPool pool;
    hp::Guid meshGuid;
    Device* device = nullptr;
    std::filesystem::path scratch;

    bool up(Device& dev) {
        device = &dev;
        std::error_code ec;
        scratch = std::filesystem::temp_directory_path() / "hp-extended-material";
        std::filesystem::remove_all(scratch, ec);
        std::filesystem::create_directories(scratch / "models", ec);
        writeQuadGltf(scratch / "models");

        hp::Vfs::shutdown();
        if (!hp::Vfs::init(nullptr) || !hp::Vfs::mount(scratch.string())) {
            return false;
        }
        meshGuid = hp::Guid::generate();
        auto mesh = hp::loadMesh(device->render->device(), device->render->context(),
                                 "models/quad.gltf");
        if (!mesh || !mesh->valid()) {
            return false;
        }
        pool.store<hp::MeshAsset>(meshGuid, mesh);
        return true;
    }

    void down() {
        hp::Vfs::shutdown();
    }

    /// Renders the quad with @p materialGuid assigned, returning the pixels.
    std::vector<std::uint8_t> render(hp::Guid materialGuid,
                                     hp::SurfaceDebugView view = hp::SurfaceDebugView::None) {
        std::vector<std::uint8_t> pixels;

        hp::Scene scene;
        hp::Entity cameraEntity = scene.create("camera");
        hp::Camera camera;
        camera.debugView = view;
        cameraEntity.add<hp::Camera>(camera);

        hp::Entity quad = scene.create("quad");
        hp::MeshRenderer renderer;
        renderer.mesh = meshGuid;
        renderer.materials = {materialGuid};
        quad.add<hp::MeshRenderer>(renderer);

        hp::Entity lightEntity = scene.create("sun");
        hp::Light sun;
        sun.type = hp::LightType::Directional;
        sun.colour = hp::float3{1.0F, 1.0F, 1.0F};
        sun.intensity = 3.0F;
        lightEntity.add<hp::Light>(sun);
        // Yawed pi about Y so the light travels +Z onto the quad's front —
        // the post-T0152 aim; see lit_surface_test.
        lightEntity.get<hp::Transform>().rotation =
            hp::Quaternion::RotationFromAxisAngle(hp::float3{0.0F, 1.0F, 0.0F}, 3.14159265F);
        scene.propagateTransforms();

        hp::SceneView sceneView;
        if (!sceneView.create(device->render->device(), device->render->context(), kSize,
                              kSize)) {
            return pixels;
        }
        // Blue, so "nothing drew" and "everything transmitted" are both
        // distinguishable from every shading outcome.
        sceneView.setClearColour(0.0F, 0.0F, 1.0F, 1.0F);

        hp::SceneViewStats stats;
        if (sceneView.render(device->render->context(), scene, pool, 0, &stats) == nullptr) {
            return pixels;
        }
        if (stats.submitted != 1) {
            return pixels;
        }
        if (!sceneView.readback(device->render->context(), pixels)) {
            pixels.clear();
        }
        return pixels;
    }
};

/// The control material every case diverges from: red, rough, dielectric —
/// the lit_surface scene's material as an asset.
std::shared_ptr<hp::Material> baseMaterial() {
    auto material = std::make_shared<hp::Material>();
    material->baseColour = hp::float4{1.0F, 0.0F, 0.0F, 1.0F};
    material->metallic = 0.0F;
    material->roughness = 1.0F;
    material->doubleSided = true;
    return material;
}

} // namespace

TEST_CASE("extended material features shape the punctual response, one by one (T0143)") {
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    Fixture fx;
    REQUIRE(fx.up(device));

    // The control: the base material through the same assigned-material path
    // every feature case uses. Everything below is asserted *against* this,
    // which is what attributes a difference to the feature rather than to the
    // scene.
    const hp::Guid controlGuid = hp::Guid::generate();
    fx.pool.store<hp::Material>(controlGuid, baseMaterial());
    const std::vector<std::uint8_t> controlPixels = fx.render(controlGuid);
    REQUIRE(!controlPixels.empty());
    const Rgb control = centreOf(controlPixels);
    MESSAGE("control (red rough dielectric): " << control.text());
    CHECK(!looksMagenta(control));
    CHECK(control.r > 100);
    CHECK(control.g == control.b); // white light: achromatic specular only

    SUBCASE("clearcoat adds a second lobe and dims what is underneath") {
        // Punctual-only honesty: this asserts the coat's *lobe and Fresnel
        // dim* exist head-on. That a coat reads as lacquer is an environment
        // claim (T0087) and is deliberately not made here.
        const hp::Guid guid = hp::Guid::generate();
        {
            auto material = baseMaterial();
            material->clearcoat = 1.0F;
            material->clearcoatRoughness = 0.3F;
            fx.pool.store<hp::Material>(guid, material);
        }
        const std::vector<std::uint8_t> pixels = fx.render(guid);
        REQUIRE(!pixels.empty());
        const Rgb coat = centreOf(pixels);
        MESSAGE("clearcoat 1.0 / roughness 0.3: " << coat.text());
        CHECK(!looksMagenta(coat));
        // The coat's achromatic GGX lifts green and blue equally; the white
        // light keeps them locked together.
        CHECK(coat.g == coat.b);
        CHECK(coat.g > control.g + 10);
        // And the frame is not simply brighter red — the Fresnel dim takes
        // from the base layer.
        CHECK(coat.r != control.r);

        // The wiring proof the shaded frame cannot give (the occlusion
        // lesson): the factor channel itself, white where the coat is 1.
        const std::vector<std::uint8_t> debugPixels =
            fx.render(guid, hp::SurfaceDebugView::ClearCoatFactor);
        REQUIRE(!debugPixels.empty());
        const Rgb factor = centreOf(debugPixels);
        MESSAGE("ClearCoatFactor view: " << factor.text());
        CHECK(factor.r == 255);
        CHECK(factor.g == 255);
        CHECK(factor.b == 255);

        // The control's factor channel must be black — a channel that reads
        // 1 everywhere is exactly the silently-wrong wiring this view exists
        // to catch.
        const std::vector<std::uint8_t> controlDebug =
            fx.render(controlGuid, hp::SurfaceDebugView::ClearCoatFactor);
        REQUIRE(!controlDebug.empty());
        const Rgb controlFactor = centreOf(controlDebug);
        CHECK(controlFactor.r == 0);
        CHECK(controlFactor.g == 0);
        CHECK(controlFactor.b == 0);
    }

    SUBCASE("sheen trades base energy for a sheen lobe, through the embedded LUT") {
        // This case is also the sheen LUT's binding proof: the permutation
        // samples g_SheenAlbedoScalingLUT, so an unbound or undecoded LUT
        // fails this draw loudly rather than anything passing.
        //
        // Punctual-only honesty: head-on, sheen is mostly the albedo-scaling
        // *dim* plus a small Charlie term; the velvet rim needs grazing
        // angles and an environment (T0087).
        const hp::Guid guid = hp::Guid::generate();
        {
            auto material = baseMaterial();
            material->sheenColour = hp::float3{1.0F, 1.0F, 1.0F};
            material->sheenRoughness = 0.5F;
            fx.pool.store<hp::Material>(guid, material);
        }
        const std::vector<std::uint8_t> pixels = fx.render(guid);
        REQUIRE(!pixels.empty());
        const Rgb sheen = centreOf(pixels);
        MESSAGE("sheen white / roughness 0.5: " << sheen.text());
        CHECK(!looksMagenta(sheen));
        CHECK(sheen.g == sheen.b);
        // The energy trade must move the frame measurably, in either
        // direction — identical pixels would mean the blocks compiled out.
        const bool moved = std::abs(sheen.r - control.r) > 4 ||
                           std::abs(sheen.g - control.g) > 4;
        CHECK(moved);

        const std::vector<std::uint8_t> debugPixels =
            fx.render(guid, hp::SurfaceDebugView::SheenColor);
        REQUIRE(!debugPixels.empty());
        const Rgb colour = centreOf(debugPixels);
        MESSAGE("SheenColor view: " << colour.text());
        CHECK(colour.r == 255);
        CHECK(colour.g == 255);
        CHECK(colour.b == 255);
    }

    SUBCASE("anisotropy reshapes the specular lobe") {
        // A flat quad under one head-on lamp shows the reshape as a changed
        // specular *value* (alpha-T goes 0.25 -> 1.0 at strength 1); the
        // stretched-highlight look needs curvature and an environment. Both
        // honest halves are stated on the ticket.
        const hp::Guid guid = hp::Guid::generate();
        {
            auto material = baseMaterial();
            material->roughness = 0.5F;
            material->anisotropyStrength = 1.0F;
            fx.pool.store<hp::Material>(guid, material);
        }
        // The isotropic control at the same roughness, so the only variable
        // is the anisotropy.
        const hp::Guid isoGuid = hp::Guid::generate();
        {
            auto material = baseMaterial();
            material->roughness = 0.5F;
            fx.pool.store<hp::Material>(isoGuid, material);
        }
        const std::vector<std::uint8_t> isoPixels = fx.render(isoGuid);
        const std::vector<std::uint8_t> anisoPixels = fx.render(guid);
        REQUIRE(!isoPixels.empty());
        REQUIRE(!anisoPixels.empty());
        const Rgb iso = centreOf(isoPixels);
        const Rgb aniso = centreOf(anisoPixels);
        MESSAGE("isotropic r=0.5: " << iso.text() << "; anisotropic strength 1: " << aniso.text());
        CHECK(!looksMagenta(aniso));
        CHECK(aniso.g == aniso.b);
        const bool moved = std::abs(aniso.r - iso.r) > 4 || std::abs(aniso.g - iso.g) > 4;
        CHECK(moved);

        const std::vector<std::uint8_t> strengthPixels =
            fx.render(guid, hp::SurfaceDebugView::AnisotropyStrength);
        REQUIRE(!strengthPixels.empty());
        const Rgb strength = centreOf(strengthPixels);
        MESSAGE("AnisotropyStrength view: " << strength.text());
        CHECK(strength.r == 255);
        CHECK(strength.g == 255);
        CHECK(strength.b == 255);

        // Direction (1, 0) — no map, no rotation — encodes as full red,
        // mid green, zero blue.
        const std::vector<std::uint8_t> directionPixels =
            fx.render(guid, hp::SurfaceDebugView::AnisotropyDirection);
        REQUIRE(!directionPixels.empty());
        const Rgb direction = centreOf(directionPixels);
        MESSAGE("AnisotropyDirection view: " << direction.text());
        CHECK(direction.r == 255);
        CHECK(direction.g > 180); // 0.5 through the sRGB encode ~ 188
        CHECK(direction.g < 195);
        CHECK(direction.b == 0);
    }

    SUBCASE("iridescence makes the specular chromatic — visible with no environment at all") {
        // The one extended feature whose punctual signature is unambiguous:
        // the thin film bends F0 per wavelength, so the white lamp's
        // achromatic specular becomes *coloured* and g == b breaks. This is
        // a real validation, not a wiring check.
        const hp::Guid guid = hp::Guid::generate();
        {
            auto material = baseMaterial();
            material->roughness = 0.3F;
            material->iridescence = 1.0F;
            material->iridescenceThicknessMin = 400.0F;
            material->iridescenceThicknessMax = 400.0F;
            fx.pool.store<hp::Material>(guid, material);
        }
        const std::vector<std::uint8_t> pixels = fx.render(guid);
        REQUIRE(!pixels.empty());
        const Rgb irid = centreOf(pixels);
        MESSAGE("iridescence 1.0 at 400 nm: " << irid.text());
        CHECK(!looksMagenta(irid));
        // The film's interference colour split the white specular.
        CHECK(std::abs(irid.g - irid.b) > 4);

        const std::vector<std::uint8_t> debugPixels =
            fx.render(guid, hp::SurfaceDebugView::IridescenceFactor);
        REQUIRE(!debugPixels.empty());
        const Rgb factor = centreOf(debugPixels);
        MESSAGE("IridescenceFactor view: " << factor.text());
        CHECK(factor.r == 255);
        CHECK(factor.g == 255);
        CHECK(factor.b == 255);
    }

    SUBCASE("transmission removes the diffuse and lets the scene behind through") {
        // Blend-pass compositing at alpha = 1 - transmission: over the blue
        // clear, a fully transmissive red quad must go blue-dominant. The
        // *refractive* version — bending what shows through — is T0087's
        // image-based path, stated on the ticket rather than claimed here.
        const hp::Guid guid = hp::Guid::generate();
        {
            auto material = baseMaterial();
            material->alphaMode = hp::AlphaMode::Blend;
            material->transmission = 1.0F;
            fx.pool.store<hp::Material>(guid, material);
        }
        const std::vector<std::uint8_t> pixels = fx.render(guid);
        REQUIRE(!pixels.empty());
        const Rgb through = centreOf(pixels);
        MESSAGE("transmission 1.0 over blue clear: " << through.text());
        CHECK(!looksMagenta(through));
        CHECK(through.b > 200);          // the clear colour survives
        CHECK(through.r < control.r / 2); // the red diffuse is gone

        const std::vector<std::uint8_t> debugPixels =
            fx.render(guid, hp::SurfaceDebugView::Transmission);
        REQUIRE(!debugPixels.empty());
        const Rgb factor = centreOf(debugPixels);
        MESSAGE("Transmission view: " << factor.text());
        CHECK(factor.r == 255);
        CHECK(factor.g == 255);
        CHECK(factor.b == 255);
    }

    SUBCASE("volume thickness is carried, debug-visible, and shades nothing — by design") {
        // The honest pin (T0143.9's scope note in test form): thickness's
        // only consumer is T0087's image-based refraction, so today it must
        // change no shaded pixel. When T0087 lands, this assertion flips
        // deliberately instead of a behaviour changing silently.
        const hp::Guid guid = hp::Guid::generate();
        {
            auto material = baseMaterial();
            material->thickness = 0.5F;
            fx.pool.store<hp::Material>(guid, material);
        }
        const std::vector<std::uint8_t> pixels = fx.render(guid);
        REQUIRE(!pixels.empty());
        const Rgb volume = centreOf(pixels);
        MESSAGE("thickness 0.5 shaded: " << volume.text() << " versus control "
                                         << control.text());
        CHECK(!looksMagenta(volume));
        CHECK(volume.r == control.r);
        CHECK(volume.g == control.g);
        CHECK(volume.b == control.b);

        // But the channel is genuinely wired: 0.5 through the sRGB encode.
        const std::vector<std::uint8_t> debugPixels =
            fx.render(guid, hp::SurfaceDebugView::Thickness);
        REQUIRE(!debugPixels.empty());
        const Rgb thickness = centreOf(debugPixels);
        MESSAGE("Thickness view: " << thickness.text());
        CHECK(thickness.r > 180);
        CHECK(thickness.r < 195);
        CHECK(thickness.g == thickness.r);
        CHECK(thickness.b == thickness.r);
    }

    fx.down();
    tearDown(device);
}
