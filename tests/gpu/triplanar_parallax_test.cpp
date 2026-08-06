// Parallax under triplanar: the intersection T0141.7 and T0141.8 never wrote
// (T0156).
//
// **The claim under test**: a triplanar material with a height map marches
// each of its three world-axis projections through the height field, so
// relief reads as carved on a mesh that has no UVs at all — the case where
// UV-mapped parallax has nothing to displace and used to render flat by
// design.
//
// The assertions are differential in 141.7's shape, because that shape earned
// its keep there:
//
//   * no height texture vs `heightScale = 0` must agree — a zero scale
//     marches nowhere, per projection exactly as per UV;
//   * a working scale must move a measurable share of the frame.
//
// Materials are **unlit**, so every differing pixel is a displaced texel and
// not a lighting response. All quads are authored in *world* orientation with
// winding derived from the authored normal (right-hand rule, per
// WindingConvention/D33) rather than rotated by transforms, so each case's
// axis-weight configuration is exact and readable.
//
// Three cases:
//   1. the oblique UV-less quad — the direct displacement claim (156.1/156.3);
//   2. a terrain-shaped heightfield — hills sweep the normal through one-,
//      two- and three-axis regions continuously, which is where a blend-seam
//      or an early-out contour would show (156.4/156.7);
//   3. cost per weight configuration — one, two and three live axes, height
//      on and off, wall-clock per frame (156.2). Numbers, not assertions:
//      the measurement belongs in the ticket and a timing CHECK is a flake.
//
// Bucket: gpu. Skips cleanly with no device or without the committed textures.

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

#include <chrono>
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
    windowConfig.title = "hp triplanar parallax test";
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

std::filesystem::path findTextureDir() {
    std::filesystem::path here = std::filesystem::current_path();
    for (int up = 0; up < 6; ++up) {
        const std::filesystem::path candidate = here / "test_assets" / "derived";
        if (std::filesystem::exists(candidate / "rock_height.png")) {
            return candidate;
        }
        if (!here.has_parent_path()) {
            break;
        }
        here = here.parent_path();
    }
    return {};
}

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec3 normalize(Vec3 v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return {v.x / len, v.y / len, v.z / len};
}

void appendBytes(std::vector<unsigned char>& out, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    out.insert(out.end(), bytes, bytes + size);
}

std::string gltfHeader() {
    return R"({
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
    "pbrMetallicRoughness": { "metallicFactor": 0.0, "roughnessFactor": 1.0 }
  } ],
)";
}

void writeMeshGltf(const std::filesystem::path& directory, const std::vector<float>& vertices,
                   const std::vector<std::uint16_t>& indices, const Vec3& posMin,
                   const Vec3& posMax) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    std::vector<unsigned char> bin;
    appendBytes(bin, vertices.data(), vertices.size() * sizeof(float));
    const std::size_t indexOffset = bin.size();
    appendBytes(bin, indices.data(), indices.size() * sizeof(std::uint16_t));
    while (bin.size() % 4 != 0) {
        bin.push_back(0);
    }
    {
        std::ofstream file(directory / "mesh.bin", std::ios::binary);
        file.write(reinterpret_cast<const char*>(bin.data()),
                   static_cast<std::streamsize>(bin.size()));
    }

    const std::size_t vertexCount = vertices.size() / 6;
    std::string json = gltfHeader();
    json += R"(  "buffers": [ { "uri": "mesh.bin", "byteLength": )" + std::to_string(bin.size()) +
            R"( } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": )" +
            std::to_string(vertexCount * 24) + R"(, "byteStride": 24 },
    { "buffer": 0, "byteOffset": )" +
            std::to_string(indexOffset) + R"(, "byteLength": )" +
            std::to_string(indices.size() * 2) + R"( }
  ],
  "accessors": [
    { "bufferView": 0, "byteOffset": 0,  "componentType": 5126, "count": )" +
            std::to_string(vertexCount) + R"(, "type": "VEC3",
      "min": [)" +
            std::to_string(posMin.x) + ", " + std::to_string(posMin.y) + ", " +
            std::to_string(posMin.z) + R"(], "max": [)" + std::to_string(posMax.x) + ", " +
            std::to_string(posMax.y) + ", " + std::to_string(posMax.z) + R"(] },
    { "bufferView": 0, "byteOffset": 12, "componentType": 5126, "count": )" +
            std::to_string(vertexCount) + R"(, "type": "VEC3" },
    { "bufferView": 1, "byteOffset": 0,  "componentType": 5123, "count": )" +
            std::to_string(indices.size()) + R"(, "type": "SCALAR" }
  ]
})";
    std::ofstream file(directory / "mesh.gltf", std::ios::binary);
    file << json;
}

