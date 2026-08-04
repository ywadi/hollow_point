// The asset pool and metafiles (T0023.1, T0023.4, T0023.6).
//
// Bucket: integration, because `loadOrCreateAssetMeta` reads through the VFS and
// the VFS needs real files mounted.
//
// **Import (23.2/23.3) is not covered here and does not exist yet** — it
// delegates to Diligent's glTF and texture loaders, which need a device. What is
// covered is everything underneath: identity, the metafile round-trip, and what
// happens when a metafile is absent, stale or corrupt.

#include <doctest/doctest.h>

#include <hp/Assets.hpp>
#include <hp/Vfs.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace {

/// Two distinct asset types, so the pool's per-type storage can be exercised.
struct FakeMesh {
    int triangles = 0;
};

struct FakeTexture {
    int width = 0;
};

} // namespace

template <>
struct hp::AssetTraits<FakeMesh> {
    static constexpr const char* name = "TestMesh";
};

template <>
struct hp::AssetTraits<FakeTexture> {
    static constexpr const char* name = "TestTexture";
};

namespace {

/// A scratch directory mounted into the VFS for one case.
class MountedScratch {
public:
    explicit MountedScratch(const char* name) {
        path_ = std::filesystem::temp_directory_path() / ("hp_assets_test_" + std::string(name));
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

    void writeFile(const std::string& relative, const std::string& text) const {
        const auto full = path_ / relative;
        std::error_code ec;
        std::filesystem::create_directories(full.parent_path(), ec);
        std::ofstream out(full, std::ios::binary);
        out << text;
    }

private:
    std::filesystem::path path_;
};

} // namespace

TEST_CASE("a metafile round-trips") {
    hp::AssetMeta meta;
    meta.guid = hp::Guid::generate();
    meta.sourcePath = "meshes/barrel.gltf";
    meta.type = "TestMesh";
    meta.schemaVersion = hp::kAssetMetaVersion;

    const std::string yaml = hp::writeAssetMeta(meta);
    const auto parsed = hp::parseAssetMeta(yaml);
    REQUIRE(parsed.has_value());

    CHECK(parsed->guid == meta.guid);
    CHECK(parsed->sourcePath == meta.sourcePath);
    CHECK(parsed->type == meta.type);
    CHECK(parsed->schemaVersion == meta.schemaVersion);
}

TEST_CASE("a metafile is human-readable and hand-editable") {
    // The whole argument for YAML as the source of truth. If a metafile were
    // opaque, a moved asset could only be repaired by reimporting, which mints
    // a new GUID and orphans every scene reference to it.
    hp::AssetMeta meta;
    meta.guid = hp::Guid::generate();
    meta.sourcePath = "textures/wood.png";
    meta.type = "TestTexture";

    const std::string yaml = hp::writeAssetMeta(meta);
    CHECK(yaml.find("source: textures/wood.png") != std::string::npos);
    CHECK(yaml.find(meta.guid.toString()) != std::string::npos);
    CHECK(yaml.find("type: TestTexture") != std::string::npos);
}

TEST_CASE("a corrupt or incomplete metafile is refused, not half-read") {
    // Every one of these means "reimport". A metafile that parsed into a
    // half-populated struct would give an asset a default GUID, which is the
    // one outcome that silently breaks every scene referencing it.
    CHECK_FALSE(hp::parseAssetMeta("").has_value());
    CHECK_FALSE(hp::parseAssetMeta("not: yaml: at: all: [").has_value());
    CHECK_FALSE(hp::parseAssetMeta("version: 1\n").has_value());
    CHECK_FALSE(hp::parseAssetMeta("version: 1\nguid: not-a-guid\n").has_value());

    // A GUID but no type or source.
    const std::string guid = hp::Guid::generate().toString();
    CHECK_FALSE(hp::parseAssetMeta("version: 1\nguid: " + guid + "\n").has_value());
    CHECK_FALSE(
        hp::parseAssetMeta("version: 1\nguid: " + guid + "\ntype: TestMesh\n").has_value());

    // A future schema version is refused rather than guessed at (T0082).
    CHECK_FALSE(hp::parseAssetMeta("version: 999\nguid: " + guid
                                   + "\ntype: TestMesh\nsource: a.gltf\n")
                    .has_value());

    // Version 0 means the field was absent.
    CHECK_FALSE(
        hp::parseAssetMeta("guid: " + guid + "\ntype: TestMesh\nsource: a.gltf\n").has_value());
}

TEST_CASE("the metafile path appends rather than replaces the extension") {
    // Replacing would give `mesh.gltf` and `mesh.png` the same metafile, and
    // therefore the same GUID -- two different assets with one identity.
    CHECK(hp::metaPathFor("meshes/barrel.gltf") == "meshes/barrel.gltf.hpmeta");
    CHECK(hp::metaPathFor("textures/barrel.png") == "textures/barrel.png.hpmeta");
    CHECK(hp::metaPathFor("meshes/barrel.gltf") != hp::metaPathFor("textures/barrel.png"));
}

TEST_CASE("a missing metafile mints a stable identity rather than failing") {
    // The ordinary state of a file someone just dropped into the project.
    const MountedScratch scratch("missing");
    scratch.writeFile("meshes/new.gltf", "pretend this is a mesh");

    const hp::AssetMeta meta = hp::loadOrCreateAssetMeta("meshes/new.gltf", "TestMesh");
    CHECK(meta.guid != hp::Guid{});
    CHECK(meta.sourcePath == "meshes/new.gltf");
    CHECK(meta.type == "TestMesh");
    CHECK(meta.schemaVersion == hp::kAssetMetaVersion);
}

TEST_CASE("an existing metafile keeps the GUID across reopens") {
    // This is what makes a project reopenable: the GUID a scene references must
    // survive, or every reference in every scene breaks.
    const MountedScratch scratch("stable");
    scratch.writeFile("meshes/barrel.gltf", "mesh bytes");

    const hp::AssetMeta first = hp::loadOrCreateAssetMeta("meshes/barrel.gltf", "TestMesh");
    REQUIRE(hp::Vfs::writeText(hp::metaPathFor("meshes/barrel.gltf"), hp::writeAssetMeta(first)));

    const hp::AssetMeta second = hp::loadOrCreateAssetMeta("meshes/barrel.gltf", "TestMesh");
    CHECK(second.guid == first.guid);
    CHECK(second.sourcePath == first.sourcePath);
}

TEST_CASE("a metafile that disagrees about type keeps the GUID") {
    // Minting a new GUID here would orphan every scene reference to the asset,
    // which is a far worse outcome than a corrected type field.
    const MountedScratch scratch("retype");
    scratch.writeFile("thing.gltf", "bytes");

    hp::AssetMeta stored;
    stored.guid = hp::Guid::generate();
    stored.sourcePath = "thing.gltf";
    stored.type = "TestTexture";
    stored.schemaVersion = hp::kAssetMetaVersion;
    REQUIRE(hp::Vfs::writeText(hp::metaPathFor("thing.gltf"), hp::writeAssetMeta(stored)));

    const hp::AssetMeta loaded = hp::loadOrCreateAssetMeta("thing.gltf", "TestMesh");
    CHECK(loaded.guid == stored.guid);
    CHECK(loaded.type == "TestMesh");
}

TEST_CASE("a corrupt metafile on disk yields a fresh identity rather than a crash") {
    const MountedScratch scratch("corrupt");
    scratch.writeFile("broken.gltf", "bytes");
    scratch.writeFile("broken.gltf.hpmeta", "this is not: [valid yaml\n");

    const hp::AssetMeta meta = hp::loadOrCreateAssetMeta("broken.gltf", "TestMesh");
    CHECK(meta.guid != hp::Guid{});
    CHECK(meta.type == "TestMesh");
}

TEST_CASE("the pool stores and retrieves by GUID") {
    hp::AssetPool pool;
    const hp::Guid guid = hp::Guid::generate();

    auto mesh = std::make_shared<FakeMesh>();
    mesh->triangles = 1200;
    pool.store(guid, mesh);

    CHECK(pool.size() == 1);
    CHECK(pool.contains<FakeMesh>(guid));

    const auto found = pool.get<FakeMesh>(guid);
    REQUIRE(found != nullptr);
    CHECK(found->triangles == 1200);
}

TEST_CASE("types do not collide, even on the same GUID") {
    // Not a situation to design for, but one that must not silently return the
    // wrong object -- which is exactly what a GUID-only key would do.
    hp::AssetPool pool;
    const hp::Guid shared = hp::Guid::generate();

    pool.store(shared, std::make_shared<FakeMesh>(FakeMesh{7}));
    pool.store(shared, std::make_shared<FakeTexture>(FakeTexture{512}));

    CHECK(pool.size() == 2);
    REQUIRE(pool.get<FakeMesh>(shared) != nullptr);
    REQUIRE(pool.get<FakeTexture>(shared) != nullptr);
    CHECK(pool.get<FakeMesh>(shared)->triangles == 7);
    CHECK(pool.get<FakeTexture>(shared)->width == 512);
}

TEST_CASE("an absent asset is nullptr, not undefined") {
    hp::AssetPool pool;
    const hp::Guid guid = hp::Guid::generate();

    CHECK(pool.get<FakeMesh>(guid) == nullptr);
    CHECK_FALSE(pool.contains<FakeMesh>(guid));

    // Stored as one type, asked for as another.
    pool.store(guid, std::make_shared<FakeTexture>());
    CHECK(pool.get<FakeMesh>(guid) == nullptr);
    CHECK(pool.get<FakeTexture>(guid) != nullptr);
}

TEST_CASE("storing again replaces, which is what a hot reload is") {
    hp::AssetPool pool;
    const hp::Guid guid = hp::Guid::generate();

    auto original = std::make_shared<FakeMesh>(FakeMesh{1});
    pool.store(guid, original);
    pool.store(guid, std::make_shared<FakeMesh>(FakeMesh{2}));

    CHECK(pool.size() == 1);
    CHECK(pool.get<FakeMesh>(guid)->triangles == 2);

    // The caller holding the old pointer still has the old object -- which is
    // the property that makes a reload safe mid-frame (T0058).
    CHECK(original->triangles == 1);
}

TEST_CASE("storing null removes") {
    hp::AssetPool pool;
    const hp::Guid guid = hp::Guid::generate();
    pool.store(guid, std::make_shared<FakeMesh>());
    REQUIRE(pool.size() == 1);

    pool.store<FakeMesh>(guid, nullptr);
    CHECK(pool.size() == 0);
    CHECK(pool.get<FakeMesh>(guid) == nullptr);
}

TEST_CASE("an asset outlives the pool when someone still holds it") {
    // Shared ownership is the point: gameplay can hold an asset across a scene
    // load, and a pool teardown must not pull it out from under them.
    std::shared_ptr<FakeMesh> held;
    {
        hp::AssetPool pool;
        auto mesh = std::make_shared<FakeMesh>(FakeMesh{99});
        pool.store(hp::Guid::generate(), mesh);
        held = mesh;
    }
    REQUIRE(held != nullptr);
    CHECK(held->triangles == 99);
}

TEST_CASE("removal and clearing report honestly") {
    hp::AssetPool pool;
    const hp::Guid a = hp::Guid::generate();
    const hp::Guid b = hp::Guid::generate();
    pool.store(a, std::make_shared<FakeMesh>());
    pool.store(b, std::make_shared<FakeMesh>());

    CHECK(pool.remove<FakeMesh>(a));
    CHECK_FALSE(pool.remove<FakeMesh>(a));
    CHECK(pool.size() == 1);

    pool.clear();
    CHECK(pool.size() == 0);
    CHECK_FALSE(pool.remove<FakeMesh>(b));
}

TEST_CASE("assets can be listed by type") {
    hp::AssetPool pool;
    const hp::Guid mesh1 = hp::Guid::generate();
    const hp::Guid mesh2 = hp::Guid::generate();
    const hp::Guid texture = hp::Guid::generate();

    pool.store(mesh1, std::make_shared<FakeMesh>());
    pool.store(mesh2, std::make_shared<FakeMesh>());
    pool.store(texture, std::make_shared<FakeTexture>());

    const auto meshes = pool.guidsOfType("TestMesh");
    CHECK(meshes.size() == 2);
    const auto textures = pool.guidsOfType("TestTexture");
    CHECK(textures.size() == 1);
    CHECK(textures[0] == texture);
    CHECK(pool.guidsOfType("NothingLikeThis").empty());
}
