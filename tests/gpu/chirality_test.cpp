// The chirality probe: does DCC-authored asymmetric content render mirrored?
// (T0152.6, re-derived by T0165.)
//
// **This test pins display handedness forever, and it measured the mirror that
// T0165 then removed.** T0152's trace concluded that the engine displayed an
// unconverted right-handed world mirror-imaged; this probe observed it on
// hardware, the owner decided against it, and D33's amendment is the result.
// What the file has to do now is say that the mirror is *gone*, and say it in a
// way that could still fail.
//
// ## The measurement changed shape, and the reason is worth reading first
//
// **"World +X lands on screen right" is true under both conventions and is not
// the mirror.** The projection's `_11` is positive either way (T0165.1 pins
// exactly that: a right-handed projection is not a screen-space mirror), so +X
// is screen right whichever way the camera looks. The mirror was never in where
// +X lands; it was in the *pairing* of that with the camera's forward. A
// physical observer's right hand is `cross(forward, up)`: with `forward = +Z`
// that is **-X**, so screen-right and observer-right disagreed. With
// `forward = -Z` it is **+X**, and they agree.
//
// A rendered image alone cannot see a camera's forward vector. So the probe
// anchors to the one thing that is both authored *in the content* and visible
// *on screen*: **which side of the surface you are looking at.** The glyph is
// **single-sided**, wound and normalled the way a DCC tool exports a readable
// face — front normal +Z, `cross(v1-v0, v2-v0)` along +Z, glyph readable from
// +Z. Then:
//
//   * it renders at all **only** if the camera is on the glyph's authored front
//     side, which under a -Z camera means placing it at negative z; and
//   * its arms land on screen right **only** if that front face is displayed
//     unmirrored.
//
// Both together are the claim "a Blender artist's asset arrives as authored".
// Under the pre-T0165 convention the same asset placed in front of the camera
// showed its *back*, which `CULL_MODE_BACK` discards -- so the coverage floor
// below is a real discriminator and not a formality. (That single-sidedness is
// new here. The file used to be double-sided on the grounds that "facing is
// T0152's other probe"; that separation is what left the assertions unable to
// tell the two conventions apart, because both put +X on screen right.)
//
// ## The glyph
//
// An "F" — asymmetric under both a horizontal and a vertical mirror, which is
// the whole point: a quad cannot tell left from right. The stem sits at world
// x ∈ [-2, -1]; the two arms extend toward **world +X**. Every row of the glyph
// therefore shares its **world -X** edge, and the rows differ only in how far
// toward +X they reach. On screen that means: all green rows share one column
// edge (the stem side), and the arms extend away from it.
//
//   * shared edge at the low columns, arms extending right  → the authored
//     front face reads correctly: **unmirrored**
//   * shared edge at the high columns, arms extending left  → the front face
//     is displayed mirrored, and T0165 did not do what it claims
//
// The vertical half of the question rides along: the arms live at world
// y > 0, the stem-only rows mostly below, so the row histogram pins where
// world +Y lands. Readback row order follows the device's convention —
// measured in present_blit_test as row 0 = screen top on Vulkan, and D29
// makes Vulkan the only backend — so "row 0" reads as "top" here.
//
// The glyph is unlit: lighting is T0152's other probe, and this one asks only
// where a face lands and which side of it is showing.
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

/// The "F", three axis-aligned rectangles in the **z = -6** plane. The camera
/// is the default — at the origin, looking -Z, 60° vertical FOV — so the
/// visible half-extent at that depth is 6·tan(30°) ≈ 3.46 world units and the
/// glyph's ±3 fits with margin.
///
///   stem     x ∈ [-2, -1], y ∈ [-3, 3]
///   top arm  x ∈ [-1,  2], y ∈ [ 2, 3]
///   mid arm  x ∈ [-1,  1], y ∈ [ 0, 1]
///
/// **Authored as a readable face, not as "something in front of the camera".**
/// Normals are +Z and the winding is `{0, 1, 2, 0, 2, 3}` over BL, BR, TR, TL,
/// so `cross(v1 - v0, v2 - v0)` is +Z too: the glTF front face is the +Z side,
/// which is the side the glyph reads correctly from. The material is
/// single-sided, so the engine draws this only when the camera is on that
/// side — see the file header for why that is the discriminator.
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
            vertices.insert(vertices.end(), {c[0], c[1], -6.0F, 0.0F, 0.0F, 1.0F});
        }
        for (const std::uint16_t i : {0, 1, 2, 0, 2, 3}) {
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
    "doubleSided": false,
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
      "min": [-2.0, -3.0, -6.0], "max": [2.0, 3.0, -6.0] },
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

    // Unlit green so lighting is not part of the answer, and **single-sided**
    // so that "which side of the glyph am I looking at" is. `CULL_MODE_BACK`
    // plus `kFrontFaceCounterClockwise` is what turns the authored front face
    // into the only one that can produce a pixel.
    const hp::Guid materialGuid = hp::Guid::generate();
    {
        auto material = std::make_shared<hp::Material>();
        material->baseColour = hp::float4{0.0F, 1.0F, 0.0F, 1.0F};
        material->unlit = true;
        material->doubleSided = false;
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
    //
    // **This is half the verdict, not a formality.** The glyph is single-sided
    // and authored front-toward-+Z; a camera that saw its back would produce
    // zero green pixels, which is exactly what the pre-T0165 convention would
    // do with this asset in front of the lens. Zero here means the display
    // shows the wrong side of authored content, not that the render failed.
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
    // landed. Aligned low columns + varying high columns = stem on screen left,
    // arms extending right — and since the coverage floor above already proved
    // the camera is on the glyph's authored front side, that is the **F reading
    // correctly**: the asset arrives as authored.
    //
    // A display mirrored in X fails this in the opposite pattern (varying low,
    // aligned high), which is what keeps it a measurement rather than a
    // tautology. **These two lines did not change with T0165 and that is not a
    // gap** — the file header derives why: `_11` stays positive through a
    // handedness change, so +X is screen right either way. What changed is that
    // it is now the *front* face landing there.
    // -----------------------------------------------------------------------
    CHECK(lowColumnSpread < minWidth / 2 + 2);
    CHECK(highColumnSpread > (minWidth * 3) / 2);

    // -----------------------------------------------------------------------
    // **The vertical verdict.** The arms live at world y > 0 and the
    // stem-only rows average world y < 0, so wide rows sitting at *smaller*
    // readback rows means **world +Y lands on screen top** (row 0 = top on
    // Vulkan, per present_blit_test's measurement). Unchanged by T0165, and
    // for the same reason as the horizontal pair: `_22` does not move either.
    // -----------------------------------------------------------------------
    CHECK(wideMeanRow < narrowMeanRow);

    hp::Vfs::shutdown();
    tearDown(device);
}