/// A UV-less quad authored in world space: centre, unit normal toward the
/// camera side, half-extent. The tangent frame comes from the world up
/// vector, and the winding follows the authored normal by the right-hand
/// rule (WindingConvention, D33) — front faces are the +normal side.
void writeOrientedQuadGltf(const std::filesystem::path& directory, Vec3 centre, Vec3 normal,
                           float half) {
    const Vec3 n = normalize(normal);
    const Vec3 t = normalize(cross(Vec3{0.0F, 1.0F, 0.0F}, n));
    const Vec3 b = cross(n, t);

    std::vector<float> vertices;
    const auto push = [&](float st, float sb) {
        vertices.push_back(centre.x + (t.x * st + b.x * sb) * half);
        vertices.push_back(centre.y + (t.y * st + b.y * sb) * half);
        vertices.push_back(centre.z + (t.z * st + b.z * sb) * half);
        vertices.push_back(n.x);
        vertices.push_back(n.y);
        vertices.push_back(n.z);
    };
    push(-1.0F, -1.0F);
    push(1.0F, -1.0F);
    push(1.0F, 1.0F);
    push(-1.0F, 1.0F);

    // {0,1,2, 0,2,3} winds CCW around +n for the corner order above —
    // cross(v1 - v0, v2 - v0) = +n, so hardware front face equals the
    // authored normal's side.
    const std::vector<std::uint16_t> indices = {0, 1, 2, 0, 2, 3};

    Vec3 posMin{1e9F, 1e9F, 1e9F};
    Vec3 posMax{-1e9F, -1e9F, -1e9F};
    for (std::size_t i = 0; i < vertices.size(); i += 6) {
        posMin.x = std::min(posMin.x, vertices[i]);
        posMin.y = std::min(posMin.y, vertices[i + 1]);
        posMin.z = std::min(posMin.z, vertices[i + 2]);
        posMax.x = std::max(posMax.x, vertices[i]);
        posMax.y = std::max(posMax.y, vertices[i + 1]);
        posMax.z = std::max(posMax.z, vertices[i + 2]);
    }
    writeMeshGltf(directory, vertices, indices, posMin, posMax);
}

