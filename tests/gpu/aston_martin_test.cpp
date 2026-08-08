// T0167: the first asset chosen by somebody other than us, rendered and
// measured — `aston_martin.glb` (Aston Martin, Francesco Coldesina /
// topfrank2013, Sketchfab, CC-BY-4.0).
//
// Bucket: gpu, and **env-gated**: the asset is not in the repository (82.3
// MiB; the licence decision is 167.7's), so every case runs only when
// `HP_ASTON_GLB` names the owner-approved file — opted in explicitly, never
// discovered by scanning — and passes as a skip everywhere else, CI included.
// The file is copied into a scratch mount first: the import writes sidecars
// and an `.assets/` directory beside its source, and those belong in a
// project tree, not in the owner's download folder.
//
// **Frames land as PNGs under `test-frames/aston/`** — the render the owner
// judges by eye, beside the numbers that keep the eye honest. A screenshot
// alone has misled this project twice (a magenta checkerboard passed its
// coverage assertions), so every asserted frame here goes through the magenta
// and checkerboard guards, and the light is moved between frames because a
// static frame cannot distinguish shading from baked luck.
//
// What these captures are: the engine's own viewport path (`SceneView`, the
// same renderer the editor's viewport presents), offscreen at 1024². What
// they are not: the editor's GUI — the editor cannot show an arbitrary model
// without a project system (T0024), and hacking the sample to smuggle the car
// in would test the hack. Recorded on the ticket in as many words.

#include <doctest/doctest.h>

#include <hp/Assets.hpp>
#include <hp/Camera.hpp>
#include <hp/Import.hpp>
#include <hp/Light.hpp>
#include <hp/Log.hpp>
#include <hp/Material.hpp>
#include <hp/Render.hpp>
#include <hp/Scene.hpp>
#include <hp/SceneView.hpp>
#include <hp/Vfs.hpp>
#include <hp/Window.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr int kSize = 1024;

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
    windowConfig.title = "hp aston martin test";
    windowConfig.width = 320;
    windowConfig.height = 240;

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

/// Captures warnings, for the no-required-extension-warning assertion.
class CapturingSink final : public hp::ILogSink {
public:
    struct Entry {
        hp::LogLevel level;
        std::string message;
    };
    void write(const hp::LogRecord& record) override {
        entries.push_back(Entry{record.level, std::string(record.message)});
    }
    std::vector<Entry> entries;
};

// --- PNG writing (tangent_frame_test's writer: stored deflate, zlib) --------

void pushBe32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

std::uint32_t crc32Of(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

void pushChunk(std::vector<std::uint8_t>& out, const char* tag,
               const std::vector<std::uint8_t>& body) {
    pushBe32(out, static_cast<std::uint32_t>(body.size()));
    std::vector<std::uint8_t> crcInput;
    crcInput.insert(crcInput.end(), tag, tag + 4);
    crcInput.insert(crcInput.end(), body.begin(), body.end());
    out.insert(out.end(), crcInput.begin(), crcInput.end());
    pushBe32(out, crc32Of(crcInput.data(), crcInput.size()));
}

std::filesystem::path writePng(const std::string& name, const std::vector<std::uint8_t>& rgba,
                               int size) {
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(size) * (1U + static_cast<std::size_t>(size) * 4U));
    for (int y = 0; y < size; ++y) {
        raw.push_back(0);
        const auto* row = rgba.data() + static_cast<std::size_t>(y) * size * 4;
        raw.insert(raw.end(), row, row + static_cast<std::size_t>(size) * 4);
    }
    std::vector<std::uint8_t> zlib{0x78, 0x01};
    std::size_t offset = 0;
    while (offset < raw.size()) {
        const std::size_t chunk = std::min<std::size_t>(65535, raw.size() - offset);
        const bool last = offset + chunk >= raw.size();
        zlib.push_back(last ? 1 : 0);
        zlib.push_back(static_cast<std::uint8_t>(chunk & 0xFFU));
        zlib.push_back(static_cast<std::uint8_t>((chunk >> 8U) & 0xFFU));
        const auto inverse = static_cast<std::uint16_t>(~static_cast<std::uint16_t>(chunk));
        zlib.push_back(static_cast<std::uint8_t>(inverse & 0xFFU));
        zlib.push_back(static_cast<std::uint8_t>((inverse >> 8U) & 0xFFU));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset + chunk));
        offset += chunk;
    }
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::uint8_t byte : raw) {
        a = (a + byte) % 65521U;
        b = (b + a) % 65521U;
    }
    pushBe32(zlib, (b << 16U) | a);

    std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<std::uint8_t> ihdr;
    pushBe32(ihdr, static_cast<std::uint32_t>(size));
    pushBe32(ihdr, static_cast<std::uint32_t>(size));
    ihdr.push_back(8);
    ihdr.push_back(6); // RGBA
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    pushChunk(png, "IHDR", ihdr);
    pushChunk(png, "IDAT", zlib);
    pushChunk(png, "IEND", {});

    const std::filesystem::path directory =
        std::filesystem::current_path() / "test-frames" / "aston";
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    const std::filesystem::path path = directory / (name + ".png");
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(png.data()),
               static_cast<std::streamsize>(png.size()));
    return path;
}

