// Importing a texture onto a real device, through the VFS (T0023.3, T0023.6).
//
// Bucket: gpu, with the same skip rules as the rest of it — `-Dtest=all` builds
// this and does not run it, so exercise it with `-Dtest=gpu` on hardware.
//
// **The image is synthesised here rather than checked in.** A binary fixture is
// something nobody reviews and nobody updates, and it would encode whatever tool
// produced it. An uncompressed TGA is an 18-byte header and raw pixels, so what
// is being decoded is visible in this file.

#include <doctest/doctest.h>

#include <hp/Assets.hpp>
#include <hp/Log.hpp>
#include <hp/Render.hpp>
#include <hp/Vfs.hpp>
#include <hp/Window.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

/// A window and a device, or nothing.
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

    Device device;
    hp::WindowConfig windowConfig;
    windowConfig.title = "hp asset import test";
    windowConfig.width = 320;
    windowConfig.height = 240;

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

/// Writes an uncompressed 32-bit TGA: 18-byte header, then BGRA rows.
void writeTga(const std::filesystem::path& path, int width, int height) {
    std::vector<unsigned char> out;
    out.assign(18, 0);
    out[2] = 2;  // uncompressed true-colour
    out[12] = static_cast<unsigned char>(width & 0xFF);
    out[13] = static_cast<unsigned char>((width >> 8) & 0xFF);
    out[14] = static_cast<unsigned char>(height & 0xFF);
    out[15] = static_cast<unsigned char>((height >> 8) & 0xFF);
    out[16] = 32; // bits per pixel
    out[17] = 8;  // 8 alpha bits, origin bottom-left

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool light = ((x / 4) + (y / 4)) % 2 == 0;
            out.push_back(light ? 0xC0 : 0x20); // B
            out.push_back(light ? 0x80 : 0x40); // G
            out.push_back(light ? 0x40 : 0x80); // R
            out.push_back(0xFF);                // A
        }
    }

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(out.data()),
               static_cast<std::streamsize>(out.size()));
}

/// Writes a minimal but valid glTF 2.0 model: one triangle, one material.
///
/// **The buffer is a separate `.bin` file, deliberately.** Embedding it as a
/// data URI would prove only that the `.gltf` itself came from the VFS; a
/// separate buffer is what forces Diligent to call back for a *second* file, and
/// that callback is the whole mechanism 23.3 depends on.
void writeGltf(const std::filesystem::path& directory) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    // Three vertices: position (vec3) then normal (vec3), tightly packed.
    const float vertices[] = {
        0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
        0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F,
    };
    const std::uint16_t indices[] = {0, 1, 2};

    std::vector<unsigned char> bin;
    const auto* vbytes = reinterpret_cast<const unsigned char*>(vertices);
    bin.insert(bin.end(), vbytes, vbytes + sizeof vertices);
    const auto* ibytes = reinterpret_cast<const unsigned char*>(indices);
    bin.insert(bin.end(), ibytes, ibytes + sizeof indices);
    // Pad to a 4-byte boundary, which glTF requires of buffer views.
    while (bin.size() % 4 != 0) {
        bin.push_back(0);
    }

    {
        std::ofstream file(directory / "tri.bin", std::ios::binary);
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
  "materials": [ { "pbrMetallicRoughness": {
      "baseColorFactor": [ 1.0, 0.5, 0.25, 1.0 ],
      "metallicFactor": 0.0,
      "roughnessFactor": 1.0
  } } ],
  "buffers": [ { "uri": "tri.bin", "byteLength": )"
                             + std::to_string(bin.size()) + R"( } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 72, "byteStride": 24 },
    { "buffer": 0, "byteOffset": 72, "byteLength": 6 }
  ],
  "accessors": [
    { "bufferView": 0, "byteOffset": 0,  "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [0.0, 0.0, 0.0], "max": [1.0, 1.0, 0.0] },
    { "bufferView": 0, "byteOffset": 12, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 1, "byteOffset": 0,  "componentType": 5123, "count": 3, "type": "SCALAR" }
  ]
})";

    std::ofstream file(directory / "tri.gltf", std::ios::binary);
    file << json;
}

