// The tangent frame, measured on an asset built to disagree with itself (T0166.3).
//
// **Every mesh this engine had ever rendered was authored by the people who
// wrote the code that consumes it**, and every one of them had *consistent* UV
// winding — so the sign of the UV determinant was invisible by construction.
// This file supplies the missing case: one mesh, two shells, same texture, same
// lighting, and **one of them mirrored in `u`**, which is what Blender's UV
// Mirror produces and what a symmetric character or prop ships with.
//
// ## What "mirrored" means here, stated once, because the sign conventions bite
//
// For a triangle, the **signed UV area** is
// `(uv1 - uv0) x (uv2 - uv0)` taken as a scalar cross product. Under glTF's
// texture convention — origin at the image's *upper left*, `v` increasing
// **downward** — a chart that shows its texture the right way up on a
// camera-facing surface has a **negative** signed UV area. That is not a
// preference; it follows from `v` pointing down while the surface's own "up" is
// `+Y`, and `tests/gpu/parallax_test.cpp`'s quad, the engine's own reference,
// has it. A **mirrored** shell is the other sign.
//
// So in this file: **shell A is negative (plain), shell B is positive
// (mirrored)**, and `writeTwoShellGltf` asserts exactly that on the bytes it
// emitted rather than on the arithmetic it meant to emit.
//
// ## Why a uniformly black height map
//
// The march's displacement is `-viewTS.xy / viewTS.z * heightScale * depth`,
// and `depth = 1 - height`. A **black** height map is `depth == 1` everywhere,
// which makes the displacement a closed form — `-fullOffset`, exactly, with no
// dependence on how many layers the march took or where the field happened to
// rise. It is also the one texel value that survives `loadTexture`'s
// unconditional `IsSRGB = true` unchanged (0 maps to 0), so the measurement
// does not quietly depend on a defect that belongs to another ticket.
//
// ## What is here but unused, deliberately
//
// The asset, the PNG writer, the normal maps and the lit render path are all
// built; only the *parallax* case reads them. The lit half — a tangent-space
// normal tilted along `v` must shade both shells the same, since only `u` is
// mirrored — was written and measured, and it **fails**: shell A reads 0 and
// shell B 130.2 under the same map. That is the normal map's bitangent, not the
// parallax frame this file landed, so it went to **T0168** with its numbers on
// T0166 rather than being fixed here. The machinery stays so that case is a
// `TEST_CASE` away rather than a rewrite.
//
// Bucket: gpu. Skips cleanly with no device.

#include <doctest/doctest.h>

#include <hp/Assets.hpp>
#include <hp/Camera.hpp>
#include <hp/Light.hpp>
#include <hp/Log.hpp>
#include <hp/Material.hpp>
#include <hp/Math.hpp>
#include <hp/Render.hpp>
#include <hp/Scene.hpp>
#include <hp/SceneView.hpp>
#include <hp/Vfs.hpp>
#include <hp/Window.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr int kSize = 256;
constexpr int kTexture = 64;

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
    windowConfig.title = "hp tangent frame test";
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

// ---------------------------------------------------------------------------
// A minimal PNG writer.
//
// **Written here rather than pulled in**, and the reason is the point of the
// file: these textures must be *generated from a rule that is readable beside
// the assertion*, the way `tools/make_cube_gltf.py` generates the cube. A
// committed binary would put the one thing under test — which texel is where —
// somewhere a reviewer cannot check.
//
// The deflate stream uses **stored** (uncompressed) blocks. That is a complete,
// conformant zlib stream, it needs no compressor, and at 64x64 the size is
// irrelevant. `IHDR`/`IDAT`/`IEND` with colour type 6 (RGBA8) is the whole
// format surface used.
// ---------------------------------------------------------------------------

std::uint32_t crc32Of(const std::uint8_t* data, std::size_t size) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1U) != 0U ? 0xEDB88320U ^ (c >> 1U) : c >> 1U;
            }
            t[n] = c;
        }
        return t;
    }();
    std::uint32_t c = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < size; ++i) {
        c = table[(c ^ data[i]) & 0xFFU] ^ (c >> 8U);
    }
    return c ^ 0xFFFFFFFFU;
}

