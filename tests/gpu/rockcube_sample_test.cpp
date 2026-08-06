// The rock cube sample, rendered (T0157).
//
// **The sample exists to be looked at, and this is what keeps it worth looking
// at.** A demo scene rots silently: a texture stops resolving, a material key is
// renamed, the scene schema moves, and nobody notices until they open the editor
// weeks later and cannot tell whether it ever worked. This renders the committed
// content — the same files the module mounts — and asserts what a person would
// otherwise have to check by eye.
//
// Three claims, and the third is the one that cannot be checked by eye at all:
//
//   1. **the content still loads**: five assets through the VFS by GUID, and a
//      hand-authored scene through `loadSceneFromString`;
//   2. **the rock is on the cube**: the frame has texture detail in it, not a
//      flat colour — which is what a lost texture or a broken material binding
//      looks like;
//   3. **the faces wind outward** (D33). From *inside* the cube, back-face
//      culling must remove every triangle and leave a clear frame. Inverted
//      winding renders an ordinary-looking cube from outside — the silhouette is
//      identical and only the shading differs — but from inside it is unmissable.
//      `tests/fast/rockcube_mesh_test.cpp` proves the same property on the
//      vertex data; this proves the whole chain honours it, rasteriser state
//      included.
//
// The scene is loaded **without** the gameplay module, so `RockCubeSpin` has no
// registered type here. That is deliberate: the load must report it as a
// preserved unknown component rather than failing, which is D23's guarantee and
// the case a developer hits whenever their gameplay module has not built.
//
// Bucket: gpu. Skips cleanly with no device or without the committed content.

#include <doctest/doctest.h>

#include <hp/Assets.hpp>
#include <hp/Camera.hpp>
#include <hp/Guid.hpp>
#include <hp/Light.hpp>
#include <hp/Log.hpp>
#include <hp/Material.hpp>
#include <hp/Render.hpp>
#include <hp/Scene.hpp>
#include <hp/SceneSerialize.hpp>
#include <hp/SceneView.hpp>
#include <hp/Vfs.hpp>
#include <hp/Window.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr int kSize = 512;

/// The clear colour, and nothing in the scene may produce it. Pure blue is what
/// the rest of the gpu suite uses for the same reason: "nothing drew" has to
/// stay distinguishable from every shading outcome.
constexpr float kClear[4] = {0.0F, 0.0F, 1.0F, 1.0F};

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
    windowConfig.title = "hp rock cube sample test";
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

/// Walks up to the repository root, identified by the sample's scene file.
std::filesystem::path findRepoRoot() {
    std::filesystem::path here = std::filesystem::current_path();
    for (int up = 0; up < 6; ++up) {
        if (std::filesystem::exists(here / "samples" / "rockcube" / "content" / "scenes" /
                                    "rockcube.hpscene")) {
            return here;
        }
        if (!here.has_parent_path()) {
            break;
        }
        here = here.parent_path();
    }
    return {};
}

std::filesystem::path writePpm(const std::string& name, const std::vector<std::uint8_t>& rgba,
                               int size) {
    std::error_code ec;
    const std::filesystem::path directory = std::filesystem::current_path() / "test-frames";
    std::filesystem::create_directories(directory, ec);
    const std::filesystem::path path = directory / (name + ".ppm");

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    file << "P6\n" << size << " " << size << "\n255\n";
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        file.put(static_cast<char>(rgba[i]));
        file.put(static_cast<char>(rgba[i + 1]));
        file.put(static_cast<char>(rgba[i + 2]));
    }
    return path;
}

/// Pixels that are not the clear colour.
long long covered(const std::vector<std::uint8_t>& rgba) {
    long long count = 0;
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        const bool clear = rgba[i] == 0 && rgba[i + 1] == 0 && rgba[i + 2] == 255;
        count += clear ? 0 : 1;
    }
    return count;
}