// --- measurement -------------------------------------------------------------

constexpr float kClear[4] = {0.06F, 0.07F, 0.10F, 1.0F};

/// The clear colour as it actually lands in the readback -- **measured from an
/// empty frame, not derived**. The first version derived it (`kClear * 255`)
/// and every metric lied: the render path encodes the target, so the empty
/// frame reads (66, 72, 88) where the derivation said (15, 18, 25), and a
/// frame of pure background counted as 99% "coverage". The probe ladder then
/// pushed the camera to a distance where the whole car sat beyond the far
/// plane, and five identical background frames passed a coverage check.
std::uint8_t gClearRef[3] = {0, 0, 0};

bool isClear(const std::uint8_t* px) {
    const auto near = [](int a, int b) { return std::abs(a - b) < 5; };
    return near(px[0], gClearRef[0]) && near(px[1], gClearRef[1]) && near(px[2], gClearRef[2]);
}

double coverageOf(const std::vector<std::uint8_t>& rgba) {
    long long covered = 0;
    long long total = 0;
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        ++total;
        if (!isClear(rgba.data() + i)) {
            ++covered;
        }
    }
    return total > 0 ? static_cast<double>(covered) / static_cast<double>(total) : 0.0;
}

double magentaShareOfCovered(const std::vector<std::uint8_t>& rgba) {
    long long magenta = 0;
    long long covered = 0;
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        if (isClear(rgba.data() + i)) {
            continue;
        }
        ++covered;
        const int r = rgba[i];
        const int g = rgba[i + 1];
        const int b = rgba[i + 2];
        if (((r > 200 && b > 200) || (r > 90 && r < 165 && b > 90 && b < 165)) && g < 60) {
            ++magenta;
        }
    }
    return covered > 0 ? static_cast<double>(magenta) / static_cast<double>(covered) : 0.0;
}

double meanLumaOfCovered(const std::vector<std::uint8_t>& rgba) {
    double sum = 0.0;
    long long n = 0;
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        if (isClear(rgba.data() + i)) {
            continue;
        }
        sum += 0.2126 * rgba[i] + 0.7152 * rgba[i + 1] + 0.0722 * rgba[i + 2];
        ++n;
    }
    return n > 0 ? sum / static_cast<double>(n) : 0.0;
}