void pushBe32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24U));
    out.push_back(static_cast<std::uint8_t>(value >> 16U));
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
    out.push_back(static_cast<std::uint8_t>(value));
}

void pushChunk(std::vector<std::uint8_t>& out, const char tag[5],
               const std::vector<std::uint8_t>& body) {
    pushBe32(out, static_cast<std::uint32_t>(body.size()));
    std::vector<std::uint8_t> crcInput;
    crcInput.insert(crcInput.end(), tag, tag + 4);
    crcInput.insert(crcInput.end(), body.begin(), body.end());
    out.insert(out.end(), crcInput.begin(), crcInput.end());
    pushBe32(out, crc32Of(crcInput.data(), crcInput.size()));
}

/// Writes @p rgba (width*height*4, row-major, **top row first**) as a PNG.
void writePng(const std::filesystem::path& path, int width, int height,
              const std::vector<std::uint8_t>& rgba) {
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(height) * (1U + static_cast<std::size_t>(width) * 4U));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0); // filter type 0 (None)
        const auto* row = rgba.data() + static_cast<std::size_t>(y) * width * 4;
        raw.insert(raw.end(), row, row + static_cast<std::size_t>(width) * 4);
    }

    // zlib stream: 0x78 0x01 header, stored deflate blocks, adler32 trailer.
    std::vector<std::uint8_t> zlib{0x78, 0x01};
    std::size_t offset = 0;
    while (offset < raw.size()) {
        const std::size_t chunk = std::min<std::size_t>(65535, raw.size() - offset);
        const bool last = offset + chunk >= raw.size();
        zlib.push_back(last ? 1 : 0);
        zlib.push_back(static_cast<std::uint8_t>(chunk & 0xFFU));
        zlib.push_back(static_cast<std::uint8_t>((chunk >> 8U) & 0xFFU));
        const std::uint16_t inverse = static_cast<std::uint16_t>(~static_cast<std::uint16_t>(chunk));
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

    std::vector<std::uint8_t> png{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<std::uint8_t> ihdr;
    pushBe32(ihdr, static_cast<std::uint32_t>(width));
    pushBe32(ihdr, static_cast<std::uint32_t>(height));
    ihdr.push_back(8); // bit depth
    ihdr.push_back(6); // colour type: RGBA
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    pushChunk(png, "IHDR", ihdr);
    pushChunk(png, "IDAT", zlib);
    pushChunk(png, "IEND", {});

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(png.data()),
               static_cast<std::streamsize>(png.size()));
}

/// The byte that decodes to @p linear through the sRGB transfer function.
///
/// **This exists because of a defect that belongs to another ticket, and naming
/// it is cheaper than being surprised by it**: `hp::loadTexture` sets
/// `IsSRGB = true` for *every* texture, normal and height maps included
/// (`AssetImport.cpp`, which says so in a comment). A normal map's 128 therefore
/// does not decode to 0.5 in the shader, it decodes to 0.214. Encoding the
/// *intended linear value* here keeps this file's normals meaning what they say.
std::uint8_t linearToSrgbByte(double linear) {
    const double clamped = linear < 0.0 ? 0.0 : (linear > 1.0 ? 1.0 : linear);
    const double encoded = clamped <= 0.0031308
                               ? clamped * 12.92
                               : 1.055 * std::pow(clamped, 1.0 / 2.4) - 0.055;
    return static_cast<std::uint8_t>(std::lround(encoded * 255.0));
}

/// A hard vertical edge at `u = 0.5`: black left, white right.
///
/// A step rather than a ramp, because what is measured is a *displacement* and
/// an edge's position survives mip selection, anisotropy and the sRGB curve
/// while a ramp's absolute value survives none of them.
void writeStepTexture(const std::filesystem::path& path) {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(kTexture) * kTexture * 4, 0);
    for (int y = 0; y < kTexture; ++y) {
        for (int x = 0; x < kTexture; ++x) {
            const std::uint8_t value = x < kTexture / 2 ? 0 : 255;
            const std::size_t at = (static_cast<std::size_t>(y) * kTexture + x) * 4;
            rgba[at] = value;
            rgba[at + 1] = value;
            rgba[at + 2] = value;
            rgba[at + 3] = 255;
        }
    }
    writePng(path, kTexture, kTexture, rgba);
}

