// The chirality probe: which screen side each world direction lands on (T0152.6).
//
// **This test pins display handedness forever, and it exists to settle a claim
// that was derived and never observed.** T0152's trace concluded that the
// engine displays an unconverted right-handed world mirror-imaged: with an
// identity camera looking +Z, world +X lands on screen *right*, while a
// physical observer facing +Z has +X on their *left*. That claim decides
// whether DCC-authored asymmetric content — text on a sign, a watch on a left
// wrist — renders flipped, and the owner's mirror decision (accept the
// D3D-native mirrored convention, or introduce exactly one compensating
// mirror à la Unity) is taken against what this test measures.
//
// The mesh is an "F" — asymmetric under both a horizontal and a vertical
// mirror, which is the whole point: a quad cannot tell left from right. The
// stem sits at world x ∈ [-2, -1]; the two arms extend toward **world +X**.
// Every row of the glyph therefore shares its **world -X** edge, and the rows
// differ only in how far toward +X they reach. On screen that means: all
// green rows share one column edge (the stem side), and the arms extend away
// from it — and *which side the shared edge lands on* is the measurement.
//
//   * shared edge at the low columns, arms extending right  → world +X is
//     screen right: the display is **mirrored** (what T0152 derived)
//   * shared edge at the high columns, arms extending left  → world +X is
//     screen left: the display is unmirrored and D33's derivation was wrong
//
// The vertical half of the question rides along: the arms live at world
// y > 0, the stem-only rows mostly below, so the row histogram pins where
// world +Y lands. Readback row order follows the device's convention —
// measured in present_blit_test as row 0 = screen top on Vulkan, and D29
// makes Vulkan the only backend — so "row 0" reads as "top" here.
//
// The glyph is double-sided and unlit, deliberately: facing and lighting are
// T0152's *other* probes, and this one asks only where positions land.
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

#include <algorithm>
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
    windowConfig.title = "hp chirality test";
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

/// The "F", three axis-aligned rectangles in the z = 6 plane. The camera is
/// the default — at the origin, looking +Z, 60° vertical FOV — so the visible
/// half-extent at that depth is 6·tan(30°) ≈ 3.46 world units and the glyph's
/// ±3 fits with margin.
///
///   stem     x ∈ [-2, -1], y ∈ [-3, 3]
///   top arm  x ∈ [-1,  2], y ∈ [ 2, 3]
///   mid arm  x ∈ [-1,  1], y ∈ [ 0, 1]
///
/// Wound consistently with the authored -Z normals per D33 — although the
/// material is double-sided, so nothing here depends on it.
void writeGlyphGltf(const std::filesystem::path& directory) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    struct Rect {
        float x0, y0, x1, y1;
    };
    const Rect rects[] = {
        {-2.0F, -3.0F, -1.0F, 3.0F}, // stem
        {-1.0F, 2.0F, 2.0F, 3.0F},   // top arm
        {-1.0F, 0.0F, 1.0F, 1.0F},   // mid arm
    };

    // position(3) + normal(3) = 6 floats per vertex; BL, BR, TR, TL per rect.
    std::vector<float> vertices;
    std::vector<std::uint16_t> indices;
    for (const Rect& r : rects) {
        const auto base = static_cast<std::uint16_t>(vertices.size() / 6);
        const float corners[4][2] = {{r.x0, r.y0}, {r.x1, r.y0}, {r.x1, r.y1}, {r.x0, r.y1}};
        for (const auto& c : corners) {
            vertices.insert(vertices.end(), {c[0], c[1], 6.0F, 0.0F, 0.0F, -1.0F});
        }
        for (const std::uint16_t i : {0, 2, 1, 0, 3, 2}) {
            indices.push_back(static_cast<std::uint16_t>(base + i));
        }
    }

    std::vector<unsigned char> bin;
    const auto* vb = reinterpret_cast<const unsigned char*>(vertices.data());
    bin.insert(bin.end(), vb, vb + vertices.size() * sizeof(float));
    const std::size_t indexOffset = bin.size();
    const auto* ib = reinterpret_cast<const unsigned char*>(indices.data());
    bin.insert(bin.end(), ib, ib + indices.size() * sizeof(std::uint16_t));
    while (bin.size() % 4 != 0) {
        bin.push_back(0);
    }
    {
        std::ofstream file(directory / "glyph.bin", std::ios::binary);
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
  "buffers": [ { "uri": "glyph.bin", "byteLength": )" +
                             std::to_string(bin.size()) + R"( } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": )" +
                             std::to_string(indexOffset) + R"(, "byteStride": 24 },
    { "buffer": 0, "byteOffset": )" +
                             std::to_string(indexOffset) + R"(, "byteLength": )" +
                             std::to_string(indices.size() * sizeof(std::uint16_t)) + R"( }
  ],
  "accessors": [
    { "bufferView": 0, "byteOffset": 0,  "componentType": 5126, "count": 12, "type": "VEC3",
      "min": [-2.0, -3.0, 6.0], "max": [2.0, 3.0, 6.0] },
    { "bufferView": 0, "byteOffset": 12, "componentType": 5126, "count": 12, "type": "VEC3" },
    { "bufferView": 1, "byteOffset": 0,  "componentType": 5123, "count": 18, "type": "SCALAR" }
  ]
})";
    std::ofstream file(directory / "glyph.gltf", std::ios::binary);
    file << json;
}