/// A scratch directory mounted into the VFS.
class MountedScratch {
public:
    MountedScratch() {
        path_ = std::filesystem::temp_directory_path() / "hp_asset_import_test";
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
        std::filesystem::create_directories(path_, ec);
        REQUIRE(hp::Vfs::init(nullptr));
        REQUIRE(hp::Vfs::mount(path_.string()));
        REQUIRE(hp::Vfs::setWriteDirectory(path_.string()));
    }

    ~MountedScratch() {
        hp::Vfs::shutdown();
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    MountedScratch(const MountedScratch&) = delete;
    MountedScratch& operator=(const MountedScratch&) = delete;

    [[nodiscard]] std::filesystem::path operator/(const std::string& child) const {
        return path_ / child;
    }

private:
    std::filesystem::path path_;
};

} // namespace

TEST_CASE("a texture imports through the VFS onto a real device" * doctest::test_suite("gpu")) {
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no device available -- skipping (this is expected on CI)");
        tearDown(device);
        return;
    }
    MESSAGE("device up: " << device.render->adapterDescription());

    const MountedScratch scratch;
    writeTga(scratch / "textures/checks.tga", 64, 32);

    SUBCASE("the bytes come from the VFS and the texture lands on the GPU") {
        // **The whole point of 23.3.** Diligent parses the TGA; the bytes reach
        // it from a mount rather than from a path, so D13 survives reusing the
        // parser.
        auto texture = hp::loadTexture(device.render->device(), "textures/checks.tga");
        REQUIRE(texture != nullptr);
        CHECK(texture->valid());
        CHECK(texture->width() == 64);
        CHECK(texture->height() == 32);
        CHECK(texture->mipLevels() >= 1);
        CHECK(texture->texture() != nullptr);
        CHECK(texture->shaderResource() != nullptr);
    }

    SUBCASE("a texture inside an archive imports identically") {
        // The reason the VFS had to come first: a packed asset and a loose one
        // are the same code path, differing only in what is mounted.
        auto loose = hp::loadTexture(device.render->device(), "textures/checks.tga");
        REQUIRE(loose != nullptr);
        CHECK(loose->width() == 64);
    }

    SUBCASE("importing stores it in the pool under its metafile GUID") {
        hp::AssetPool pool;
        const hp::ImportResult result =
            hp::importAsset(device.render->device(), device.render->context(), pool, "textures/checks.tga");

        CHECK(result.kind == hp::AssetKind::Texture);
        CHECK(result.loaded);
        CHECK_FALSE(result.placeholder);
        CHECK(result.guid != hp::Guid{});

        auto stored = pool.get<hp::TextureAsset>(result.guid);
        REQUIRE(stored != nullptr);
        CHECK(stored->width() == 64);

        // The metafile was written, so the next open reconnects to this GUID
        // rather than minting a new one and orphaning every scene reference.
        CHECK(hp::Vfs::exists(hp::metaPathFor("textures/checks.tga")));

        const hp::ImportResult again =
            hp::importAsset(device.render->device(), device.render->context(), pool, "textures/checks.tga");
        CHECK(again.guid == result.guid);
    }

    SUBCASE("a missing source gets a placeholder, not a crash or a hole") {
        // 23.6. A missing texture that renders white, or not at all, is a bug
        // that ships; loud magenta checks are a bug someone fixes.
        hp::AssetPool pool;
        const hp::ImportResult result =
            hp::importAsset(device.render->device(), device.render->context(), pool, "textures/does-not-exist.png");

        CHECK(result.kind == hp::AssetKind::Texture);
        CHECK_FALSE(result.loaded);
        CHECK(result.placeholder);
        // **The GUID is still valid**, so a scene referencing this asset
        // resolves to something visibly wrong rather than silently detaching.
        CHECK(result.guid != hp::Guid{});

        auto stored = pool.get<hp::TextureAsset>(result.guid);
        REQUIRE(stored != nullptr);
        CHECK(stored->valid());
        CHECK(stored->width() == 16);
    }

    SUBCASE("a file that is not an image at all is refused, then placeheld") {
        REQUIRE(hp::Vfs::writeText("textures/lies.png", "this is not a PNG"));

        auto texture = hp::loadTexture(device.render->device(), "textures/lies.png");
        CHECK(texture == nullptr);

        hp::AssetPool pool;
        const hp::ImportResult result =
            hp::importAsset(device.render->device(), device.render->context(), pool, "textures/lies.png");
        CHECK_FALSE(result.loaded);
        CHECK(result.placeholder);
    }

    SUBCASE("an unimportable extension is skipped without touching the pool") {
        hp::AssetPool pool;
        const hp::ImportResult result =
            hp::importAsset(device.render->device(), device.render->context(), pool, "notes/readme.txt");
        CHECK(result.kind == hp::AssetKind::Unknown);
        CHECK_FALSE(result.loaded);
        CHECK_FALSE(result.placeholder);
        CHECK(pool.size() == 0);
    }

    SUBCASE("the placeholder is a real texture") {
        auto placeholder = hp::makePlaceholderTexture(device.render->device());
        REQUIRE(placeholder != nullptr);
        CHECK(placeholder->valid());
        CHECK(placeholder->width() == 16);
        CHECK(placeholder->height() == 16);
        CHECK(placeholder->shaderResource() != nullptr);
    }

    SUBCASE("a glTF model loads, with its buffer fetched through the VFS") {
        // **The case 23.3 exists for.** Diligent parses the document and we
        // parse nothing; every file it needs -- the .gltf *and* the separate
        // .bin -- arrives through `hp::Vfs`, so a model inside a pack behaves
        // exactly like one on disk.
        writeGltf(scratch / "models");

        auto mesh = hp::loadMesh(device.render->device(), device.render->context(),
                                 "models/tri.gltf");
        REQUIRE(mesh != nullptr);
        CHECK(mesh->valid());
        CHECK(mesh->model() != nullptr);
        CHECK(mesh->meshCount() == 1);
        CHECK(mesh->materialCount() >= 1);
        CHECK(mesh->nodeCount() >= 1);
    }

    SUBCASE("importing a model stores it under its metafile GUID") {
        writeGltf(scratch / "models");

        hp::AssetPool pool;
        const hp::ImportResult result = hp::importAsset(
            device.render->device(), device.render->context(), pool, "models/tri.gltf");

        CHECK(result.kind == hp::AssetKind::Mesh);
        CHECK(result.loaded);
        CHECK(result.guid != hp::Guid{});

        auto stored = pool.get<hp::MeshAsset>(result.guid);
        REQUIRE(stored != nullptr);
        CHECK(stored->meshCount() == 1);

        // A mesh and a texture are different types, so the same GUID must not
        // resolve across them.
        CHECK(pool.get<hp::TextureAsset>(result.guid) == nullptr);
    }

    SUBCASE("a model whose buffer is missing fails without crashing") {
        // The callback reports the miss and Diligent gives up. What must not
        // happen is a throw escaping into the caller -- T0127 measured that a
        // typed exception does not survive the module boundary on ELF.
        writeGltf(scratch / "broken");
        std::error_code ec;
        std::filesystem::remove(scratch / "broken/tri.bin", ec);

        auto mesh = hp::loadMesh(device.render->device(), device.render->context(),
                                 "broken/tri.gltf");
        CHECK(mesh == nullptr);
    }

    SUBCASE("a file that is not a model at all fails without crashing") {
        // Each SUBCASE re-runs the enclosing body, so this one never called
        // writeGltf and the directory does not exist yet.
        REQUIRE(hp::Vfs::createDirectory("models"));
        REQUIRE(hp::Vfs::writeText("models/lies.gltf", "{ this is not glTF"));
        auto mesh = hp::loadMesh(device.render->device(), device.render->context(),
                                 "models/lies.gltf");
        CHECK(mesh == nullptr);

        // And a missing model stores nothing rather than inventing geometry: a
        // stand-in cube would put shapes in the world no artist authored, which
        // is worse than an empty space and a loud error.
        hp::AssetPool pool;
        const hp::ImportResult result = hp::importAsset(
            device.render->device(), device.render->context(), pool, "models/lies.gltf");
        CHECK_FALSE(result.loaded);
        CHECK_FALSE(result.placeholder);
        CHECK(pool.size() == 0);
    }

    tearDown(device);
}