/// A uniform colour. Black is the height map that makes the march a closed form.
void writeSolidTexture(const std::filesystem::path& path, std::uint8_t r, std::uint8_t g,
                       std::uint8_t b) {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(kTexture) * kTexture * 4, 255);
    for (std::size_t at = 0; at + 3 < rgba.size(); at += 4) {
        rgba[at] = r;
        rgba[at + 1] = g;
        rgba[at + 2] = b;
        rgba[at + 3] = 255;
    }
    writePng(path, kTexture, kTexture, rgba);
}

/// A uniform tangent-space normal, given in **linear** components.
void writeNormalTexture(const std::filesystem::path& path, double x, double y, double z) {
    writeSolidTexture(path, linearToSrgbByte((x + 1.0) * 0.5), linearToSrgbByte((y + 1.0) * 0.5),
                      linearToSrgbByte((z + 1.0) * 0.5));
}

// ---------------------------------------------------------------------------
// The asset: one mesh, one primitive, two shells.
// ---------------------------------------------------------------------------

/// Half-width of each shell in mesh units; both shells are 8 wide and 4 tall.
constexpr float kHalfWidth = 4.0F;

/// Where each shell's `v` range sits. **Both shells use the texture's top half**
/// — the same texels, which is what "same texture" has to mean for the
/// comparison to say anything.
constexpr float kVSpan = 0.5F;

struct ShellBand {
    float bottom;
    float top;
};

/// Shell A occupies mesh `y` in [0.1, 4.1]; shell B the mirror band below zero.
constexpr ShellBand kShellA{0.1F, 4.1F};
constexpr ShellBand kShellB{-4.1F, -0.1F};

