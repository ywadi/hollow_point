// Material assets reach the pixels, and a missing one is loud (T0141.12).
//
// **This is the three-state table of T0060.10, asserted on the render target.**
// The resolver was pinned in the fast bucket when it landed; what could not be
// tested there is what the renderer *draws* for each state, because that needs
// a device. These are those assertions:
//
//   * `Missing`  — the magenta-and-black checkerboard, and **no light can dim
//     it**: the scene here has no light at all, so anything shaded would come
//     back black. The pattern showing up is the proof the fallback is unlit,
//     which T0060 calls the half that gets missed.
//   * `Assigned` — the material asset's own values, not the model's imported
//     material. An unlit green material must produce *exactly* (0, 255, 0):
//     unshaded writes the base colour straight to the sRGB target, so any
//     other number means a light, a texture or a factor leaked in.
//   * `Imported` — every other gpu test in this suite: they all render with an
//     empty override vector through the same path, so the state needs no new
//     case here, it needs the rest of the suite to stay green.
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
    windowConfig.title = "hp material assignment test";
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

/// Counts how often the missing-material warning fires (the once-per-GUID rule).
class MissingMaterialCounter final : public hp::ILogSink {
public:
    void write(const hp::LogRecord& record) override {
        if (record.message.find("missing-material pattern") != std::string_view::npos) {
            ++count_;
        }
    }
    [[nodiscard]] int count() const { return count_.load(); }

private:
    std::atomic<int> count_{0};
};

/// A quad facing the camera with UVs, so the checkerboard has coordinates to
/// tile over. Double-sided, matching every other quad in this suite — which
/// faces exist is not the question these cases ask.
void writeQuadGltf(const std::filesystem::path& directory) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    // position(3) normal(3) uv(2) = 8 floats per vertex.
    const float vertices[] = {
        -4.0F, -4.0F, 3.0F, 0.0F, 0.0F, -1.0F, 0.0F, 1.0F,
         4.0F, -4.0F, 3.0F, 0.0F, 0.0F, -1.0F, 1.0F, 1.0F,
         4.0F,  4.0F, 3.0F, 0.0F, 0.0F, -1.0F, 1.0F, 0.0F,
        -4.0F,  4.0F, 3.0F, 0.0F, 0.0F, -1.0F, 0.0F, 0.0F,
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
      "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 },
      "indices": 3,
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
    { "buffer": 0, "byteOffset": 0,   "byteLength": 128, "byteStride": 32 },
    { "buffer": 0, "byteOffset": 128, "byteLength": 12 }
  ],
  "accessors": [
    { "bufferView": 0, "byteOffset": 0,  "componentType": 5126, "count": 4, "type": "VEC3",
      "min": [-4.0, -4.0, 3.0], "max": [4.0, 4.0, 3.0] },
    { "bufferView": 0, "byteOffset": 12, "componentType": 5126, "count": 4, "type": "VEC3" },
    { "bufferView": 0, "byteOffset": 24, "componentType": 5126, "count": 4, "type": "VEC2" },
    { "bufferView": 1, "byteOffset": 0,  "componentType": 5123, "count": 6, "type": "SCALAR" }
  ]
})";
    std::ofstream file(directory / "quad.gltf", std::ios::binary);
    file << json;
}

struct Frame {
    std::vector<std::uint8_t> pixels;
    hp::SceneViewStats stats;
    [[nodiscard]] bool ok() const { return !pixels.empty(); }
};