/// A UV-less heightfield: a grid in x/z with two gaussian hills, normals from
/// the analytic gradient. The camera at the origin looks down +Z across it,
/// so flat ground is one live axis (Y), hillsides are two, and the compound
/// slopes facing the camera diagonally are three — the whole weight space in
/// one frame, swept continuously.
void writeTerrainGltf(const std::filesystem::path& directory) {
    constexpr int kGrid = 33;
    constexpr float kX0 = -6.0F;
    constexpr float kX1 = 6.0F;
    constexpr float kZ0 = 4.0F;
    constexpr float kZ1 = 16.0F;
    constexpr float kBase = -3.0F;

    struct Hill {
        float amplitude;
        float cx;
        float cz;
        float sigma2;
    };
    // Peak slopes ~50 degrees: steep enough that the two- and three-axis
    // blend bands genuinely appear, which is what this mesh exists for.
    const Hill hills[] = {{3.0F, 1.5F, 9.5F, 2.0F}, {2.2F, -2.5F, 12.0F, 3.0F}};

    const auto height = [&](float x, float z) {
        float y = kBase;
        for (const Hill& hill : hills) {
            const float dx = x - hill.cx;
            const float dz = z - hill.cz;
            y += hill.amplitude * std::exp(-(dx * dx + dz * dz) / (2.0F * hill.sigma2));
        }
        return y;
    };
    const auto gradient = [&](float x, float z) {
        float gx = 0.0F;
        float gz = 0.0F;
        for (const Hill& hill : hills) {
            const float dx = x - hill.cx;
            const float dz = z - hill.cz;
            const float h = hill.amplitude * std::exp(-(dx * dx + dz * dz) / (2.0F * hill.sigma2));
            gx += -dx / hill.sigma2 * h;
            gz += -dz / hill.sigma2 * h;
        }
        return Vec3{gx, 0.0F, gz};
    };

    std::vector<float> vertices;
    vertices.reserve(static_cast<std::size_t>(kGrid) * kGrid * 6);
    Vec3 posMin{1e9F, 1e9F, 1e9F};
    Vec3 posMax{-1e9F, -1e9F, -1e9F};
    for (int j = 0; j < kGrid; ++j) {
        for (int i = 0; i < kGrid; ++i) {
            const float x = kX0 + (kX1 - kX0) * static_cast<float>(i) / (kGrid - 1);
            const float z = kZ0 + (kZ1 - kZ0) * static_cast<float>(j) / (kGrid - 1);
            const float y = height(x, z);
            const Vec3 g = gradient(x, z);
            const Vec3 n = normalize(Vec3{-g.x, 1.0F, -g.z});
            vertices.insert(vertices.end(), {x, y, z, n.x, n.y, n.z});
            posMin.x = std::min(posMin.x, x);
            posMin.y = std::min(posMin.y, y);
            posMin.z = std::min(posMin.z, z);
            posMax.x = std::max(posMax.x, x);
            posMax.y = std::max(posMax.y, y);
            posMax.z = std::max(posMax.z, z);
        }
    }

    std::vector<std::uint16_t> indices;
    indices.reserve(static_cast<std::size_t>(kGrid - 1) * (kGrid - 1) * 6);
    for (int j = 0; j < kGrid - 1; ++j) {
        for (int i = 0; i < kGrid - 1; ++i) {
            const auto v00 = static_cast<std::uint16_t>(j * kGrid + i);
            const auto v10 = static_cast<std::uint16_t>(j * kGrid + i + 1);
            const auto v01 = static_cast<std::uint16_t>((j + 1) * kGrid + i);
            const auto v11 = static_cast<std::uint16_t>((j + 1) * kGrid + i + 1);
            // Wound so the front faces point +Y (up), per the right-hand rule
            // — cross(v01 - v00, v10 - v00) = +Y for this grid layout.
            indices.insert(indices.end(), {v00, v01, v10});
            indices.insert(indices.end(), {v10, v01, v11});
        }
    }

    writeMeshGltf(directory, vertices, indices, posMin, posMax);
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

/// Mean absolute per-channel difference between two frames.
double meanDifference(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    if (a.size() != b.size() || a.empty()) {
        return -1.0;
    }
    double total = 0.0;
    long long counted = 0;
    for (std::size_t i = 0; i + 3 < a.size(); i += 4) {
        total += std::abs(int(a[i]) - int(b[i])) + std::abs(int(a[i + 1]) - int(b[i + 1])) +
                 std::abs(int(a[i + 2]) - int(b[i + 2]));
        counted += 3;
    }
    return total / static_cast<double>(counted);
}

/// Mean absolute luminance deviation — the detail statistic the textured
/// tests established. Proves sampling happens; says nothing about parallax,
/// which is why the assertions above it are differential.
double variationOf(const std::vector<std::uint8_t>& rgba) {
    double mean = 0.0;
    long long n = 0;
    std::vector<double> luma;
    luma.reserve(rgba.size() / 4);
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

enum class HeightMode { None, ZeroScale, On };

/// A mounted scene ready to render repeatedly: the cost case iterates it, the
/// pixel cases render once and read back.
struct Bench {
    hp::AssetPool pool;
    hp::Scene scene;
    hp::SceneView view;
    bool ready = false;

    /// @p repeats draws the same scene several times before the single
    /// readback: the benchmark's device-overhead amortiser. One draw of the
    /// march costs tens of *nano*seconds per pixel on a discrete GPU, and a
    /// readback-synchronised frame costs a millisecond — timed one draw per
    /// sync, the first RTX numbers came back negative. Thirty-two draws per
    /// sync puts the work above the noise while staying far below the
    /// dynamic-heap budget between frame boundaries.
    bool render(Device& device, std::vector<std::uint8_t>* pixels, int repeats = 1) {
        for (int i = 0; i < repeats; ++i) {
            hp::SceneViewStats stats;
            if (view.render(device.render->context(), scene, pool, 0, &stats) == nullptr) {
                return false;
            }
            if (stats.submitted != 1) {
                return false;
            }
        }
        bool ok = false;
        if (pixels != nullptr) {
            ok = view.readback(device.render->context(), *pixels);
        } else {
            // The cost loop still reads back every frame: it is the only
            // synchronisation point an offscreen view has, and without one
            // the loop would time command recording rather than the march.
            std::vector<std::uint8_t> sink;
            ok = view.readback(device.render->context(), sink);
        }
        // **Every iteration must be a real engine frame.** `onRender`
        // presents, and presenting is what advances Diligent's frame and
        // recycles the dynamic heap. Without it, a few hundred offscreen
        // renders exhaust the heap — "Space in dynamic heap is exhausted!"
        // once per frame — and every draw after that silently writes
        // nothing, which turned half this benchmark's first numbers into
        // measurements of a blank frame.
        device.render->onRender();
        return ok;
    }
};

/// Mounts the scratch directory (mesh must already be written into
/// `scratch/models`), builds the triplanar rock material and the scene.
bool setUpBench(Device& device, Bench& bench, const std::filesystem::path& scratch,
                HeightMode heightMode, int viewSize) {
    const std::filesystem::path textures = findTextureDir();
    if (textures.empty()) {
        return false;
    }
    std::error_code ec;
    for (const char* name : {"rock_basecolour.png", "rock_height.png"}) {
        std::filesystem::copy_file(textures / name, scratch / "models" / name,
                                   std::filesystem::copy_options::overwrite_existing, ec);
    }

    hp::Vfs::shutdown();
    if (!hp::Vfs::init(nullptr) || !hp::Vfs::mount(scratch.string())) {
        return false;
    }

    const hp::Guid meshGuid = hp::Guid::generate();
    auto mesh =
        hp::loadMesh(device.render->device(), device.render->context(), "models/mesh.gltf");
    if (!mesh || !mesh->valid()) {
        return false;
    }
    bench.pool.store<hp::MeshAsset>(meshGuid, mesh);

    const hp::Guid colourGuid = hp::Guid::generate();
    bench.pool.store<hp::TextureAsset>(
        colourGuid, hp::loadTexture(device.render->device(), "models/rock_basecolour.png"));
    hp::Guid heightGuid;
    if (heightMode != HeightMode::None) {
        heightGuid = hp::Guid::generate();
        bench.pool.store<hp::TextureAsset>(
            heightGuid, hp::loadTexture(device.render->device(), "models/rock_height.png"));
    }

    const hp::Guid materialGuid = hp::Guid::generate();
    {
        auto material = std::make_shared<hp::Material>();
        material->baseColourTexture = colourGuid;
        material->triplanar = true;
        // Half a tile per metre, 141.8's proven setting for these meshes.
        material->triplanarScale = 0.5F;
        material->heightTexture = heightGuid;
        material->heightScale = heightMode == HeightMode::On ? 0.08F : 0.0F;
        // Unlit, so a differing pixel is a displaced texel and nothing else.
        material->unlit = true;
        material->doubleSided = true;
        bench.pool.store<hp::Material>(materialGuid, material);
    }

    hp::Entity cameraEntity = bench.scene.create("camera");
    cameraEntity.add<hp::Camera>(hp::Camera{});

    hp::Entity meshEntity = bench.scene.create("mesh");
    hp::MeshRenderer renderer;
    renderer.mesh = meshGuid;
    renderer.materials = {materialGuid};
    meshEntity.add<hp::MeshRenderer>(renderer);
    bench.scene.propagateTransforms();

    if (!bench.view.create(device.render->device(), device.render->context(), viewSize,
                           viewSize)) {
        return false;
    }
    bench.view.setClearColour(0.0F, 0.0F, 1.0F, 1.0F);
    bench.ready = true;
    return true;
}

/// One render of the mesh already written to the scratch directory.
bool renderOnce(Device& device, const std::filesystem::path& scratch, HeightMode heightMode,
                std::vector<std::uint8_t>& pixels) {
    Bench bench;
    if (!setUpBench(device, bench, scratch, heightMode, kSize)) {
        return false;
    }
    return bench.render(device, &pixels);
}

} // namespace

TEST_CASE("parallax under triplanar displaces texels on a UV-less quad") {
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

    // Oblique: the normal is yawed ~40 degrees off the view axis, so the X
    // and Z projections both contribute (weights ~0.33 / 0.67) and the view
    // vector has real tangent-plane components across the quad — a facing
    // quad would pass with the march deleted, exactly as 141.7 recorded.
    std::error_code ec;
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "hp-triplanar-parallax";
    std::filesystem::remove_all(scratch, ec);
    std::filesystem::create_directories(scratch / "models", ec);
    writeOrientedQuadGltf(scratch / "models", Vec3{0.0F, 0.0F, 4.0F},
                          Vec3{0.644F, 0.0F, -0.766F}, 4.0F);

    std::vector<std::uint8_t> off;
    std::vector<std::uint8_t> zeroScale;
    std::vector<std::uint8_t> displaced;
    REQUIRE(renderOnce(device, scratch, HeightMode::None, off));
    REQUIRE(renderOnce(device, scratch, HeightMode::ZeroScale, zeroScale));
    REQUIRE(renderOnce(device, scratch, HeightMode::On, displaced));

    writePpm("triplanar_parallax_off", off, kSize);
    writePpm("triplanar_parallax_zero_scale", zeroScale, kSize);
    writePpm("triplanar_parallax_on", displaced, kSize);

    const double zeroDiff = meanDifference(off, zeroScale);
    const double onDiff = meanDifference(off, displaced);
    MESSAGE("no-height vs zero-scale: " << zeroDiff << "; no-height vs displaced: " << onDiff);

    // **A zero scale must march nowhere, in any projection.** The pipelines
    // differ (marches and `SampleGrad` compiled into one, `SampleBias` in
    // the other), so this is also the check that explicit pre-march
    // gradients times `exp2(0)` select the same mips `SampleBias` does.
    CHECK(zeroDiff == 0.0);

    // A working scale must move a substantial share of an oblique frame.
    CHECK(onDiff > 3.0);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("parallax under triplanar works on a terrain-shaped surface") {
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

    std::error_code ec;
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "hp-triplanar-terrain";
    std::filesystem::remove_all(scratch, ec);
    std::filesystem::create_directories(scratch / "models", ec);
    writeTerrainGltf(scratch / "models");

    std::vector<std::uint8_t> off;
    std::vector<std::uint8_t> zeroScale;
    std::vector<std::uint8_t> displaced;
    REQUIRE(renderOnce(device, scratch, HeightMode::None, off));
    REQUIRE(renderOnce(device, scratch, HeightMode::ZeroScale, zeroScale));
    REQUIRE(renderOnce(device, scratch, HeightMode::On, displaced));

    writePpm("triplanar_terrain_off", off, kSize);
    writePpm("triplanar_terrain_zero_scale", zeroScale, kSize);
    writePpm("triplanar_terrain_on", displaced, kSize);

    // The terrain must actually be covered in rock before a differential
    // claim about it means anything.
    const double variation = variationOf(off);
    const double zeroDiff = meanDifference(off, zeroScale);
    const double onDiff = meanDifference(off, displaced);
    MESSAGE("terrain variation " << variation << "; no-height vs zero-scale: " << zeroDiff
                                 << "; no-height vs displaced: " << onDiff);

    // Lower than the quad tests' 8.0 because the sky occupies the upper
    // part of the frame and dilutes the statistic; measured 7.4 on both
    // targets with rock across the whole terrain.
    CHECK(variation > 5.0);
    CHECK(zeroDiff == 0.0);
    // Lower bar than the quad: the sky half of the frame cannot change, and
    // flat ground viewed at ground-level angles shows little parallax. The
    // measured value belongs in the ticket.
    CHECK(onDiff > 1.0);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("parallax under triplanar: cost per weight configuration") {
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

    // Each configuration positions the quad off the view axis so every live
    // projection also has a real view component — a projection edge-on to
    // the view early-outs by design (the offset diverges as 1/z), and a
    // benchmark that let that happen would time the early-out, not the
    // march.
    struct Config {
        const char* name;
        Vec3 centre;
        Vec3 normal;
    };
    // Normals chosen so every configuration faces the camera comfortably —
    // the first cut of the three-axis case pointed its plane almost edge-on
    // through the camera and rasterised zero pixels, which the coverage
    // CHECK below now catches. Weights: (0,0,1), (0.5,0,0.5) and
    // (0.27,0.27,0.46) — one, two and three live axes under the 1% floor.
    const Config configs[] = {
        {"one axis (facing)", {0.0F, 0.0F, 5.0F}, {0.0F, 0.0F, -1.0F}},
        {"two axes (45 deg)", {2.5F, 0.0F, 5.0F}, {0.707F, 0.0F, -0.707F}},
        {"three axes (corner)", {1.2F, 1.2F, 5.0F}, {-0.55F, -0.55F, -0.63F}},
    };
    MESSAGE("cost device: " << device.render->adapterDescription());

    constexpr int kBenchSize = 384;
    std::error_code ec;
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "hp-triplanar-cost";

    int configIndex = 0;
    for (const Config& config : configs) {
        double msByMode[2] = {0.0, 0.0};
        long long covered = 0;
        for (const HeightMode mode : {HeightMode::None, HeightMode::On}) {
            std::filesystem::remove_all(scratch, ec);
            std::filesystem::create_directories(scratch / "models", ec);
            writeOrientedQuadGltf(scratch / "models", config.centre, config.normal, 5.0F);

            Bench bench;
            REQUIRE(setUpBench(device, bench, scratch, mode, kBenchSize));
            // The configurations cover different shares of the frame, so the
            // per-frame delta is normalised by covered pixels below — the
            // per-pixel number is the comparable one.
            {
                std::vector<std::uint8_t> pixels;
                REQUIRE(bench.render(device, &pixels));
                writePpm("triplanar_cost_" + std::to_string(configIndex) +
                             (mode == HeightMode::On ? "_on" : "_off"),
                         pixels, kBenchSize);
                if (mode == HeightMode::None) {
                    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
                        const bool clear =
                            pixels[i] == 0 && pixels[i + 1] == 0 && pixels[i + 2] == 255;
                        covered += clear ? 0 : 1;
                    }
                }
            }
            constexpr int kRepeats = 32;
            for (int warm = 0; warm < 3; ++warm) {
                REQUIRE(bench.render(device, nullptr, kRepeats));
            }

            const auto start = std::chrono::steady_clock::now();
            int frames = 0;
            double elapsed = 0.0;
            while ((elapsed < 1.5 && frames < 40) || frames < 6) {
                REQUIRE(bench.render(device, nullptr, kRepeats));
                ++frames;
                elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
                              .count();
            }
            msByMode[mode == HeightMode::On ? 1 : 0] =
                elapsed * 1000.0 / (static_cast<double>(frames) * kRepeats);

            // The frame must still be live after the loop — the dynamic-heap
            // exhaustion this benchmark first hit blanked every frame *after*
            // the first, so a single up-front coverage check passed while the
            // timed frames measured a clear pass.
            {
                std::vector<std::uint8_t> pixels;
                REQUIRE(bench.render(device, &pixels));
                long long still = 0;
                for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
                    const bool clear =
                        pixels[i] == 0 && pixels[i + 1] == 0 && pixels[i + 2] == 255;
                    still += clear ? 0 : 1;
                }
                CHECK(still > 0);
            }
            hp::Vfs::shutdown();
        }
        // A configuration that rasterises nothing measures nothing — this is
        // exactly how the edge-on three-axis quad was caught.
        CHECK(covered > 0);
        const double marchMs = msByMode[1] - msByMode[0];
        const double nsPerPixel =
            covered > 0 ? marchMs * 1.0e6 / static_cast<double>(covered) : 0.0;
        MESSAGE(std::string(config.name)
                << ": height off " << msByMode[0] << " ms/draw, on " << msByMode[1]
                << " ms/draw, march cost " << marchMs << " ms/draw over " << covered
                << " covered pixels = " << nsPerPixel << " ns/pixel");
        ++configIndex;
    }

    tearDown(device);
}