/// Writes the two-shell mesh and **asserts the invariants on the emitted data**.
///
/// The asserts are the whole reason this is a generator rather than a committed
/// file. Three of them, per triangle:
///
///  * the winding agrees with the authored normal (D33 — `cross(v1-v0, v2-v0)`
///    points along `+N`);
///  * shell A's signed UV area is **negative** — the plain, glTF-oriented chart;
///  * shell B's is **positive** — mirrored, and *provably* so rather than
///    intended-so.
void writeTwoShellGltf(const std::filesystem::path& directory) {
    struct Vertex {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
    };
    std::vector<Vertex> vertices;
    std::vector<std::uint16_t> indices;

    const auto addShell = [&](ShellBand band, bool mirrored) {
        const auto base = static_cast<std::uint16_t>(vertices.size());
        // Corners in BL, BR, TR, TL order. `u` runs with `+x` on the plain
        // shell and against it on the mirrored one; `v` runs *downward* on
        // both, which is glTF's own image convention and the only thing the
        // two shells are required to share besides the texture.
        const std::array<std::pair<float, float>, 4> corners{
            std::pair{-kHalfWidth, band.bottom}, std::pair{kHalfWidth, band.bottom},
            std::pair{kHalfWidth, band.top}, std::pair{-kHalfWidth, band.top}};
        for (const auto& [x, y] : corners) {
            const float acrossFraction = (x + kHalfWidth) / (2.0F * kHalfWidth);
            const float u = mirrored ? 1.0F - acrossFraction : acrossFraction;
            const float v = (band.top - y) / (band.top - band.bottom) * kVSpan;
            vertices.push_back(Vertex{x, y, 0.0F, 0.0F, 0.0F, 1.0F, u, v});
        }
        for (const std::uint16_t i : {0, 1, 2, 0, 2, 3}) {
            indices.push_back(static_cast<std::uint16_t>(base + i));
        }
    };
    addShell(kShellA, /*mirrored=*/false);
    addShell(kShellB, /*mirrored=*/true);

    REQUIRE(vertices.size() == 8);
    REQUIRE(indices.size() == 12);

    for (std::size_t tri = 0; tri * 3 + 2 < indices.size(); ++tri) {
        const Vertex& v0 = vertices[indices[tri * 3]];
        const Vertex& v1 = vertices[indices[tri * 3 + 1]];
        const Vertex& v2 = vertices[indices[tri * 3 + 2]];
        // Winding against the authored normal (+Z): the z component of
        // `cross(v1 - v0, v2 - v0)` must be positive.
        const float ax = v1.px - v0.px;
        const float ay = v1.py - v0.py;
        const float bx = v2.px - v0.px;
        const float by = v2.py - v0.py;
        CHECK(ax * by - ay * bx > 0.0F);
        // The signed UV area, and this is the number the whole file is about.
        const float uvArea = (v1.u - v0.u) * (v2.v - v0.v) - (v1.v - v0.v) * (v2.u - v0.u);
        if (tri < 2) {
            CHECK_MESSAGE(uvArea < 0.0F, "shell A must be the plain, glTF-oriented chart");
        } else {
            CHECK_MESSAGE(uvArea > 0.0F, "shell B must be mirrored -- that is its whole job");
        }
    }

    std::vector<unsigned char> bin;
    const auto* vb = reinterpret_cast<const unsigned char*>(vertices.data());
    bin.insert(bin.end(), vb, vb + vertices.size() * sizeof(Vertex));
    const std::size_t indexOffset = bin.size();
    const auto* ib = reinterpret_cast<const unsigned char*>(indices.data());
    bin.insert(bin.end(), ib, ib + indices.size() * sizeof(std::uint16_t));

    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    {
        std::ofstream file(directory / "shells.bin", std::ios::binary);
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
    "doubleSided": false,
    "pbrMetallicRoughness": { "metallicFactor": 0.0, "roughnessFactor": 1.0 }
  } ],
  "buffers": [ { "uri": "shells.bin", "byteLength": )" +
                             std::to_string(bin.size()) + R"( } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": )" +
                             std::to_string(indexOffset) + R"(, "byteStride": 32 },
    { "buffer": 0, "byteOffset": )" +
                             std::to_string(indexOffset) + R"(, "byteLength": )" +
                             std::to_string(bin.size() - indexOffset) + R"( }
  ],
  "accessors": [
    { "bufferView": 0, "byteOffset": 0,  "componentType": 5126, "count": 8, "type": "VEC3",
      "min": [-4.0, -4.1, 0.0], "max": [4.0, 4.1, 0.0] },
    { "bufferView": 0, "byteOffset": 12, "componentType": 5126, "count": 8, "type": "VEC3" },
    { "bufferView": 0, "byteOffset": 24, "componentType": 5126, "count": 8, "type": "VEC2" },
    { "bufferView": 1, "byteOffset": 0,  "componentType": 5123, "count": 12, "type": "SCALAR" }
  ]
})";
    std::ofstream file(directory / "shells.gltf", std::ios::binary);
    file << json;
}

// ---------------------------------------------------------------------------
// Rendering and measurement
// ---------------------------------------------------------------------------

struct Options {
    bool withHeight = false;
    float heightScale = 0.0F;
    const char* normalMap = nullptr; ///< file name under models/, or null
    bool unlit = true;
    float yaw = 0.0F;
    float pitch = 0.0F;
    hp::Quaternion lightRotation = hp::Quaternion{};
};