/// Renders the quad with the given per-surface override, and no light unless
/// asked — most of these cases *depend* on the dark.
///
/// `scale` is the quad entity's local scale — the determinant-rule case hands
/// in a mirror (T0152.5); everything else takes the default.
Frame renderWithMaterials(Device& device, hp::AssetPool& pool, const std::vector<hp::Guid>& slots,
                          int frames = 1, hp::float3 scale = hp::float3{1.0F, 1.0F, 1.0F}) {
    Frame frame;

    std::error_code ec;
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "hp-material-assignment";
    std::filesystem::remove_all(scratch, ec);
    std::filesystem::create_directories(scratch / "models", ec);
    writeQuadGltf(scratch / "models");

    hp::Vfs::shutdown();
    if (!hp::Vfs::init(nullptr) || !hp::Vfs::mount(scratch.string())) {
        return frame;
    }

    const hp::Guid meshGuid = hp::Guid::generate();
    auto mesh =
        hp::loadMesh(device.render->device(), device.render->context(), "models/quad.gltf");
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
    renderer.materials = slots;
    quad.add<hp::MeshRenderer>(renderer);
    quad.get<hp::Transform>().scale = scale;

    // Without this nothing has a world transform, and the frame comes back
    // pure clear colour with `submitted == 1` — the exact symptom the T0134
    // notes warn about, rediscovered while writing this test.
    scene.propagateTransforms();

    hp::SceneView view;
    if (!view.create(device.render->device(), device.render->context(), kSize, kSize)) {
        return frame;
    }
    // Blue, so "nothing drew" stays distinguishable from every material state.
    view.setClearColour(0.0F, 0.0F, 1.0F, 1.0F);

    for (int i = 0; i < frames; ++i) {
        if (view.render(device.render->context(), scene, pool, 0, &frame.stats) == nullptr) {
            return frame;
        }
    }
    if (frame.stats.submitted != 1) {
        return frame;
    }
    if (!view.readback(device.render->context(), frame.pixels)) {
        frame.pixels.clear();
    }
    return frame;
}

struct Rgb {
    int r = 0;
    int g = 0;
    int b = 0;
};

/// The mean of the middle half of the frame.
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

/// Counts centre pixels that are loudly magenta, and ones that are near black.
/// A checkerboard has plenty of both; a flat fill of either has only one.
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

} // namespace

