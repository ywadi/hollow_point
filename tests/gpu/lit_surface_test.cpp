// A lit surface is the colour it was authored as (T0079).
//
// Bucket: gpu. **This is the assertion T0134 recorded as owed and nothing in
// this repository could make.** Every pixel test written before this one could
// only ask "did geometry reach the target", because the engine rendered
// everything pure black — no lights, no IBL, no emissive — so black-on-clear-
// colour was the only signal available. T0028's mesh test passes against a
// completely broken shading path for exactly that reason.
//
// So this test does the one thing that distinguishes shading from geometry: it
// puts a **red** quad under a **white** light and checks the pixels come back
// red. A frame that is black, white, green, or the clear colour all fail, and
// each of those is a different real bug:
//
//   * black  — the light is not reaching the shader at all
//   * clear  — nothing drew (the T0028 class of failure)
//   * white  — base colour is being ignored
//   * dark   — the light is pointing the wrong way, which is the sign error the
//              glTF negative-Z convention invites
//
// **The light points down +Z and the quad faces the camera at z = 3**, so the
// surface normal (0, 0, -1) faces the light head-on and the geometric term is at
// its maximum. That is deliberate: a grazing angle would make the expected value
// depend on the exact BRDF, and this test is about whether light arrives, not
// about whether Diligent's PBR is correct.

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

Device bringUp(hp::RenderBackend backend) {
    static const bool sink = [] {
        hp::logAddConsoleSink();
        return true;
    }();
    (void)sink;

    hp::WindowConfig windowConfig;
    windowConfig.title = "hp lit surface test";
    windowConfig.width = kSize;
    windowConfig.height = kSize;

    Device device;
    device.window = hp::Window::create(windowConfig);
    if (!device.window) {
        return device;
    }
    hp::RenderConfig renderConfig;
    renderConfig.backend = backend;
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

/// A quad facing the camera, with a base colour given as glTF.
///
/// Rough and non-metallic on purpose: a smooth metal surface reflects almost
/// nothing without an environment map (T0087), so it would come back black and
/// look exactly like the bug this test exists to catch.
void writeQuadGltf(const std::filesystem::path& directory, float r, float g, float b) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    const float vertices[] = {
        -4.0F, -4.0F, 3.0F, 0.0F, 0.0F, -1.0F,
         4.0F, -4.0F, 3.0F, 0.0F, 0.0F, -1.0F,
         4.0F,  4.0F, 3.0F, 0.0F, 0.0F, -1.0F,
        -4.0F,  4.0F, 3.0F, 0.0F, 0.0F, -1.0F,
    };
    // Wound consistently with the authored -Z normals (right-hand rule) --
    // re-wound by T0152, whose trace found the old {0,1,2, 0,2,3} order
    // pointing the winding-defined front face away from the camera while the
    // NORMAL attributes pointed at it.
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
      "baseColorFactor": [ )" + std::to_string(r) + ", " + std::to_string(g) + ", "
                             + std::to_string(b) + R"(, 1.0 ],
      "metallicFactor": 0.0,
      "roughnessFactor": 1.0
  } } ],
  "buffers": [ { "uri": "quad.bin", "byteLength": )"
                             + std::to_string(bin.size()) + R"( } ],
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
            if (i + 3 >= rgba.size()) {
                continue;
            }
            r += rgba[i];
            g += rgba[i + 1];
            b += rgba[i + 2];
            ++n;
        }
    }
    return n == 0 ? Rgb{}
                  : Rgb{static_cast<int>(r / n), static_cast<int>(g / n), static_cast<int>(b / n)};
}