/// Renders the two-shell quad. Returns false only on a real failure.
bool renderShells(Device& device, const std::filesystem::path& scratch, const Options& options,
                  std::vector<std::uint8_t>& pixels) {
    hp::Vfs::shutdown();
    if (!hp::Vfs::init(nullptr) || !hp::Vfs::mount(scratch.string())) {
        return false;
    }

    hp::AssetPool pool;
    const hp::Guid meshGuid = hp::Guid::generate();
    auto mesh =
        hp::loadMesh(device.render->device(), device.render->context(), "models/shells.gltf");
    if (!mesh || !mesh->valid()) {
        return false;
    }
    pool.store<hp::MeshAsset>(meshGuid, mesh);

    const hp::Guid colourGuid = hp::Guid::generate();
    pool.store<hp::TextureAsset>(colourGuid,
                                 hp::loadTexture(device.render->device(), "models/step.png"));

    const hp::Guid materialGuid = hp::Guid::generate();
    {
        auto material = std::make_shared<hp::Material>();
        material->baseColourTexture = colourGuid;
        if (options.withHeight) {
            const hp::Guid heightGuid = hp::Guid::generate();
            pool.store<hp::TextureAsset>(
                heightGuid, hp::loadTexture(device.render->device(), "models/black.png"));
            material->heightTexture = heightGuid;
            material->heightScale = options.heightScale;
        }
        if (options.normalMap != nullptr) {
            const hp::Guid normalGuid = hp::Guid::generate();
            pool.store<hp::TextureAsset>(
                normalGuid, hp::loadTexture(device.render->device(),
                                            std::string("models/") + options.normalMap));
            material->normalTexture = normalGuid;
        }
        material->unlit = options.unlit;
        // **Single-sided, deliberately.** A double-sided material would hide a
        // winding mistake in the generator, which is the one thing this asset
        // must not be able to do.
        material->doubleSided = false;
        material->roughness = 1.0F;
        material->metallic = 0.0F;
        pool.store<hp::Material>(materialGuid, material);
    }

    hp::Scene scene;
    hp::Entity cameraEntity = scene.create("camera");
    cameraEntity.add<hp::Camera>(hp::Camera{});

    hp::Entity quad = scene.create("shells");
    hp::MeshRenderer renderer;
    renderer.mesh = meshGuid;
    renderer.materials = {materialGuid};
    quad.add<hp::MeshRenderer>(renderer);
    hp::Transform& transform = quad.get<hp::Transform>();
    transform.position = hp::float3{0.0F, 0.0F, -6.0F};
    transform.rotation =
        hp::Quaternion::RotationFromAxisAngle(hp::float3{1.0F, 0.0F, 0.0F}, options.pitch) *
        hp::Quaternion::RotationFromAxisAngle(hp::float3{0.0F, 1.0F, 0.0F}, options.yaw);

    if (!options.unlit) {
        hp::Entity lightEntity = scene.create("sun");
        hp::Light sun;
        sun.type = hp::LightType::Directional;
        sun.colour = hp::float3{1.0F, 1.0F, 1.0F};
        sun.intensity = 3.0F;
        lightEntity.add<hp::Light>(sun);
        lightEntity.get<hp::Transform>().rotation = options.lightRotation;
    }
    scene.propagateTransforms();

    hp::SceneView view;
    if (!view.create(device.render->device(), device.render->context(), kSize, kSize)) {
        return false;
    }
    // Blue, so "nothing drew" is distinguishable from every shading outcome.
    view.setClearColour(0.0F, 0.0F, 1.0F, 1.0F);

    hp::SceneViewStats stats;
    if (view.render(device.render->context(), scene, pool, 0, &stats) == nullptr ||
        stats.submitted != 1) {
        return false;
    }
    return view.readback(device.render->context(), pixels);
}

/// Rows of the readback that belong to a shell. Row 0 is the **top** of the
/// image (pinned by `present_blit_test`), and world `+Y` lands on screen top, so
/// shell A — the upper band in mesh space — occupies the smaller row numbers.
struct RowBand {
    int first;
    int last;
};

constexpr RowBand kBandA{40, 105};  ///< shell A, comfortably inside its extent
constexpr RowBand kBandB{150, 215}; ///< shell B

/// A screen column window around the texture's `u = 0.5` edge, wide enough to
/// contain the displacement and narrow enough to exclude the shells' own
/// borders (where the marched UV leaves [0,1] and wraps).
constexpr int kColumnFirst = 70;
constexpr int kColumnLast = 185;

/// Mean red over the measurement window. The base colour is a black/white step,
/// so this is a monotone, signed proxy for **where the edge is**: the edge
/// moving toward larger columns leaves more black behind it and lowers the mean.
double meanOverWindow(const std::vector<std::uint8_t>& pixels, RowBand band) {
    double total = 0.0;
    long long counted = 0;
    for (int y = band.first; y <= band.last; ++y) {
        for (int x = kColumnFirst; x <= kColumnLast; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) * kSize + x) * 4;
            total += pixels[at];
            ++counted;
        }
    }
    return counted > 0 ? total / static_cast<double>(counted) : -1.0;
}