/// Mean absolute luminance deviation over the covered pixels.
///
/// The detail statistic the textured tests established: it proves sampling
/// happened. A flat magenta placeholder or a lost base colour collapses it.
double variationOfCovered(const std::vector<std::uint8_t>& rgba) {
    std::vector<double> luma;
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        if (rgba[i] == 0 && rgba[i + 1] == 0 && rgba[i + 2] == 255) {
            continue;
        }
        luma.push_back(0.2126 * rgba[i] + 0.7152 * rgba[i + 1] + 0.0722 * rgba[i + 2]);
    }
    if (luma.empty()) {
        return 0.0;
    }
    double mean = 0.0;
    for (const double value : luma) {
        mean += value;
    }
    mean /= static_cast<double>(luma.size());
    double deviation = 0.0;
    for (const double value : luma) {
        deviation += value > mean ? value - mean : mean - value;
    }
    return deviation / static_cast<double>(luma.size());
}

/// The sample's own content, mounted the way the sample mounts it.
///
/// Two mounts rather than one, and that is the point rather than a shortcut:
/// the `.hpmeta` files live beside the sample and the PNGs live in
/// `test_assets/derived`, and **directories merge across mounts**, so
/// `textures/rock_orm.png` and `textures/rock_orm.png.hpmeta` resolve from
/// different mounts into one directory. That is the same mechanism a patch or a
/// DLC pack uses (D13), exercised here for free.
bool mountContent(const std::filesystem::path& root) {
    hp::Vfs::shutdown();
    if (!hp::Vfs::init(nullptr)) {
        return false;
    }
    if (!hp::Vfs::mount((root / "samples" / "rockcube" / "content").string())) {
        return false;
    }
    return hp::Vfs::mount((root / "test_assets" / "derived").string(), "textures");
}

} // namespace