/// Mean absolute per-pixel luminance change between two frames, over pixels
/// covered in **either** (T0170.5).
///
/// **This replaced a ratio of whole-frame means, and the reason is the point
/// of the ticket.** Before image-based lighting existed, the only light on the
/// car was the two lamps, so swinging the key moved the frame's *average*
/// brightness by 15% and a mean ratio was a fine instrument. With an
/// environment the average barely moves — measured 112.891 against 112.517,
/// a ratio of 1.003 — because most of what lights the car now comes from a
/// sky that did not move. The frame still answered the light: a specular
/// highlight travelled across the bonnet and one flank went from lit to
/// shaded. A mean of means cannot see that and a per-pixel difference can, so
/// the weaker instrument was replaced rather than its threshold lowered.
double meanLumaDelta(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    if (a.size() != b.size()) {
        return 0.0;
    }
    const auto luma = [](const std::uint8_t* px) {
        return 0.2126 * px[0] + 0.7152 * px[1] + 0.0722 * px[2];
    };
    double sum = 0.0;
    long long n = 0;
    for (std::size_t i = 0; i + 3 < a.size(); i += 4) {
        if (isClear(a.data() + i) && isClear(b.data() + i)) {
            continue;
        }
        sum += std::abs(luma(a.data() + i) - luma(b.data() + i));
        ++n;
    }
    return n > 0 ? sum / static_cast<double>(n) : 0.0;
}

/// Standard deviation of covered-pixel luminance — a textured, shaded surface
/// has structure; a flat default-material blob does not.
double lumaSpreadOfCovered(const std::vector<std::uint8_t>& rgba) {
    const double mean = meanLumaOfCovered(rgba);
    double sum = 0.0;
    long long n = 0;
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        if (isClear(rgba.data() + i)) {
            continue;
        }
        const double luma = 0.2126 * rgba[i] + 0.7152 * rgba[i + 1] + 0.0722 * rgba[i + 2];
        sum += (luma - mean) * (luma - mean);
        ++n;
    }
    return n > 0 ? std::sqrt(sum / static_cast<double>(n)) : 0.0;
}

/// The covered-pixel bounding box, for the orientation measurements: rows are
/// screen-down, so "the car's top is up" is a statement about which rows the
/// roofline occupies.
struct Bounds {
    int minX = kSize;
    int maxX = -1;
    int minY = kSize;
    int maxY = -1;
};

Bounds boundsOfCovered(const std::vector<std::uint8_t>& rgba) {
    Bounds bounds;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) * kSize + x) * 4;
            if (!isClear(rgba.data() + at)) {
                bounds.minX = std::min(bounds.minX, x);
                bounds.maxX = std::max(bounds.maxX, x);
                bounds.minY = std::min(bounds.minY, y);
                bounds.maxY = std::max(bounds.maxY, y);
            }
        }
    }
    return bounds;
}

} // namespace

