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

#include <algorithm>
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

/// Fraction of covered pixels that are the missing-material checkerboard's
/// loud magenta.
///
/// **The guard T0158's process failure demanded** (T0159.7): a failed or
/// missing shader renders the magenta checkerboard, and the checkerboard
/// *passes* both the coverage and the variation assertions — measured, twice,
/// by a magenta frame being reported as a working render. Mean RGB over the
/// covered pixels comes out near (127, 0, 127); per pixel, half the checks are
/// loud magenta. No shading outcome of this rock scene produces either, so a
/// small bound on this share is what makes "the shader actually ran"
/// assertable.
double magentaShareOfCovered(const std::vector<std::uint8_t>& rgba) {
    long long magenta = 0;
    long long covered = 0;
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        if (rgba[i] == 0 && rgba[i + 1] == 0 && rgba[i + 2] == 255) {
            continue;
        }
        ++covered;
        if (rgba[i] > 200 && rgba[i + 2] > 200 && rgba[i + 1] < 60) {
            ++magenta;
        }
    }
    return covered > 0 ? static_cast<double>(magenta) / static_cast<double>(covered) : 0.0;
}

/// Mean luminance over the covered pixels — what a frame-wide darkening moves.
double meanLumaOfCovered(const std::vector<std::uint8_t>& rgba) {
    double sum = 0.0;
    long long n = 0;
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        if (rgba[i] == 0 && rgba[i + 1] == 0 && rgba[i + 2] == 255) {
            continue;
        }
        sum += 0.2126 * rgba[i] + 0.7152 * rgba[i + 1] + 0.0722 * rgba[i + 2];
        ++n;
    }
    return n > 0 ? sum / static_cast<double>(n) : 0.0;
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
                             "textures/rock_height.png", "textures/rock_normal.png",
                             "shaders/rock_pom.slang", "materials/rock.hpmat",
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
    CHECK(loaded.entities == 4);
    // `RockCubeSpin` belongs to the gameplay module, which is not loaded here.
    // Preserved, not dropped, and not fatal (D23).
    CHECK(loaded.unknownComponents == 1);

    // Both lights must arrive from above and from the camera's side, and from
    // *opposite* horizontal sides. This is here because the sample authors
    // each rotation as four hand-written numbers in a file where a sign error
    // is invisible to read and obvious on screen.
    //
    // The fill exists because there is no ambient or environment lighting yet
    // (T0087): with one lamp and a normal map, a face angled away from it
    // renders solid black — measured, and the reason the scene has two.
    const hp::LightList lights = hp::gatherLights(scene);
    REQUIRE(lights.size() == 2);
    for (std::size_t i = 0; i < lights.size(); ++i) {
        CAPTURE(i);
        CHECK(lights[i].direction.y < 0.0F); // travelling down, therefore from above
        CHECK(lights[i].direction.z > 0.0F); // travelling away, from the camera's side
    }
    // Key from the +X side, fill from the -X side. Same sign and they stop
    // being a key and a fill and become one brighter lamp.
    CHECK(lights[0].direction.x * lights[1].direction.x < 0.0F);

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

    // **And it is not the checkerboard** (T0159.7). The variation bound above
    // does not exclude it — the checks are high-contrast, so a failed shader
    // sails through `variation > 4.0` — and that is not a hypothetical: a
    // magenta frame was reported as a working render twice on 2026-08-06,
    // from these very statistics. The checkerboard is ~50% loud magenta over
    // the covered pixels; real rock is none.
    const double magentaShare = magentaShareOfCovered(pixels);
    MESSAGE("magenta share of covered pixels: " << magentaShare);
    CHECK(magentaShare < 0.05);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("parallax self-shadowing darkens the frame, measured against itself with it off") {
    // **T0159.7 — the acceptance test for the opened contract.** The sample's
    // shadow march needs everything T0159 landed at once: `[mutating]` hooks
    // carrying the tangent frame from `surfaceCoordinates` to `surface()`, and
    // a light read from `g_Frame.Lights[]` under the amended D27. If either
    // regresses, the shadow term collapses to zero — which is *exactly* the
    // silent failure that opened the ticket — so this case renders the same
    // scene twice, with the march's strength at its committed 1.0 and forced
    // to 0.0, and requires a real difference.
    //
    // The off-switch is the VFS, not a second shader: the committed module is
    // patched textually (`kShadowStrength = 1.0` -> `0.0`) and Prepend-mounted
    // over the sample's copy — the same per-file override a patch or a
    // developer's working directory uses (D13). Everything else — scene,
    // lights, camera, textures, GUIDs — is byte-identical between the two
    // renders. A fresh pool and view per variant, because the pipeline cache
    // keys on the module's *path*, which does not change.
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

    const auto renderVariant = [&](bool shadowOff, float yaw, std::vector<std::uint8_t>& pixels,
                                   const char* frameName) -> bool {
        if (!mountContent(root)) {
            return false;
        }
        if (shadowOff) {
            // Patch the committed shader's strength constant and mount the
            // copy in front. The assert-on-replace rule: if the constant is
            // renamed, fail here with a message, not downstream with a
            // meaningless zero difference.
            const auto source = hp::Vfs::readText("shaders/rock_pom.slang");
            REQUIRE(source.has_value());
            const std::string needle = "kShadowStrength = 1.0";
            const std::size_t at = source->find(needle);
            REQUIRE_MESSAGE(at != std::string::npos,
                            "rock_pom.slang no longer declares kShadowStrength = 1.0; "
                            "update this test's patch alongside the shader");
            std::string patched = *source;
            patched.replace(at, needle.size(), "kShadowStrength = 0.0");

            std::error_code ec;
            const std::filesystem::path scratch =
                std::filesystem::temp_directory_path() / "hp-rockcube-shadow-off";
            std::filesystem::remove_all(scratch, ec);
            std::filesystem::create_directories(scratch / "shaders", ec);
            std::ofstream file(scratch / "shaders" / "rock_pom.slang", std::ios::binary);
            file << patched;
            file.close();
            if (!hp::Vfs::mount(scratch.string(), {}, hp::MountOrder::Prepend)) {
                return false;
            }
        }

        hp::AssetPool pool;
        for (const char* path : {"textures/rock_basecolour.png", "textures/rock_orm.png",
                                 "textures/rock_height.png", "textures/rock_normal.png",
                                 "shaders/rock_pom.slang", "materials/rock.hpmat",
                                 "models/cube.gltf"}) {
            const hp::ImportResult result =
                hp::importAsset(device.render->device(), device.render->context(), pool, path);
            if (!result.loaded || result.placeholder) {
                return false;
            }
        }

        const auto text = hp::Vfs::readText("scenes/rockcube.hpscene");
        if (!text.has_value()) {
            return false;
        }
        hp::Scene scene;
        if (hp::loadSceneFromString(scene, *text, "scenes/rockcube.hpscene").status !=
            hp::SceneLoadStatus::Ok) {
            return false;
        }
        // **Posed, not resting.** At the scene's resting yaw the key light
        // stands 15-48 degrees off every visible face, and this height map is
        // smooth — the p90 rise over 12 texels is 0.078 of the range — so
        // almost nothing occludes and the honest difference is ~0.02
        // luminance, indistinguishable from a broken term. The yaw below
        // turns the wide face until the key grazes it, which is the regime
        // self-shadowing exists for and the one a viewer actually notices on
        // the spinning cube.
        const auto cubeGuid = hp::Guid::parse("1570000000000103");
        if (!cubeGuid.has_value()) {
            return false;
        }
        const auto cube = scene.find(*cubeGuid);
        if (!cube.has_value()) {
            return false;
        }
        hp::Transform posed;
        posed.position = {0.0F, 0.0F, 5.0F};
        posed.rotation = hp::Quaternion::RotationFromAxisAngle(hp::float3{0.0F, 1.0F, 0.0F}, yaw);
        scene.setLocalTransform(*cube, posed);
        scene.propagateTransforms();

        hp::SceneView view;
        if (!view.create(device.render->device(), device.render->context(), kSize, kSize)) {
            return false;
        }
        view.setClearColour(kClear[0], kClear[1], kClear[2], kClear[3]);

        hp::SceneViewStats stats;
        if (view.render(device.render->context(), scene, pool, 0, &stats) == nullptr ||
            stats.submitted != 1) {
            return false;
        }
        if (!view.readback(device.render->context(), pixels)) {
            return false;
        }
        writePpm(frameName, pixels, kSize);
        return true;
    };

    // **Posed at yaw 0.45, where the sweep found the effect.** The key's
    // actual travel is (0.15, -0.62, 0.77) — computed from the scene's
    // quaternion, and nearly frontal to the camera — so self-shadowing from
    // this viewpoint concentrates on the face the key grazes, not across the
    // frame. At this yaw that face is visible and its light sits ~11 degrees
    // above its horizon; the measured effect is 72 pixels darkened by more
    // than 10 luminance (max drop 51.7) and a frame-mean difference of 0.045.
    // The assertions below are sized to that shape: a *population* of
    // strongly darkened pixels, not a frame-mean threshold that the small
    // footprint would drown.
    std::vector<std::uint8_t> withShadow;
    std::vector<std::uint8_t> withoutShadow;
    REQUIRE(renderVariant(/*shadowOff=*/false, 0.45F, withShadow, "rockcube_selfshadow_on"));
    REQUIRE(renderVariant(/*shadowOff=*/true, 0.45F, withoutShadow, "rockcube_selfshadow_off"));

    // **Neither frame is the checkerboard** — without this, one variant
    // failing to compile would register as an enormous "difference" and pass.
    // The exact trap this whole case exists to close.
    const double magentaOn = magentaShareOfCovered(withShadow);
    const double magentaOff = magentaShareOfCovered(withoutShadow);
    MESSAGE("magenta shares: on " << magentaOn << ", off " << magentaOff);
    REQUIRE(magentaOn < 0.05);
    REQUIRE(magentaOff < 0.05);

    // The shadow darkens texels; it moves none. Same silhouette, same count.
    const long long coveredOn = covered(withShadow);
    const long long coveredOff = covered(withoutShadow);
    MESSAGE("covered: on " << coveredOn << ", off " << coveredOff);
    CHECK(coveredOn == coveredOff);

    double difference = 0.0;
    long long compared = 0;
    long long darkened = 0;
    long long blackOn = 0;
    long long blackOff = 0;
    double maxDrop = 0.0;
    for (std::size_t i = 0; i + 3 < withShadow.size() && i + 3 < withoutShadow.size(); i += 4) {
        const bool clearOn =
            withShadow[i] == 0 && withShadow[i + 1] == 0 && withShadow[i + 2] == 255;
        const bool clearOff =
            withoutShadow[i] == 0 && withoutShadow[i + 1] == 0 && withoutShadow[i + 2] == 255;
        if (clearOn || clearOff) {
            continue;
        }
        const double lumaOn = 0.2126 * withShadow[i] + 0.7152 * withShadow[i + 1] +
                              0.0722 * withShadow[i + 2];
        const double lumaOff = 0.2126 * withoutShadow[i] + 0.7152 * withoutShadow[i + 1] +
                               0.0722 * withoutShadow[i + 2];
        difference += lumaOn > lumaOff ? lumaOn - lumaOff : lumaOff - lumaOn;
        if (lumaOff - lumaOn > 10.0) {
            ++darkened;
        }
        if (lumaOff - lumaOn > maxDrop) {
            maxDrop = lumaOff - lumaOn;
        }
        if (withShadow[i] < 8 && withShadow[i + 1] < 8 && withShadow[i + 2] < 8) {
            ++blackOn;
        }
        if (withoutShadow[i] < 8 && withoutShadow[i + 1] < 8 && withoutShadow[i + 2] < 8) {
            ++blackOff;
        }
        ++compared;
    }
    REQUIRE(compared > 0);
    difference /= static_cast<double>(compared);

    const double meanOn = meanLumaOfCovered(withShadow);
    const double meanOff = meanLumaOfCovered(withoutShadow);
    MESSAGE("mean luminance: with shadow " << meanOn << ", without " << meanOff
                                           << "; mean abs difference " << difference
                                           << "; darkened>10 " << darkened << "; max drop "
                                           << maxDrop << "; black on/off " << blackOn << "/"
                                           << blackOff);

    // Darker with the march on — a shadow that brightens is a sign error.
    CHECK(meanOn < meanOff);

    // **The population of shadowed pixels, which is what a zero term has none
    // of.** Measured 72 darkened beyond 10 luminance with a max drop of 51.7;
    // the bounds sit at half and well under that so texture tweaks do not
    // flake them. T0158's collapsed term — the bug this case exists to catch —
    // measures zero on both.
    CHECK(darkened > 30);
    CHECK(maxDrop > 20.0);

    // **Self-shadowing adds no black pixels.** The black speckle visible on
    // the cube under POM predates the shadow march — it is the documented
    // N-dot-L clamp with a normal map and no ambient (T0087, and the scene
    // file's own comment) — and this pins that the march does not add to it:
    // the count is bit-identical with the march on and off, measured at
    // 431/431 here (and 10000/10000 at yaw 0.9, where the artefact peaks).
    CHECK(blackOn == blackOff);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("the sample's shader parameter comes from its .hpmat, not from the shader") {
    // **T0160's acceptance test, on the first material shader ever authored
    // here.** `rock_pom.slang`'s reference plane was a `static const float`
    // with a comment saying it had to be, "because that vocabulary is the
    // engine's and this knob is this shader's". It is now a declared parameter
    // and `rock.hpmat` gives it 0.5.
    //
    // The proof is that **the material file alone changes the image**: the
    // shader bytes are byte-identical between the two renders, only the
    // `.hpmat` differs, and the frames differ. The off-switch is the VFS, the
    // same per-file override a patch uses (D13) — but overriding the *material*
    // rather than the shader, which is the whole difference this ticket makes.
    //
    // 1.0 is the engine's own behaviour (everything sunken, nothing above the
    // polygon plane) and 0.5 is what the sample ships, so the two frames are
    // the same scene with the relief sitting half a range apart.
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

    const auto renderWithPlane = [&](const char* plane, std::vector<std::uint8_t>& pixels,
                                     const char* frameName) -> bool {
        if (!mountContent(root)) {
            return false;
        }
        if (plane != nullptr) {
            // Patch the committed material's value and mount the copy in
            // front. The assert-on-replace rule: if the parameter is renamed,
            // fail here with a message rather than downstream with two
            // identical frames and no reason.
            const auto source = hp::Vfs::readText("materials/rock.hpmat");
            REQUIRE(source.has_value());
            const std::string needle = "value: 0.5";
            const std::size_t at = source->find(needle);
            REQUIRE_MESSAGE(at != std::string::npos,
                            "rock.hpmat no longer carries 'value: 0.5' for referencePlane; "
                            "update this test's patch alongside the material");
            std::string patched = *source;
            patched.replace(at, needle.size(), std::string("value: ") + plane);

            std::error_code ec;
            const std::filesystem::path scratch =
                std::filesystem::temp_directory_path() / "hp-rockcube-plane";
            std::filesystem::remove_all(scratch, ec);
            std::filesystem::create_directories(scratch / "materials", ec);
            std::ofstream file(scratch / "materials" / "rock.hpmat", std::ios::binary);
            file << patched;
            file.close();
            if (!hp::Vfs::mount(scratch.string(), {}, hp::MountOrder::Prepend)) {
                return false;
            }
        }

        hp::AssetPool pool;
        for (const char* path : {"textures/rock_basecolour.png", "textures/rock_orm.png",
                                 "textures/rock_height.png", "textures/rock_normal.png",
                                 "shaders/rock_pom.slang", "materials/rock.hpmat",
                                 "models/cube.gltf"}) {
            const hp::ImportResult result =
                hp::importAsset(device.render->device(), device.render->context(), pool, path);
            if (!result.loaded || result.placeholder) {
                return false;
            }
        }

        const auto text = hp::Vfs::readText("scenes/rockcube.hpscene");
        if (!text.has_value()) {
            return false;
        }
        hp::Scene scene;
        if (hp::loadSceneFromString(scene, *text, "scenes/rockcube.hpscene").status !=
            hp::SceneLoadStatus::Ok) {
            return false;
        }
        scene.propagateTransforms();

        hp::SceneView view;
        if (!view.create(device.render->device(), device.render->context(), kSize, kSize)) {
            return false;
        }
        view.setClearColour(kClear[0], kClear[1], kClear[2], kClear[3]);

        hp::SceneViewStats stats;
        if (view.render(device.render->context(), scene, pool, 0, &stats) == nullptr ||
            stats.submitted != 1) {
            return false;
        }
        if (!view.readback(device.render->context(), pixels)) {
            return false;
        }
        writePpm(frameName, pixels, kSize);
        return true;
    };

    std::vector<std::uint8_t> shipped;
    std::vector<std::uint8_t> sunken;
    REQUIRE(renderWithPlane(nullptr, shipped, "rockcube_plane_committed"));
    REQUIRE(renderWithPlane("1.0", sunken, "rockcube_plane_one"));

    // **Neither frame is the checkerboard.** Without this, a shader that
    // stopped compiling would register as an enormous "difference" and pass —
    // the trap that produced two false green results on 2026-08-06.
    const double magentaShipped = magentaShareOfCovered(shipped);
    const double magentaSunken = magentaShareOfCovered(sunken);
    MESSAGE("magenta shares: shipped " << magentaShipped << ", plane 1.0 " << magentaSunken);
    REQUIRE(magentaShipped < 0.05);
    REQUIRE(magentaSunken < 0.05);

    // The silhouette does not move: parallax displaces texels inside the face,
    // never the face. A different covered count would mean something other
    // than the parameter changed.
    const long long coveredShipped = covered(shipped);
    const long long coveredSunken = covered(sunken);
    MESSAGE("covered: shipped " << coveredShipped << ", plane 1.0 " << coveredSunken);
    CHECK(coveredShipped == coveredSunken);

    double difference = 0.0;
    long long compared = 0;
    long long moved = 0;
    for (std::size_t i = 0; i + 3 < shipped.size() && i + 3 < sunken.size(); i += 4) {
        const bool clearA = shipped[i] == 0 && shipped[i + 1] == 0 && shipped[i + 2] == 255;
        const bool clearB = sunken[i] == 0 && sunken[i + 1] == 0 && sunken[i + 2] == 255;
        if (clearA || clearB) {
            continue;
        }
        const double lumaA =
            0.2126 * shipped[i] + 0.7152 * shipped[i + 1] + 0.0722 * shipped[i + 2];
        const double lumaB = 0.2126 * sunken[i] + 0.7152 * sunken[i + 1] + 0.0722 * sunken[i + 2];
        const double delta = lumaA > lumaB ? lumaA - lumaB : lumaB - lumaA;
        difference += delta;
        if (delta > 10.0) {
            ++moved;
        }
        ++compared;
    }
    REQUIRE(compared > 0);
    difference /= static_cast<double>(compared);
    MESSAGE("reference plane 0.5 vs 1.0: mean abs difference " << difference << "; moved>10 "
                                                               << moved << " of " << compared);

    // A real, large population of moved texels. The bounds sit well under the
    // measured values so a texture tweak does not flake them; what they catch
    // is the parameter never arriving, which measures **zero** on both.
    CHECK(difference > 1.0);
    CHECK(moved > 1000);

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
                             "textures/rock_height.png", "textures/rock_normal.png",
                             "shaders/rock_pom.slang", "materials/rock.hpmat",
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

TEST_CASE("the cube's lighting is anchored to the world, not to the mesh") {
    // **The rotation test a still frame cannot make.**
    //
    // At yaw 0, 90, 180 and 270 degrees a cube presents an identical square to
    // the camera: the same world-space normals face the same way, only *which*
    // face — and therefore which UV island — is in front. So if shading is
    // world-anchored, the frame's brightness distribution must be near
    // identical at all four, and only the rock pattern differs. If normals were
    // left in object space, the lit side would swing with the mesh and one face
    // would appear to be the only one reacting to light.
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
                             "textures/rock_height.png", "textures/rock_normal.png",
                             "shaders/rock_pom.slang", "materials/rock.hpmat",
                             "models/cube.gltf"}) {
        (void)hp::importAsset(device.render->device(), device.render->context(), pool, path);
    }
    const auto text = hp::Vfs::readText("scenes/rockcube.hpscene");
    REQUIRE(text.has_value());
    hp::Scene scene;
    REQUIRE(hp::loadSceneFromString(scene, *text, "scene").status == hp::SceneLoadStatus::Ok);

    const auto cubeGuid = hp::Guid::parse("1570000000000103");
    REQUIRE(cubeGuid.has_value());
    const auto cube = scene.find(*cubeGuid);
    REQUIRE(cube.has_value());

    hp::SceneView view;
    REQUIRE(view.create(device.render->device(), device.render->context(), kSize, kSize));
    view.setClearColour(kClear[0], kClear[1], kClear[2], kClear[3]);

    // Mean luminance of the left and right halves of the covered pixels.
    const auto halves = [](const std::vector<std::uint8_t>& rgba) {
        double left = 0.0;
        double right = 0.0;
        long long leftN = 0;
        long long rightN = 0;
        for (int y = 0; y < kSize; ++y) {
            for (int x = 0; x < kSize; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * kSize + x) * 4;
                if (rgba[i] == 0 && rgba[i + 1] == 0 && rgba[i + 2] == 255) {
                    continue;
                }
                const double luma =
                    0.2126 * rgba[i] + 0.7152 * rgba[i + 1] + 0.0722 * rgba[i + 2];
                if (x < kSize / 2) {
                    left += luma;
                    ++leftN;
                } else {
                    right += luma;
                    ++rightN;
                }
            }
        }
        return std::pair<double, double>{leftN > 0 ? left / leftN : 0.0,
                                         rightN > 0 ? right / rightN : 0.0};
    };

    std::vector<double> means;
    for (int step = 0; step < 4; ++step) {
        const float yaw = static_cast<float>(step) * 3.14159265358979F / 2.0F;
        hp::Transform placement;
        placement.position = {0.0F, 0.0F, 5.0F};
        placement.rotation =
            hp::Quaternion::RotationFromAxisAngle(hp::float3{0.0F, 1.0F, 0.0F}, yaw);
        scene.setLocalTransform(*cube, placement);
        scene.propagateTransforms();

        std::vector<std::uint8_t> pixels;
        REQUIRE(view.render(device.render->context(), scene, pool, 0, nullptr) != nullptr);
        REQUIRE(view.readback(device.render->context(), pixels));
        writePpm("rockcube_yaw_" + std::to_string(step * 90), pixels, kSize);

        const auto [left, right] = halves(pixels);
        MESSAGE("yaw " << step * 90 << " deg: left " << left << ", right " << right
                       << ", right/left " << (left > 0.0 ? right / left : 0.0));
        means.push_back((left + right) / 2.0);
    }

    // **The four frames must agree.** Not approximately-in-spirit: the same
    // world normals face the same way at every 90-degree step, so the only
    // legitimate difference is which patch of rock is in front.
    //
    // The first cube failed this by a factor of five — 26 at one yaw and 140 at
    // the yaw opposite it — because its per-face tangent bases were not
    // consistently oriented, so the normal map tilted bumps toward the light on
    // one face and away on another. Every other assertion in this suite passed
    // while that was true.
    const double low = *std::min_element(means.begin(), means.end());
    const double high = *std::max_element(means.begin(), means.end());
    MESSAGE("mean luminance across the four yaws: " << low << " .. " << high);
    REQUIRE(low > 0.0);
    // 25% covers the genuine variation between four different patches of a
    // detailed rock texture. A basis inconsistency is an order of magnitude
    // larger than that, which is what makes the bound worth stating loosely.
    CHECK(high / low < 1.25);

    hp::Vfs::shutdown();
    tearDown(device);
}