/// One row of the readback: how many probe-green pixels, and their column span.
struct RowSpan {
    int count = 0;
    int minX = kSize;
    int maxX = -1;
};

} // namespace

TEST_CASE("the chirality probe: an F pins where each world direction lands on screen") {
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    std::error_code ec;
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "hp-chirality";
    std::filesystem::remove_all(scratch, ec);
    std::filesystem::create_directories(scratch / "models", ec);
    writeGlyphGltf(scratch / "models");

    hp::Vfs::shutdown();
    REQUIRE(hp::Vfs::init(nullptr));
    REQUIRE(hp::Vfs::mount(scratch.string()));

    hp::AssetPool pool;
    const hp::Guid meshGuid = hp::Guid::generate();
    {
        auto mesh = hp::loadMesh(device.render->device(), device.render->context(),
                                 "models/glyph.gltf");
        REQUIRE(mesh);
        REQUIRE(mesh->valid());
        pool.store<hp::MeshAsset>(meshGuid, mesh);
    }

    // Unlit green, double-sided: the probe asks where positions land, so
    // neither lighting nor facing is allowed to be part of the answer.
    const hp::Guid materialGuid = hp::Guid::generate();
    {
        auto material = std::make_shared<hp::Material>();
        material->baseColour = hp::float4{0.0F, 1.0F, 0.0F, 1.0F};
        material->unlit = true;
        material->doubleSided = true;
        pool.store<hp::Material>(materialGuid, material);
    }

    hp::Scene scene;
    hp::Entity cameraEntity = scene.create("camera");
    cameraEntity.add<hp::Camera>(hp::Camera{});

    hp::Entity glyph = scene.create("glyph");
    hp::MeshRenderer renderer;
    renderer.mesh = meshGuid;
    renderer.materials = {materialGuid};
    glyph.add<hp::MeshRenderer>(renderer);
    scene.propagateTransforms();

    hp::SceneView view;
    REQUIRE(view.create(device.render->device(), device.render->context(), kSize, kSize));
    // Black clear: nothing here is dark, so the glyph is the only green.
    view.setClearColour(0.0F, 0.0F, 0.0F, 1.0F);

    hp::SceneViewStats stats;
    REQUIRE(view.render(device.render->context(), scene, pool, 0, &stats) != nullptr);
    CHECK(stats.submitted == 1);

    std::vector<std::uint8_t> pixels;
    REQUIRE(view.readback(device.render->context(), pixels));

    // ---------------------------------------------------------------------
    // The histogram. A pixel is the glyph's iff it is loudly green — which is
    // also the magenta guard: a failed shader's (127, 0, 127) matches nothing
    // below, and the coverage REQUIRE fails loudly instead of silently.
    // ---------------------------------------------------------------------
    std::vector<RowSpan> rows(kSize);
    long long total = 0;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * kSize + x) * 4;
            if (pixels[i + 1] > 200 && pixels[i] < 60 && pixels[i + 2] < 60) {
                ++rows[y].count;
                rows[y].minX = std::min(rows[y].minX, x);
                rows[y].maxX = std::max(rows[y].maxX, x);
                ++total;
            }
        }
    }
    // The glyph covers 11 world units² at ≈18 px per unit ≈ 3600 px; half
    // that is a generous floor that still fails for any partial render.
    REQUIRE(total > 1500);

    // Rows that clearly hold glyph pixels (a few-pixel floor drops
    // antialiased boundary rows whose blended pixels fail the green test).
    int narrowRows = 0;
    int wideRows = 0;
    long long narrowRowSum = 0;
    long long wideRowSum = 0;
    int minWidth = kSize;
    int sharedEdgeSpreadLo = kSize;
    int sharedEdgeSpreadHi = -1;
    int extentLo = kSize;
    int extentHi = -1;
    for (int y = 0; y < kSize; ++y) {
        if (rows[y].count < 4) {
            continue;
        }
        minWidth = std::min(minWidth, rows[y].maxX - rows[y].minX + 1);
    }
    REQUIRE(minWidth < kSize);
    for (int y = 0; y < kSize; ++y) {
        if (rows[y].count < 4) {
            continue;
        }
        const int width = rows[y].maxX - rows[y].minX + 1;
        // The stem is 1 world unit wide; the narrowest arm row spans 3
        // (stem + mid arm). 1.8× the narrowest row splits them with margin.
        if (width > (minWidth * 9) / 5) {
            ++wideRows;
            wideRowSum += y;
        } else {
            ++narrowRows;
            narrowRowSum += y;
        }
        sharedEdgeSpreadLo = std::min(sharedEdgeSpreadLo, rows[y].minX);
        sharedEdgeSpreadHi = std::max(sharedEdgeSpreadHi, rows[y].minX);
        extentLo = std::min(extentLo, rows[y].maxX);
        extentHi = std::max(extentHi, rows[y].maxX);
    }
    REQUIRE(narrowRows > 0);
    REQUIRE(wideRows > 0);

    const int lowColumnSpread = sharedEdgeSpreadHi - sharedEdgeSpreadLo;
    const int highColumnSpread = extentHi - extentLo;
    const long long narrowMeanRow = narrowRowSum / narrowRows;
    const long long wideMeanRow = wideRowSum / wideRows;
    MESSAGE("glyph pixels " << total << ", stem width " << minWidth << " px");
    MESSAGE("low-column (min-x) spread across rows: " << lowColumnSpread
                                                      << " px; high-column (max-x) spread: "
                                                      << highColumnSpread << " px");
    MESSAGE("mean readback row: narrow (stem-only) " << narrowMeanRow << ", wide (arms) "
                                                     << wideMeanRow);

    // -----------------------------------------------------------------------
    // **The horizontal verdict.** Every glyph row shares its world -X edge, so
    // whichever *screen* side all the rows' edges align on is where world -X
    // landed. Aligned low columns + varying high columns = stem on screen
    // left, arms extending right = **world +X lands on screen right** — the
    // mirrored display T0152 derived. If the display were unmirrored these
    // two assertions would fail in the opposite pattern (varying low, aligned
    // high), which is what makes this a measurement and not a tautology.
    // -----------------------------------------------------------------------
    CHECK(lowColumnSpread < minWidth / 2 + 2);
    CHECK(highColumnSpread > (minWidth * 3) / 2);

    // -----------------------------------------------------------------------
    // **The vertical verdict.** The arms live at world y > 0 and the
    // stem-only rows average world y < 0, so wide rows sitting at *smaller*
    // readback rows means **world +Y lands on screen top** (row 0 = top on
    // Vulkan, per present_blit_test's measurement). Unmirrored vertically —
    // the mirror is in X, once, exactly as D33 records.
    // -----------------------------------------------------------------------
    CHECK(wideMeanRow < narrowMeanRow);

    hp::Vfs::shutdown();
    tearDown(device);
}