/// The column at which the row-averaged red **crosses** half scale, whichever
/// way round the step runs.
///
/// It runs both ways on purpose: shell B reads its texture mirrored, so its
/// step is white-on-the-left where shell A's is black-on-the-left. A one-sided
/// search would report the window's own edge for one of them and quietly look
/// like a measurement.
double edgeColumn(const std::vector<std::uint8_t>& pixels, RowBand band) {
    const auto columnMean = [&](int x) {
        double total = 0.0;
        int counted = 0;
        for (int y = band.first; y <= band.last; ++y) {
            total += pixels[(static_cast<std::size_t>(y) * kSize + x) * 4];
            ++counted;
        }
        return counted > 0 ? total / counted : -1.0;
    };
    const bool startsDark = columnMean(kColumnFirst) < 128.0;
    for (int x = kColumnFirst; x <= kColumnLast; ++x) {
        const double value = columnMean(x);
        if (startsDark ? value >= 128.0 : value < 128.0) {
            return x;
        }
    }
    return -1.0;
}

/// Mean luminance over a band, for the lit cases.
double meanLuma(const std::vector<std::uint8_t>& pixels, RowBand band) {
    double total = 0.0;
    long long counted = 0;
    for (int y = band.first; y <= band.last; ++y) {
        for (int x = kColumnFirst; x <= kColumnLast; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) * kSize + x) * 4;
            total += 0.2126 * pixels[at] + 0.7152 * pixels[at + 1] + 0.0722 * pixels[at + 2];
            ++counted;
        }
    }
    return counted > 0 ? total / static_cast<double>(counted) : -1.0;
}

/// Share of the measurement window that is the compile-failure checkerboard.
///
/// **A failed shader renders magenta and passes every coverage assertion**, and
/// that has been read as a working render twice in this project. Every asserted
/// frame in this file goes through here.
double magentaShare(const std::vector<std::uint8_t>& pixels) {
    long long magenta = 0;
    long long counted = 0;
    for (int y = kBandA.first; y <= kBandB.last; ++y) {
        for (int x = kColumnFirst; x <= kColumnLast; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) * kSize + x) * 4;
            const int r = pixels[at];
            const int g = pixels[at + 1];
            const int b = pixels[at + 2];
            if (r > 90 && r < 165 && g < 60 && b > 90 && b < 165) {
                ++magenta;
            }
            ++counted;
        }
    }
    return counted > 0 ? static_cast<double>(magenta) / static_cast<double>(counted) : 1.0;
}

std::filesystem::path prepareScratch() {
    std::error_code ec;
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "hp-tangent-frame";
    std::filesystem::remove_all(scratch, ec);
    std::filesystem::create_directories(scratch / "models", ec);
    writeTwoShellGltf(scratch / "models");
    writeStepTexture(scratch / "models" / "step.png");
    // Black: `depth = 1 - height` is 1 everywhere, so the march's displacement
    // is exactly `-fullOffset` and depends on the frame and nothing else.
    writeSolidTexture(scratch / "models" / "black.png", 0, 0, 0);
    // Tangent-space normals tilted a known way, in linear components.
    // The normal maps a lit case needs. Written here rather than by the one
    // case that uses them, because `prepareScratch` is the file's single
    // account of what the controlled asset consists of.
    //
    // **Nothing in this file reads them yet.** The lit half of the two-shell
    // measurement — a tangent-space normal tilted along `v` must shade both
    // shells the *same*, since only `u` is mirrored — was written, measured and
    // moved to **T0168**: it fails today, and what it fails on is the normal
    // map's bitangent rather than the parallax frame this file landed. The
    // numbers are on T0166 so the case can be rebuilt rather than rediscovered.
    writeNormalTexture(scratch / "models" / "normal_up.png", 0.0, 0.6, 0.8);
    writeNormalTexture(scratch / "models" / "normal_down.png", 0.0, -0.6, 0.8);
    writeNormalTexture(scratch / "models" / "normal_right.png", 0.6, 0.0, 0.8);
    writeNormalTexture(scratch / "models" / "normal_left.png", -0.6, 0.0, 0.8);
    return scratch;
}

} // namespace