void exerciseLitSurface(hp::RenderBackend backend, const char* backendName) {
    Device device = bringUp(backend);
    if (!device.ok()) {
        MESSAGE("no " << std::string(backendName)
                      << " device available -- skipping (this is expected on CI)");
        tearDown(device);
        return;
    }

    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "hp_lit_surface_test";
    std::error_code ec;
    std::filesystem::remove_all(scratch, ec);
    // Pure red, so a channel that should be dark and is not is unmistakable.
    writeQuadGltf(scratch / "models", 1.0F, 0.0F, 0.0F);

    REQUIRE(hp::Vfs::init(nullptr));
    REQUIRE(hp::Vfs::mount(scratch.string()));

    hp::AssetPool pool;
    const hp::Guid meshGuid = hp::Guid::generate();
    {
        auto loaded =
            hp::loadMesh(device.render->device(), device.render->context(), "models/quad.gltf");
        REQUIRE(loaded);
        REQUIRE(loaded->valid());
        pool.store<hp::MeshAsset>(meshGuid, loaded);
    }

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
    // Yawed pi about Y: a light travels down its local **negative** Z, so the
    // half-turn makes it travel +Z -- from the camera's side onto the quad's
    // -Z-facing front. Before T0152 re-wound the assets this was an identity
    // transform and the quad was lit from behind, which only looked right
    // because the backwards winding inverted the two-sided normal flip.
    lightEntity.get<hp::Transform>().rotation =
        hp::Quaternion::RotationFromAxisAngle(hp::float3{0.0F, 1.0F, 0.0F}, 3.14159265F);
    scene.propagateTransforms();

    hp::SceneView view;
    REQUIRE(view.create(device.render->device(), device.render->context(), kSize, kSize));
    // Blue, so "nothing drew" is distinguishable from every shading outcome.
    view.setClearColour(0.0F, 0.0F, 1.0F, 1.0F);

    hp::SceneViewStats stats;
    REQUIRE(view.render(device.render->context(), scene, pool, 0,
                        &stats) != nullptr);
    CHECK(stats.submitted == 1);

    std::vector<std::uint8_t> pixels;
    REQUIRE(view.readback(device.render->context(), pixels));
    const Rgb lit = centreOf(pixels);
    MESSAGE("lit red quad under a white light: " << lit.text());

    // **The assertion this ticket exists for.** Each failure mode is a different
    // real bug, which is why they are checked separately rather than as one
    // distance.
    CHECK(lit.r > 60);                    // light arrives at all -- not black
    CHECK(lit.r > lit.g + 40);            // base colour survives -- recognisably red
    CHECK(lit.r > lit.b + 40);            // and not the blue clear colour
    CHECK(lit.g == lit.b);                // white light, so the non-red channels match

    // **Measured (242, 25, 25) since T0152 re-wound the asset, not
    // (255, 0, 0), and that is correct**: a white light on a rough dielectric
    // produces an achromatic specular term, so green and blue lift equally
    // while base colour keeps red far ahead. **The pre-T0152 value here was
    // (211, 144, 144), and the difference is the winding fix measured**: the
    // backwards asset put the flipped shading normal *away* from the camera,
    // `NdotV` sat at its 1e-4 clamp, and the Fresnel term blew the achromatic
    // specular up to 144 -- the old baseline quietly encoded the inversion.
    // T0152's "survives by symmetry" prediction was wrong for exactly that
    // reason (symmetry holds for `N·L`, not for the view-dependent terms), and
    // the ticket records it. Asserting the *ordering* and the g == b symmetry
    // is what distinguishes "lit red surface" from every failure mode without
    // pinning a BRDF this test does not own -- which is also why every one of
    // these checks survived a change that moved the exact values this much.

    SUBCASE("a mirrored node shades like the unmirrored one -- the determinant "
            "rule's shader half") {
        // **T0152.5's GPU-half probe.** The quad's material is double-sided
        // (`CULL_MODE_NONE`), so a mirror never makes the geometry disappear
        // -- what a (-1, 1, 1) entity scale flips is raw `SV_IsFrontFace`.
        // Without the determinant correction in `evaluateSurface`, the
        // two-sided flip then turns the authored normal *away* from the
        // viewer, `N·L` goes negative, and this frame renders black with the
        // draw submitted and the light on -- the exact inverted-shading bug
        // the backwards-wound assets used to produce, reproduced by data.
        //
        // The quad is x-symmetric and the mirror leaves N, V and L untouched
        // (the normal is pure -Z), so the corrected frame must match the
        // unmirrored one within readback rounding -- the triangulation
        // diagonal flips with the mirror, which can move an averaged channel
        // by a count, and no more.
        hp::Transform placement = quad.get<hp::Transform>();
        placement.scale = hp::float3{-1.0F, 1.0F, 1.0F};
        scene.setLocalTransform(quad, placement);
        scene.propagateTransforms();

        REQUIRE(view.render(device.render->context(), scene, pool, 0,
                            &stats) != nullptr);
        CHECK(stats.submitted == 1);
        std::vector<std::uint8_t> mirroredPixels;
        REQUIRE(view.readback(device.render->context(), mirroredPixels));
        const Rgb mirrored = centreOf(mirroredPixels);
        MESSAGE("mirrored lit quad: " << mirrored.text() << " versus unmirrored: " << lit.text());

        CHECK(std::abs(mirrored.r - lit.r) <= 2);
        CHECK(std::abs(mirrored.g - lit.g) <= 2);
        CHECK(std::abs(mirrored.b - lit.b) <= 2);
        // The magenta guard, and the white-light symmetry: a failed shader's
        // (127, 0, 127) fails both.
        CHECK(mirrored.g == mirrored.b);
        CHECK(mirrored.r > 60);
    }

    SUBCASE("removing the light returns the frame to black") {
        // The control, and the proof that the colour above came from the light
        // rather than from anything else in the pipeline.
        scene.destroy(lightEntity);
        scene.propagateTransforms();

        REQUIRE(view.render(device.render->context(), scene, pool, 0,
                            &stats) != nullptr);
        std::vector<std::uint8_t> unlit;
        REQUIRE(view.readback(device.render->context(), unlit));
        const Rgb dark = centreOf(unlit);
        MESSAGE("same quad with no light: " << dark.text());

        CHECK(dark.r < 20);
        CHECK(dark.g < 20);
        CHECK(dark.b < 20);
    }

    SUBCASE("a disabled light contributes nothing") {
        lightEntity.get<hp::Light>().enabled = false;

        REQUIRE(view.render(device.render->context(), scene, pool, 0,
                            &stats) != nullptr);
        std::vector<std::uint8_t> unlit;
        REQUIRE(view.readback(device.render->context(), unlit));
        const Rgb dark = centreOf(unlit);
        CHECK(dark.r < 20);
    }

    SUBCASE("a brighter light produces a brighter surface") {
        // Monotonicity, which is a cheap way to catch intensity being dropped on
        // the floor -- a fixed value would pass every check above.
        lightEntity.get<hp::Light>().intensity = 0.5F;
        REQUIRE(view.render(device.render->context(), scene, pool, 0,
                            &stats) != nullptr);
        std::vector<std::uint8_t> dim;
        REQUIRE(view.readback(device.render->context(), dim));
        const Rgb dimmer = centreOf(dim);
        MESSAGE("intensity 0.5: " << dimmer.text() << " versus 3.0: " << lit.text());
        CHECK(dimmer.r < lit.r);
        CHECK(dimmer.r > 0);
    }

    SUBCASE("a point light attenuates with distance and stops at its range") {
        // **The half of the light model the directional case cannot reach.**
        // `Light::range` becomes `Range4` and the shader applies glTF's
        // `clamp(1 - (d/range)^4, 0, 1) / d^2` — wired through Diligent's own
        // packing, and until this subcase existed, entirely unexercised.
        hp::Light& lamp = lightEntity.get<hp::Light>();
        lamp.type = hp::LightType::Point;
        lamp.intensity = 30.0F;
        lamp.range = 20.0F;

        const auto renderAt = [&](float z) {
            hp::Transform placement;
            placement.position = hp::float3{0.0F, 0.0F, z};
            scene.setLocalTransform(lightEntity, placement);
            scene.propagateTransforms();
            REQUIRE(view.render(device.render->context(), scene, pool, 0, &stats) != nullptr);
            std::vector<std::uint8_t> pixels;
            REQUIRE(view.readback(device.render->context(), pixels));
            return centreOf(pixels);
        };

        // The quad sits at z = 3 and faces the camera (T0152 re-wound the
        // asset, so the winding, the normals and the visible side finally
        // agree). Nearer is brighter -- that is attenuation -- and the lamp
        // now sits on the camera's side, where a lamp lighting the visible
        // face belongs. The same distances as before the re-wind, so the
        // measured pair carries over by symmetry.
        const Rgb near = renderAt(1.0F);
        const Rgb far = renderAt(-6.0F);
        MESSAGE("point light 2 units away: " << near.text() << ", 9 units away: " << far.text());
        CHECK(near.r > 0);
        CHECK(near.r > far.r);

        // Beyond `range` the attenuation term clamps to zero, so the surface is
        // unlit however bright the lamp is. A light that kept reaching past its
        // range would make range a decoration rather than a bound.
        const Rgb beyond = renderAt(-34.0F);
        MESSAGE("point light past its range: " << beyond.text());
        CHECK(beyond.r < 8);
    }

    SUBCASE("a spot light lights its cone and not the world outside it") {
        // Aimed the same way as the directional light that works above, so the
        // only variable is the cone.
        hp::Light& lamp = lightEntity.get<hp::Light>();
        lamp.type = hp::LightType::Spot;
        lamp.intensity = 60.0F;
        lamp.range = 20.0F;

        // On the camera's side of the quad, aimed at the visible face: the
        // pi yaw turns the spot's travel direction from -Z to +Z, exactly as
        // the directional light above (T0152).
        hp::Transform placement;
        placement.position = hp::float3{0.0F, 0.0F, 1.0F};
        placement.rotation =
            hp::Quaternion::RotationFromAxisAngle(hp::float3{0.0F, 1.0F, 0.0F}, 3.14159265F);
        scene.setLocalTransform(lightEntity, placement);
        scene.propagateTransforms();

        const auto renderWithCone = [&](float inner, float outer) {
            lamp.innerConeAngle = inner;
            lamp.outerConeAngle = outer;
            REQUIRE(view.render(device.render->context(), scene, pool, 0, &stats) != nullptr);
            std::vector<std::uint8_t> pixels;
            REQUIRE(view.readback(device.render->context(), pixels));
            return centreOf(pixels);
        };

        // A wide cone covers the sampled centre; a very narrow one does not
        // reach nearly as much of it. Comparing two cones rather than asserting
        // an absolute value keeps this about the cone and not about the BRDF.
        const Rgb wide = renderWithCone(0.7F, 1.2F);
        const Rgb narrow = renderWithCone(0.01F, 0.02F);
        MESSAGE("spot wide cone: " << wide.text() << ", narrow cone: " << narrow.text());
        CHECK(wide.r > 0);
        CHECK(wide.r > narrow.r);
    }

    hp::Vfs::shutdown();
    view.release();
    tearDown(device);
    std::filesystem::remove_all(scratch, ec);
}

} // namespace

TEST_CASE("a lit surface is the colour it was authored as, on the default backend"
          * doctest::test_suite("gpu")) {
    exerciseLitSurface(hp::RenderBackend::Default, "default");
}