TEST_CASE("the rock cube sample renders its committed content") {
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }
    const std::filesystem::path root = findRepoRoot();
    if (root.empty()) {
        MESSAGE("samples/rockcube/content not found; skipping");
        tearDown(device);
        return;
    }
    MESSAGE("device: " << device.render->adapterDescription());

    REQUIRE(mountContent(root));

    hp::AssetPool pool;
    for (const char* path : {"textures/rock_basecolour.png", "textures/rock_orm.png",
                             "textures/rock_height.png", "materials/rock.hpmat",
                             "models/cube.gltf"}) {
        const hp::ImportResult result =
            hp::importAsset(device.render->device(), device.render->context(), pool, path);
        CAPTURE(path);
        CHECK(result.loaded);
        CHECK_FALSE(result.placeholder);
    }

    // The GUIDs the hand-written metafiles declare. Checked because the scene
    // file names them literally: if an identity were regenerated, every
    // reference in the scene would silently resolve to nothing and the cube
    // would render as the missing-material checkerboard.
    const auto meshGuid = hp::Guid::parse("15700000000000c0");
    const auto materialGuid = hp::Guid::parse("15700000000000a1");
    REQUIRE(meshGuid.has_value());
    REQUIRE(materialGuid.has_value());
    CHECK(pool.contains<hp::MeshAsset>(*meshGuid));
    CHECK(pool.contains<hp::Material>(*materialGuid));

    const auto text = hp::Vfs::readText("scenes/rockcube.hpscene");
    REQUIRE(text.has_value());

    hp::Scene scene;
    const hp::SceneLoadResult loaded =
        hp::loadSceneFromString(scene, *text, "scenes/rockcube.hpscene");
    REQUIRE(loaded.status == hp::SceneLoadStatus::Ok);
    CHECK(loaded.entities == 3);
    // `RockCubeSpin` belongs to the gameplay module, which is not loaded here.
    // Preserved, not dropped, and not fatal (D23).
    CHECK(loaded.unknownComponents == 1);

    // The light must arrive from above and from the camera's side. Same
    // assertion the textured-surface test makes about the same rake, and it is
    // here because the sample authors that rotation as four hand-written
    // numbers in a file where a sign error is invisible.
    const hp::LightList lights = hp::gatherLights(scene);
    REQUIRE(lights.size() == 1);
    CHECK(lights[0].direction.y < 0.0F); // travelling down, therefore from above
    CHECK(lights[0].direction.z > 0.0F); // travelling away, therefore from the camera's side

    hp::SceneView view;
    REQUIRE(view.create(device.render->device(), device.render->context(), kSize, kSize));
    view.setClearColour(kClear[0], kClear[1], kClear[2], kClear[3]);

    scene.propagateTransforms();

    std::vector<std::uint8_t> pixels;
    {
        hp::SceneViewStats stats;
        REQUIRE(view.render(device.render->context(), scene, pool, 0, &stats) != nullptr);
        CHECK(stats.hadCamera);
        CHECK(stats.submitted == 1);
        CHECK(stats.missingMesh == 0);
        REQUIRE(view.readback(device.render->context(), pixels));
    }
    const std::filesystem::path frame = writePpm("rockcube_sample", pixels, kSize);
    MESSAGE("wrote " << frame.string());

    const long long lit = covered(pixels);
    const double variation = variationOfCovered(pixels);
    const double share = static_cast<double>(lit) / (kSize * kSize);
    MESSAGE("cube covers " << share * 100.0 << "% of the frame, luminance variation " << variation);

    // **Framing.** A 2-metre cube 5 metres from a 60-degree lens, seen from
    // 1.5 metres above it, occupies roughly a sixth of the frame. The bounds
    // are wide because this is a composition check, not a pixel comparison:
    // what they catch is a cube that vanished, and a cube that fills the frame
    // the way the editor's demo quad did before somebody noticed.
    CHECK(share > 0.04);
    CHECK(share < 0.40);

    // **The rock is actually on it.** A missing base colour renders flat, a
    // missing material renders the checkerboard — and the checkerboard would
    // pass a coverage check happily.
    CHECK(variation > 4.0);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("the rock cube's faces cull from inside, so they wind outward") {
    // D33's whole chain in one assertion. From inside a closed, single-sided
    // mesh every triangle is back-facing, so a correct build draws **nothing**.
    // With the winding inverted the same viewpoint shows the cube's interior —
    // and from outside, which is the only view a person ever looks at, the two
    // are nearly indistinguishable.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }
    const std::filesystem::path root = findRepoRoot();
    if (root.empty()) {
        MESSAGE("samples/rockcube/content not found; skipping");
        tearDown(device);
        return;
    }
    REQUIRE(mountContent(root));

    hp::AssetPool pool;
    for (const char* path : {"textures/rock_basecolour.png", "textures/rock_orm.png",
                             "textures/rock_height.png", "materials/rock.hpmat",
                             "models/cube.gltf"}) {
        (void)hp::importAsset(device.render->device(), device.render->context(), pool, path);
    }

    // The material must be single-sided for this to mean anything. It is,
    // because it does not say otherwise and `doubleSided` defaults to false —
    // asserted rather than assumed, since a `doubleSided: true` added to the
    // `.hpmat` would turn this whole case into a tautology that always passes.
    const auto materialGuid = hp::Guid::parse("15700000000000a1");
    REQUIRE(materialGuid.has_value());
    const auto material = pool.get<hp::Material>(*materialGuid);
    REQUIRE(material != nullptr);
    REQUIRE_FALSE(material->doubleSided);

    const auto text = hp::Vfs::readText("scenes/rockcube.hpscene");
    REQUIRE(text.has_value());
    hp::Scene scene;
    REQUIRE(hp::loadSceneFromString(scene, *text, "scenes/rockcube.hpscene").status ==
            hp::SceneLoadStatus::Ok);

    // Move the camera to the cube's centre. The cube is at [0, 0, 5] and is
    // 2 metres across, so every face is a metre away — well beyond the 0.1
    // near plane, which would otherwise clip them and pass for the wrong
    // reason.
    const auto cameraGuid = hp::Guid::parse("1570000000000101");
    REQUIRE(cameraGuid.has_value());
    const auto camera = scene.find(*cameraGuid);
    REQUIRE(camera.has_value());
    hp::Transform inside;
    inside.position = {0.0F, 0.0F, 5.0F};
    scene.setLocalTransform(*camera, inside);
    scene.propagateTransforms();

    hp::SceneView view;
    REQUIRE(view.create(device.render->device(), device.render->context(), kSize, kSize));
    view.setClearColour(kClear[0], kClear[1], kClear[2], kClear[3]);

    std::vector<std::uint8_t> pixels;
    hp::SceneViewStats stats;
    REQUIRE(view.render(device.render->context(), scene, pool, 0, &stats) != nullptr);
    // The draw is still *submitted* — culling happens on the device, not in
    // the scene parse, so this must not be confused with "nothing was drawn".
    CHECK(stats.submitted == 1);
    REQUIRE(view.readback(device.render->context(), pixels));
    writePpm("rockcube_from_inside", pixels, kSize);

    CHECK(covered(pixels) == 0);

    hp::Vfs::shutdown();
    tearDown(device);
}