TEST_CASE("parallax lands a texel where the view ray does, on a mirrored shell too (T0166.3)") {
    // **The case the engine has never had**, and it asserts two independent
    // things because the defect it found needed both to be caught.
    //
    // **The mirror half.** Both shells are the same physical surface at the same
    // yaw under the same view, so a texel displaced by parallax must appear at
    // the same *screen* position on both, whichever way that shell's `u` runs.
    // In each shell's own UV the displacement is opposite — that is what
    // mirrored means — so a test written in UV space would have to pick the sign
    // it expected and could therefore agree with the bug.
    //
    // **The absolute half, and this is the one that mattered.** The mirror half
    // passes for a frame that is rotated 180 degrees *consistently*, which is
    // exactly what `HpParallaxUv` did on every asset before T0166.2. So the
    // direction is pinned from first principles rather than from a baseline:
    //
    //   parallax samples the surface **further along the view ray**, so on an
    //   obliquely-viewed plane the sampled texel comes from the **far** side,
    //   and a feature in the texture therefore appears shifted toward the
    //   **near** side.
    //
    // The quad is yawed `+0.6` about `Y`, which turns its local `+X` — the `+u`
    // side of the plain shell — **away** from the camera. So the step edge must
    // move toward the `-u` side, which is screen **left**, and a smaller column.
    // Before the fix it moved right.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }
    const std::filesystem::path scratch = prepareScratch();

    Options flat;
    flat.yaw = 0.6F;
    Options displaced = flat;
    displaced.withHeight = true;
    displaced.heightScale = 0.06F;

    std::vector<std::uint8_t> flatPixels;
    std::vector<std::uint8_t> displacedPixels;
    REQUIRE(renderShells(device, scratch, flat, flatPixels));
    REQUIRE(renderShells(device, scratch, displaced, displacedPixels));

    const double magentaFlat = magentaShare(flatPixels);
    const double magentaDisplaced = magentaShare(displacedPixels);
    MESSAGE("magenta shares: flat " << magentaFlat << ", displaced " << magentaDisplaced);
    REQUIRE(magentaFlat < 0.05);
    REQUIRE(magentaDisplaced < 0.05);

    const double flatA = edgeColumn(flatPixels, kBandA);
    const double flatB = edgeColumn(flatPixels, kBandB);
    const double displacedA = edgeColumn(displacedPixels, kBandA);
    const double displacedB = edgeColumn(displacedPixels, kBandB);
    MESSAGE("step edge column, flat: A " << flatA << ", B " << flatB);
    MESSAGE("step edge column, displaced: A " << displacedA << ", B " << displacedB);
    MESSAGE("window means, flat: A " << meanOverWindow(flatPixels, kBandA) << ", B "
                                     << meanOverWindow(flatPixels, kBandB));
    MESSAGE("window means, displaced: A " << meanOverWindow(displacedPixels, kBandA) << ", B "
                                          << meanOverWindow(displacedPixels, kBandB));

    // **The control.** With no height map the two shells must put the edge in
    // the same column: same geometry, same texels, one chart read backwards,
    // and a step that is symmetric about `u = 0.5`. If this fails the asset is
    // wrong and nothing below means anything.
    REQUIRE(flatA > 0.0);
    REQUIRE(flatB > 0.0);
    CHECK(std::abs(flatA - flatB) < 3.0);

    const double shiftA = displacedA - flatA;
    const double shiftB = displacedB - flatB;
    MESSAGE("parallax shift of the step edge, in columns: A " << shiftA << ", B " << shiftB);

    // Each shell must actually move, or the case is measuring nothing.
    REQUIRE(displacedA > 0.0);
    REQUIRE(displacedB > 0.0);
    CHECK(std::abs(shiftA) > 4.0);

    // **The absolute direction.** Toward the near side; see the header.
    CHECK(shiftA < 0.0);
    CHECK(shiftB < 0.0);

    // **The mirror.** Same screen column, within a pixel or two of filtering.
    CHECK(std::abs(displacedA - displacedB) < 3.0);

    tearDown(device);
}