TEST_CASE("a missing material renders unlit magenta checks, reported once") {
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    MissingMaterialCounter counter;
    hp::logAddSink(&counter);

    hp::AssetPool pool;
    // A GUID nothing has ever loaded: the `Missing` row of the table.
    // **Three frames**, because the second assertion here is that the warning
    // fires once per GUID, not once per frame — the draw path must be silent.
    const Frame frame = renderWithMaterials(device, pool, {hp::Guid::generate()}, /*frames=*/3);
    hp::logRemoveSink(&counter);
    REQUIRE(frame.ok());

    int magenta = 0;
    int black = 0;
    countChecks(frame.pixels, magenta, black);
    const Rgb centre = centreOf(frame.pixels);
    MESSAGE("centre (" << centre.r << ", " << centre.g << ", " << centre.b << "), magenta "
                       << magenta << ", black " << black);

    // **There is no light in this scene.** A shaded surface would be pitch
    // black, so bright magenta arriving at the target is the proof the
    // fallback is unlit — which is the half of the convention T0060 records
    // as the one that gets missed.
    const int centrePixels = (kSize / 2) * (kSize / 2);
    CHECK(magenta > centrePixels / 8);
    CHECK(black > centrePixels / 8);
    // Both check colours have green ~0; anything green leaked from lighting
    // or a texture factor would raise it.
    CHECK(centre.g < 30);

    // Reported on the transition, once per GUID — never per draw, never per
    // frame (T0141's 3,600-lines-a-minute trap).
    CHECK(counter.count() == 1);

    // **Leaving a mount up poisons the tests that run after this one.** A
    // later case's `Vfs::init` is a no-op, and PhysicsFS searches earlier
    // mounts first — so this case's `models/quad.gltf` would shadow the next
    // test's file of the same name. Measured: present_blit's offset quad
    // became this test's centred one, and its control REQUIRE caught it.
    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("an assigned material replaces the imported one, exactly") {
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    hp::AssetPool pool;
    const hp::Guid materialGuid = hp::Guid::generate();
    {
        auto material = std::make_shared<hp::Material>();
        material->baseColour = hp::float4{0.0F, 1.0F, 0.0F, 1.0F};
        // **Single-sided on purpose, and it is T0152's inverse probe**: the
        // material's default `doubleSided = false` puts this draw through
        // `CULL_MODE_BACK`, so the exact green below arrives only if the
        // camera-facing, correctly-wound face is a hardware *front* face --
        // the winding convention's Done-when, asserted where it cannot
        // regress silently.
        // Unlit, so the expected pixels are exact: base colour straight to the
        // target, no BRDF in the way. This is also what proves
        // `Material::unlit` reaches the pipeline as the unshaded permutation.
        material->unlit = true;
        pool.store<hp::Material>(materialGuid, material);
    }

    // The control first: the same scene with no override must come back
    // **black** — imported material, shaded, and there is no light. This is
    // what makes the green below attributable to the assignment rather than
    // to anything about the scene.
    {
        const Frame control = renderWithMaterials(device, pool, {});
        REQUIRE(control.ok());
        const Rgb c = centreOf(control.pixels);
        MESSAGE("imported control, no light: (" << c.r << ", " << c.g << ", " << c.b << ")");
        CHECK(c.r == 0);
        CHECK(c.g == 0);
        CHECK(c.b == 0);
    }

    const Frame frame = renderWithMaterials(device, pool, {materialGuid});
    REQUIRE(frame.ok());

    const Rgb centre = centreOf(frame.pixels);
    MESSAGE("centre (" << centre.r << ", " << centre.g << ", " << centre.b << ")");

    // Exact, not approximate. Unshaded writes (0, 1, 0) straight out; the
    // sRGB target stores 255 for 1.0 and 0 for 0.0. One count of drift means
    // something else touched the pixel.
    CHECK(centre.r == 0);
    CHECK(centre.g == 255);
    CHECK(centre.b == 0);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("a mirrored node keeps its single-sided faces -- the glTF determinant rule") {
    // **T0152.5's CPU-half probe.** The glTF spec says a negative-determinant
    // node transform flips effective winding (§instantiation), so a mirrored
    // single-sided mesh must cull FRONT instead of BACK or the faces that
    // exist are exactly the wrong ones. The scene is the assigned-green case
    // above with one change: the quad entity's scale is (-1, 1, 1). The quad
    // is symmetric, so the mirror moves no geometry -- the *only* thing it
    // changes is the screen-space winding. Before the determinant rule this
    // frame came back pure clear-colour blue: every triangle culled, which is
    // the original T0141.12 symptom reproduced by data instead of by a bug.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    hp::AssetPool pool;
    const hp::Guid materialGuid = hp::Guid::generate();
    {
        auto material = std::make_shared<hp::Material>();
        material->baseColour = hp::float4{0.0F, 1.0F, 0.0F, 1.0F};
        // Single-sided and unlit, exactly like the probe above: the exact
        // green arrives only if the surviving faces are the glTF front faces.
        material->unlit = true;
        pool.store<hp::Material>(materialGuid, material);
    }

    const Frame frame = renderWithMaterials(device, pool, {materialGuid}, /*frames=*/1,
                                            hp::float3{-1.0F, 1.0F, 1.0F});
    REQUIRE(frame.ok());

    const Rgb centre = centreOf(frame.pixels);
    MESSAGE("mirrored single-sided quad: (" << centre.r << ", " << centre.g << ", " << centre.b
                                            << ")");

    // Exact, for the same reason as the unmirrored probe -- and the failure
    // modes are distinct colours: culled-away is the blue clear, a magenta
    // fallback is a failed shader, and anything shaded would be black.
    CHECK(centre.r == 0);
    CHECK(centre.g == 255);
    CHECK(centre.b == 0);

    hp::Vfs::shutdown();
    tearDown(device);
}