TEST_CASE("the Aston Martin renders, and the frames are on disk beside their numbers "
          "(T0167)") {
    const char* assetPath = std::getenv("HP_ASTON_GLB");
    if (assetPath == nullptr || !std::filesystem::exists(assetPath)) {
        MESSAGE("HP_ASTON_GLB not set (or not a file); skipping the car");
        return;
    }

    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }
    MESSAGE("device: " << device.render->adapterDescription());

    // ---- the asset, taken as it comes (167.1) ------------------------------
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "hp_aston_martin_test";
    std::error_code ec;
    std::filesystem::remove_all(scratch, ec);
    std::filesystem::create_directories(scratch / "models", ec);
    std::filesystem::copy_file(assetPath, scratch / "models" / "aston_martin.glb",
                               std::filesystem::copy_options::overwrite_existing, ec);
    REQUIRE_FALSE(ec);

    hp::Vfs::shutdown();
    REQUIRE(hp::Vfs::init(nullptr));
    REQUIRE(hp::Vfs::mount(scratch.string()));
    REQUIRE(hp::Vfs::setWriteDirectory(scratch.string()));

    CapturingSink sink;
    hp::logAddSink(&sink);

    // ---- import, and what the loader saw (167.2) ---------------------------
    hp::AssetPool pool;
    const hp::ImportResult imported = hp::importAsset(
        device.render->device(), device.render->context(), pool, "models/aston_martin.glb");
    REQUIRE(imported.loaded);
    const auto mesh = pool.get<hp::MeshAsset>(imported.guid);
    REQUIRE(mesh != nullptr);
    MESSAGE("loader saw: " << mesh->meshCount() << " meshes, " << mesh->materialCount()
                           << " materials, " << mesh->nodeCount() << " nodes");
    CHECK(mesh->meshCount() == 31);
    CHECK(mesh->materialCount() == 1);
    CHECK(mesh->nodeCount() == 33);

    // 167.9b, decided on T0168.6: the file *requires* spec-gloss, spec-gloss
    // is end-to-end now, so the expected observation is **no warning at all**.
    for (const auto& entry : sink.entries) {
        const bool requiredWarning =
            entry.level == hp::LogLevel::Warning &&
            entry.message.find("requires the glTF extension") != std::string::npos;
        CHECK_FALSE(requiredWarning);
    }

    // ---- the scene ---------------------------------------------------------
    hp::Scene scene;
    hp::Entity cameraEntity = scene.create("camera");
    {
        // The default lens clips at 1000 units and this scene is measured in
        // centimetres -- the car alone spans ~1014, so at any framing
        // distance most or all of it sat beyond the default far plane, and
        // the first run of this file rendered five frames of pure background
        // (see gClearRef above for why the assertions did not catch it
        // unaided). The lens is set for the scene's own scale.
        hp::Camera lens;
        lens.nearPlane = 5.0F;
        lens.farPlane = 50000.0F;
        cameraEntity.add<hp::Camera>(lens);
    }
    {
        // Raised and pitched a little downward, like a person looking at a
        // car. The height is in the model's own units — measured centimetres
        // (the container spans ~1014 × 426 × 270 with the ground at −10,
        // which is a ~10 m scene, not a 10 unit one; the framing ladder
        // below turns that measurement into a composition rather than
        // trusting it).
        hp::Transform eye;
        eye.position = hp::float3{0.0F, 320.0F, 0.0F};
        eye.rotation =
            hp::Quaternion::RotationFromAxisAngle(hp::float3{1.0F, 0.0F, 0.0F}, -0.16F);
        scene.setLocalTransform(cameraEntity, eye);
    }

    hp::Entity car = scene.create("car");
    hp::MeshRenderer renderer;
    renderer.mesh = imported.guid;
    car.add<hp::MeshRenderer>(renderer);

    hp::Entity keyEntity = scene.create("key");
    {
        hp::Light key;
        key.type = hp::LightType::Directional;
        key.colour = hp::float3{1.0F, 0.98F, 0.92F};
        key.intensity = 4.0F;
        keyEntity.add<hp::Light>(key);
    }
    hp::Entity fillEntity = scene.create("fill");
    {
        // The fill exists for the same reason the rock cube's does: there is
        // no environment lighting yet (T0087), so a face angled away from the
        // one lamp renders black — an absence, not a defect (167.6).
        hp::Light fill;
        fill.type = hp::LightType::Directional;
        fill.colour = hp::float3{0.6F, 0.65F, 0.8F};
        fill.intensity = 1.2F;
        fillEntity.add<hp::Light>(fill);
    }

    const auto aimLights = [&](float keyYaw) {
        hp::Transform keyPose;
        keyPose.rotation =
            hp::Quaternion::RotationFromAxisAngle(hp::float3{0.0F, 1.0F, 0.0F}, keyYaw) *
            hp::Quaternion::RotationFromAxisAngle(hp::float3{1.0F, 0.0F, 0.0F}, 0.7F);
        scene.setLocalTransform(keyEntity, keyPose);
        hp::Transform fillPose;
        fillPose.rotation =
            hp::Quaternion::RotationFromAxisAngle(hp::float3{0.0F, 1.0F, 0.0F},
                                                  keyYaw + 2.4F) *
            hp::Quaternion::RotationFromAxisAngle(hp::float3{1.0F, 0.0F, 0.0F}, 0.5F);
        scene.setLocalTransform(fillEntity, fillPose);
    };

    hp::SceneView view;
    REQUIRE(view.create(device.render->device(), device.render->context(), kSize, kSize));
    view.setClearColour(kClear[0], kClear[1], kClear[2], kClear[3]);

    // The measured clear reference: one frame with nothing to draw. The car
    // entity exists but references a mesh the pool does not hold yet at this
    // point in no case -- so instead the reference frame poses the car far
    // behind the lens, where the near plane discards it.
    {
        hp::Transform hidden;
        hidden.position = hp::float3{0.0F, 0.0F, 1000000.0F};
        scene.setLocalTransform(car, hidden);
        scene.propagateTransforms();
        hp::SceneViewStats stats;
        REQUIRE(view.render(device.render->context(), scene, pool, 0, &stats) != nullptr);
        std::vector<std::uint8_t> empty;
        REQUIRE(view.readback(device.render->context(), empty));
        gClearRef[0] = empty[0];
        gClearRef[1] = empty[1];
        gClearRef[2] = empty[2];
        MESSAGE("measured clear reference: (" << int(gClearRef[0]) << ", " << int(gClearRef[1])
                                              << ", " << int(gClearRef[2]) << ")");
    }

    const auto renderCar = [&](float carYaw, float distance, float height,
                               std::vector<std::uint8_t>& pixels) {
        hp::Transform pose;
        pose.position = hp::float3{0.0F, height, -distance};
        pose.rotation =
            hp::Quaternion::RotationFromAxisAngle(hp::float3{0.0F, 1.0F, 0.0F}, carYaw);
        scene.setLocalTransform(car, pose);
        scene.propagateTransforms();
        hp::SceneViewStats stats;
        if (view.render(device.render->context(), scene, pool, 0, &stats) == nullptr) {
            return false;
        }
        if (!stats.hadCamera || stats.submitted != 1) {
            return false;
        }
        return view.readback(device.render->context(), pixels);
    };

    // ---- auto-framing: the units question answered by measurement ----------
    // The container gives no unit declaration (glTF is metres by spec; what
    // the artist *meant* is 167.5's question). The camera is placed by
    // measuring coverage rather than by trusting either answer.
    aimLights(0.6F);
    float distance = 600.0F;
    double coverage = 0.0;
    std::vector<std::uint8_t> probe;
    for (const float candidate : {600.0F, 900.0F, 1300.0F, 1800.0F, 2600.0F, 3600.0F}) {
        if (!renderCar(0.6F, candidate, 0.0F, probe)) {
            continue;
        }
        const double c = coverageOf(probe);
        MESSAGE("framing probe: distance " << candidate << " covers " << c * 100.0 << "%");
        distance = candidate;
        coverage = c;
        if (c > 0.12 && c < 0.42) {
            break;
        }
    }
    REQUIRE(coverage > 0.02);

    // ---- the turntable: four yaws, and the key light moved (167.3) ---------
    struct Shot {
        const char* name;
        float yaw;
        float keyYaw;
    };
    // The yaws are measured against the model, not assumed: the car's nose
    // points down **−Z at yaw 0** — away from the camera — so a naive 0.6 yaw
    // labelled "front quarter" showed the rear deck, and the owner caught it
    // from the frame where the numbers could not (a luminance mean has no
    // idea which end of a car it is looking at). Every yaw below carries the
    // π that turns the nose toward the lens, and the frames were re-examined
    // by eye after the fix.
    // The front-facing shots take the −1.8 key as their base and 0.6 as the
    // "relit" swing: the 0.6 key favours the car's rear flank, and a frame
    // whose subject sits in its own shadow is a poor thing to hand an owner,
    // however honest its numbers. The pair still measures the same swing.
    const Shot shots[] = {
        {"front_quarter", 3.74F, -1.8F},
        {"side", 1.5708F, 0.6F},
        {"rear_quarter", 5.74F, 0.6F},
        {"top_front", 2.56F, -1.8F},
        {"front_quarter_relit", 3.74F, 0.6F}, // the same pose, the key swung ~137°
    };

    double firstLuma = -1.0;
    double relitLuma = -1.0;
    std::vector<std::uint8_t> firstPixels;
    std::vector<std::uint8_t> relitPixels;
    for (const Shot& shot : shots) {
        aimLights(shot.keyYaw);
        std::vector<std::uint8_t> pixels;
        REQUIRE(renderCar(shot.yaw, distance, 0.0F, pixels));

        const double cov = coverageOf(pixels);
        const double magenta = magentaShareOfCovered(pixels);
        const double luma = meanLumaOfCovered(pixels);
        const double spread = lumaSpreadOfCovered(pixels);
        const Bounds bounds = boundsOfCovered(pixels);
        const std::filesystem::path frame = writePng(shot.name, pixels, kSize);
        MESSAGE(shot.name << ": coverage " << cov * 100.0 << "%, magenta " << magenta
                          << ", mean luma " << luma << ", spread " << spread << ", bounds x["
                          << bounds.minX << "," << bounds.maxX << "] y[" << bounds.minY << ","
                          << bounds.maxY << "] -> " << frame.string());

        // The guards, on every asserted frame (167.4).
        CHECK(cov > 0.02);
        CHECK(magenta < 0.05);
        // A textured car has structure; the flat default-material blob the old
        // MR-misread would have produced does not.
        CHECK(spread > 10.0);

        if (std::string(shot.name) == "front_quarter") {
            firstLuma = luma;
            firstPixels = pixels;
        }
        if (std::string(shot.name) == "front_quarter_relit") {
            relitLuma = luma;
            relitPixels = pixels;
        }
    }

    // **The light moved and the surface answered** (167.4, re-instrumented on
    // T0170.5): the same pose under a swung key must shade measurably
    // differently, or the frame is a painting. Measured per pixel — see
    // `meanLumaDelta` for why the frame-wide mean stopped being able to say.
    REQUIRE(firstLuma >= 0.0);
    REQUIRE(relitLuma >= 0.0);
    REQUIRE_FALSE(firstPixels.empty());
    REQUIRE_FALSE(relitPixels.empty());
    const double ratio = firstLuma > relitLuma ? firstLuma / std::max(relitLuma, 1e-3)
                                               : relitLuma / std::max(firstLuma, 1e-3);
    const double delta = meanLumaDelta(firstPixels, relitPixels);
    MESSAGE("key swung ~137 degrees: mean luma " << firstLuma << " vs " << relitLuma
                                                 << " (ratio " << ratio
                                                 << "), mean per-pixel |dLuma| " << delta);
    CHECK(delta > 2.0);

    // **And the environment is actually reaching the car** (T0170.5). The
    // paint's colour lives in this asset's *specular* map — its diffuse is
    // navy-black — so before there was anything to reflect the car rendered
    // near-black whatever the lamps did: mean luma 10.4 across all five shots.
    // A number rather than a screenshot, because a screenshot was what let the
    // black car pass for a year.
    MESSAGE("mean luma with the environment: " << firstLuma << " (pre-T0170: ~10.4)");
    CHECK(firstLuma > 40.0);

    // ---- 167.9: the 1,316 TANGENT.w = -1 vertices, against the decline -----
    // The vertex path stays float3 by T0168.4's recorded decision; the frame
    // is derivative-built. If the mirrored shells shaded wrong the symptom
    // would be normal-map seams splitting the car's symmetric panels — a
    // visual check on the captures, plus the spread guard above; there is no
    // per-shell oracle in a production asset, and that is exactly why the
    // controlled two-shell case exists (tangent_frame_test).

    hp::logRemoveSink(&sink);
    view.release();
    hp::Vfs::shutdown();
    tearDown(device);
    std::filesystem::remove_all(scratch, ec);
}
