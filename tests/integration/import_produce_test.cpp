// An import produces engine assets (T0169, D39): the deviceless half, which
// is nearly all of it.
//
// Bucket: integration, because `produceEngineAssets` reads and writes through
// the VFS and the VFS needs real files mounted. **No device anywhere** — that
// is the design being exercised: identity, re-import and override survival
// must be provable without a GPU, or they would only ever be checked by hand
// in an editor.
//
// Every case here is one of D39's sentences turned falsifiable. The ones that
// matter most are the identity cases: the failure they guard against — a
// reordered material list silently repointing every scene reference — is the
// one both Godot and Unity redesigned around after shipping it.

#include <doctest/doctest.h>

#include <hp/Assets.hpp>
#include <hp/Import.hpp>
#include <hp/Material.hpp>
#include <hp/Vfs.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

/// A scratch directory mounted into the VFS for one case — `assets_test`'s
/// helper, duplicated because the suites deliberately share no library.
class MountedScratch {
public:
    explicit MountedScratch(const char* name) {
        path_ = std::filesystem::temp_directory_path() /
                ("hp_import_produce_test_" + std::string(name));
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

    void writeFile(const std::string& relative, std::string_view text) {
        const std::filesystem::path full = path_ / relative;
        std::error_code ec;
        std::filesystem::create_directories(full.parent_path(), ec);
        std::ofstream file(full, std::ios::binary);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    void writeBytes(const std::string& relative, const std::vector<std::uint8_t>& bytes) {
        const std::filesystem::path full = path_ / relative;
        std::error_code ec;
        std::filesystem::create_directories(full.parent_path(), ec);
        std::ofstream file(full, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

/// A minimal .gltf whose only content is its material array — the production
/// pass reads materials and images, so a mesh is not required to exist.
std::string gltfWithMaterials(const std::string& materialsJson,
                              const std::string& extraTopLevel = {}) {
    return R"({ "asset": { "version": "2.0" },)" + extraTopLevel +
           R"( "materials": [ )" + materialsJson + R"( ] })";
}

/// Wraps a JSON string and binary blob as a GLB container, 4-byte aligned —
/// the layout `GltfPeek` slices.
std::vector<std::uint8_t> glbOf(std::string json, const std::vector<std::uint8_t>& bin) {
    while (json.size() % 4 != 0) {
        json.push_back(' ');
    }
    std::vector<std::uint8_t> padded(bin);
    while (padded.size() % 4 != 0) {
        padded.push_back(0);
    }

    std::vector<std::uint8_t> out;
    const auto push32 = [&](std::uint32_t value) {
        out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
        out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
        out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
        out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    };
    const std::uint32_t total =
        12 + 8 + static_cast<std::uint32_t>(json.size()) + 8 +
        static_cast<std::uint32_t>(padded.size());
    push32(0x46546C67U); // glTF
    push32(2);
    push32(total);
    push32(static_cast<std::uint32_t>(json.size()));
    push32(0x4E4F534AU); // JSON
    out.insert(out.end(), json.begin(), json.end());
    push32(static_cast<std::uint32_t>(padded.size()));
    push32(0x004E4942U); // BIN
    out.insert(out.end(), padded.begin(), padded.end());
    return out;
}

const hp::ImportedSubAsset* findAsset(const hp::ImportProducts& products, std::string_view kind,
                                      std::string_view key) {
    for (const auto& asset : products.assets) {
        if (asset.kind == kind && asset.key == key) {
            return &asset;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("an import produces a .hpmat per material, with registry identity (T0169.2)") {
    MountedScratch scratch("materials");
    scratch.writeFile("models/thing.gltf",
                      gltfWithMaterials(R"(
        { "name": "paint", "doubleSided": true, "alphaMode": "MASK", "alphaCutoff": 0.25,
          "pbrMetallicRoughness": { "baseColorFactor": [0.8, 0.1, 0.2, 1.0],
                                    "metallicFactor": 0.9, "roughnessFactor": 0.3 } },
        { "name": "glass", "extensions": { "KHR_materials_unlit": {} },
          "pbrMetallicRoughness": { "baseColorFactor": [0.2, 0.4, 0.9, 0.5] } }
    )"));

    const hp::ImportProducts products = hp::produceEngineAssets("models/thing.gltf");
    REQUIRE(products.ok);
    REQUIRE(products.assets.size() == 2);

    const hp::ImportedSubAsset* paint = findAsset(products, "material", "paint");
    const hp::ImportedSubAsset* glass = findAsset(products, "material", "glass");
    REQUIRE(paint != nullptr);
    REQUIRE(glass != nullptr);
    CHECK(paint->created);
    CHECK(paint->path == "models/thing.gltf.assets/paint.hpmat");
    CHECK(paint->guid.isValid());
    CHECK(paint->guid != glass->guid);

    // The generated material parses back with the authored values intact.
    const auto text = hp::Vfs::readText(paint->path);
    REQUIRE(text.has_value());
    const auto material = hp::parseMaterial(*text, paint->path);
    REQUIRE(material.has_value());
    CHECK(material->baseColour.x == doctest::Approx(0.8));
    CHECK(material->metallic == doctest::Approx(0.9));
    CHECK(material->roughness == doctest::Approx(0.3));
    CHECK(material->alphaMode == hp::AlphaMode::Mask);
    CHECK(material->alphaCutoff == doctest::Approx(0.25));
    CHECK(material->doubleSided);
    CHECK_FALSE(material->unlit);

    const auto glassText = hp::Vfs::readText(glass->path);
    REQUIRE(glassText.has_value());
    const auto glassMaterial = hp::parseMaterial(*glassText, glass->path);
    REQUIRE(glassMaterial.has_value());
    CHECK(glassMaterial->unlit);

    // The sidecar resolves to the registry identity, and the source metafile
    // carries the registry.
    const auto sidecarText = hp::Vfs::readText(hp::metaPathFor(paint->path));
    REQUIRE(sidecarText.has_value());
    const auto sidecar = hp::parseAssetMeta(*sidecarText, "sidecar");
    REQUIRE(sidecar.has_value());
    CHECK(sidecar->guid == paint->guid);
    CHECK(sidecar->type == "Material");

    const auto sourceMetaText = hp::Vfs::readText(hp::metaPathFor("models/thing.gltf"));
    REQUIRE(sourceMetaText.has_value());
    const auto sourceMeta = hp::parseAssetMeta(*sourceMetaText, "source meta");
    REQUIRE(sourceMeta.has_value());
    CHECK(sourceMeta->schemaVersion == hp::kAssetMetaVersion);
    const hp::SubAssetRecord* record = sourceMeta->findSubAsset("material", "paint");
    REQUIRE(record != nullptr);
    CHECK(record->guid == paint->guid);
}

TEST_CASE("identity survives the DCC reordering its material list (T0169.1, D39)") {
    // **The case the whole design exists for.** The same two materials, then
    // the same file re-exported with the array reversed: every GUID must
    // follow its name, and nothing may be written.
    MountedScratch scratch("reorder");
    scratch.writeFile("models/thing.gltf", gltfWithMaterials(R"(
        { "name": "paint" }, { "name": "trim" }
    )"));

    const hp::ImportProducts first = hp::produceEngineAssets("models/thing.gltf");
    REQUIRE(first.ok);
    const hp::ImportedSubAsset* paintBefore = findAsset(first, "material", "paint");
    const hp::ImportedSubAsset* trimBefore = findAsset(first, "material", "trim");
    REQUIRE(paintBefore != nullptr);
    REQUIRE(trimBefore != nullptr);

    scratch.writeFile("models/thing.gltf", gltfWithMaterials(R"(
        { "name": "trim" }, { "name": "paint" }
    )"));

    const hp::ImportProducts second = hp::produceEngineAssets("models/thing.gltf");
    REQUIRE(second.ok);
    const hp::ImportedSubAsset* paintAfter = findAsset(second, "material", "paint");
    const hp::ImportedSubAsset* trimAfter = findAsset(second, "material", "trim");
    REQUIRE(paintAfter != nullptr);
    REQUIRE(trimAfter != nullptr);

    CHECK(paintAfter->guid == paintBefore->guid);
    CHECK(trimAfter->guid == trimBefore->guid);
    // Extract-once: the reorder wrote nothing.
    CHECK_FALSE(paintAfter->created);
    CHECK_FALSE(trimAfter->created);
}

TEST_CASE("an author's edit survives re-import, and deleting the file resets it (T0169.3)") {
    MountedScratch scratch("override");
    scratch.writeFile("models/thing.gltf", gltfWithMaterials(R"(
        { "name": "paint", "pbrMetallicRoughness": { "roughnessFactor": 0.3 } }
    )"));

    const hp::ImportProducts first = hp::produceEngineAssets("models/thing.gltf");
    REQUIRE(first.ok);
    const hp::ImportedSubAsset* paint = findAsset(first, "material", "paint");
    REQUIRE(paint != nullptr);

    // The author edits the generated material.
    {
        const auto text = hp::Vfs::readText(paint->path);
        REQUIRE(text.has_value());
        auto material = hp::parseMaterial(*text, paint->path);
        REQUIRE(material.has_value());
        material->roughness = 0.77F;
        REQUIRE(hp::Vfs::writeText(paint->path, hp::writeMaterial(*material)));
    }

    // Re-import: extract-once means the edit survives **structurally** — the
    // pass never opens the file to decide.
    const hp::ImportProducts second = hp::produceEngineAssets("models/thing.gltf");
    REQUIRE(second.ok);
    {
        const auto text = hp::Vfs::readText(paint->path);
        REQUIRE(text.has_value());
        const auto material = hp::parseMaterial(*text, paint->path);
        REQUIRE(material.has_value());
        CHECK(material->roughness == doctest::Approx(0.77));
    }

    // Deleting the generated file is "reset to source": recreated from the
    // glTF, same registry GUID, so scene references survive the reset.
    REQUIRE(hp::Vfs::remove(paint->path));
    const hp::ImportProducts third = hp::produceEngineAssets("models/thing.gltf");
    REQUIRE(third.ok);
    const hp::ImportedSubAsset* recreated = findAsset(third, "material", "paint");
    REQUIRE(recreated != nullptr);
    CHECK(recreated->created);
    CHECK(recreated->guid == paint->guid);
    {
        const auto text = hp::Vfs::readText(paint->path);
        REQUIRE(text.has_value());
        const auto material = hp::parseMaterial(*text, paint->path);
        REQUIRE(material.has_value());
        CHECK(material->roughness == doctest::Approx(0.3));
    }
}

TEST_CASE("duplicate and missing names get D39's documented keys") {
    MountedScratch scratch("names");
    scratch.writeFile("models/thing.gltf", gltfWithMaterials(R"(
        { "name": "steel" }, { "name": "steel" }, {}, { "name": "b a d/key" }
    )"));

    const hp::ImportProducts products = hp::produceEngineAssets("models/thing.gltf");
    REQUIRE(products.ok);
    CHECK(findAsset(products, "material", "steel") != nullptr);
    CHECK(findAsset(products, "material", "steel_2") != nullptr);
    CHECK(findAsset(products, "material", "material_2") != nullptr);
    CHECK(findAsset(products, "material", "b_a_d_key") != nullptr);
}

TEST_CASE("embedded and external images resolve to texture GUIDs (T0169.6)") {
    MountedScratch scratch("images");

    // The embedded image's bytes are arbitrary on purpose: extraction is
    // **verbatim** — a write, never a re-encode (that boundary is T0097's) —
    // so the assertion is byte equality, not decodability.
    const std::vector<std::uint8_t> embedded{0x89, 'P', 'N', 'G', 1, 2, 3, 4, 5, 6, 7};
    const std::string json = R"({ "asset": { "version": "2.0" },
      "materials": [ { "name": "skin",
        "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } },
        "normalTexture": { "index": 1 } } ],
      "textures": [ { "source": 0, "sampler": 0 }, { "source": 1 } ],
      "samplers": [ { "wrapS": 33071, "wrapT": 33648 } ],
      "images": [ { "name": "hull", "mimeType": "image/png", "bufferView": 0 },
                  { "uri": "tex/detail.png" } ],
      "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": )" +
                             std::to_string(embedded.size()) + R"( } ],
      "buffers": [ { "byteLength": )" +
                             std::to_string(embedded.size()) + R"( } ] })";
    scratch.writeBytes("models/thing.glb", glbOf(json, embedded));
    scratch.writeFile("models/tex/detail.png", "not really a png");

    const hp::ImportProducts products = hp::produceEngineAssets("models/thing.glb");
    REQUIRE(products.ok);

    // The embedded image landed as a file, bytes verbatim, registry identity.
    const hp::ImportedSubAsset* hull = findAsset(products, "image", "hull");
    REQUIRE(hull != nullptr);
    CHECK(hull->path == "models/thing.glb.assets/hull.png");
    const auto bytes = hp::Vfs::read(hull->path);
    REQUIRE(bytes.has_value());
    REQUIRE(bytes->size() == embedded.size());
    CHECK(std::memcmp(bytes->data(), embedded.data(), embedded.size()) == 0);

    // The material references it, and the external image, by their GUIDs.
    const hp::ImportedSubAsset* skin = findAsset(products, "material", "skin");
    REQUIRE(skin != nullptr);
    const auto text = hp::Vfs::readText(skin->path);
    REQUIRE(text.has_value());
    const auto material = hp::parseMaterial(*text, skin->path);
    REQUIRE(material.has_value());
    CHECK(material->baseColourTexture == hull->guid);

    // The external image's identity is its own metafile — the registry stays
    // out of it, exactly as if the author had imported the file by hand.
    const auto detailMeta = hp::Vfs::readText(hp::metaPathFor("models/tex/detail.png"));
    REQUIRE(detailMeta.has_value());
    const auto parsed = hp::parseAssetMeta(*detailMeta, "detail meta");
    REQUIRE(parsed.has_value());
    CHECK(material->normalTexture == parsed->guid);

    // The base-colour texture's sampler reached the UV channel.
    CHECK(material->uv0.wrapU == hp::TextureWrap::Clamp);
    CHECK(material->uv0.wrapV == hp::TextureWrap::Mirror);
}

TEST_CASE("KHR_texture_transform reaches the UV channel of the generated material") {
    MountedScratch scratch("transform");
    const std::vector<std::uint8_t> embedded{1, 2, 3};
    const std::string json = R"({ "asset": { "version": "2.0" },
      "materials": [ { "name": "tiled",
        "pbrMetallicRoughness": { "baseColorTexture": { "index": 0,
          "extensions": { "KHR_texture_transform": {
            "offset": [0.25, 0.5], "scale": [2.0, 3.0], "rotation": 0.5 } } } } } ],
      "textures": [ { "source": 0 } ],
      "images": [ { "mimeType": "image/png", "bufferView": 0 } ],
      "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 3 } ],
      "buffers": [ { "byteLength": 3 } ] })";
    scratch.writeBytes("models/thing.glb", glbOf(json, embedded));

    const hp::ImportProducts products = hp::produceEngineAssets("models/thing.glb");
    REQUIRE(products.ok);
    const hp::ImportedSubAsset* tiled = findAsset(products, "material", "tiled");
    REQUIRE(tiled != nullptr);
    const auto text = hp::Vfs::readText(tiled->path);
    REQUIRE(text.has_value());
    const auto material = hp::parseMaterial(*text, tiled->path);
    REQUIRE(material.has_value());
    CHECK(material->uv0.offset.x == doctest::Approx(0.25));
    CHECK(material->uv0.offset.y == doctest::Approx(0.5));
    CHECK(material->uv0.scale.x == doctest::Approx(2.0));
    CHECK(material->uv0.scale.y == doctest::Approx(3.0));
    CHECK(material->uv0.rotation == doctest::Approx(0.5));
}

TEST_CASE("spec-gloss extracts as the migration D39 describes") {
    MountedScratch scratch("specgloss");
    scratch.writeFile("models/thing.gltf", gltfWithMaterials(R"(
        { "name": "legacy", "extensions": { "KHR_materials_pbrSpecularGlossiness": {
            "diffuseFactor": [0.1, 0.5, 0.9, 1.0], "glossinessFactor": 0.75 } } }
    )", R"( "extensionsUsed": [ "KHR_materials_pbrSpecularGlossiness" ],)"));

    const hp::ImportProducts products = hp::produceEngineAssets("models/thing.gltf");
    REQUIRE(products.ok);
    const hp::ImportedSubAsset* legacy = findAsset(products, "material", "legacy");
    REQUIRE(legacy != nullptr);
    const auto text = hp::Vfs::readText(legacy->path);
    REQUIRE(text.has_value());
    const auto material = hp::parseMaterial(*text, legacy->path);
    REQUIRE(material.has_value());
    CHECK(material->baseColour.z == doctest::Approx(0.9));
    CHECK(material->metallic == doctest::Approx(0.0));
    // `glossinessFactor` is read here although the render path's loader drops
    // it — the generated file preserves what the source said.
    CHECK(material->roughness == doctest::Approx(0.25));
}

TEST_CASE("the owner's car produces its material file, when the asset is supplied "
          "(T0169.7)") {
    // `aston_martin.glb` is not in the repository (82.3 MiB, licence decision
    // on T0167.7), so this case runs only when `HP_ASTON_GLB` names the file
    // — the owner-approved copy, opted in explicitly, never discovered by
    // scanning. On CI and on any machine without the variable it passes as a
    // skip. The asset is copied into the scratch mount first: the production
    // pass writes sidecars and an `.assets/` directory beside its source, and
    // those belong in a project tree, not in the owner's download folder.
    const char* assetPath = std::getenv("HP_ASTON_GLB");
    if (assetPath == nullptr || !std::filesystem::exists(assetPath)) {
        MESSAGE("HP_ASTON_GLB not set (or not a file); skipping the car");
        return;
    }

    MountedScratch scratch("aston");
    {
        std::error_code ec;
        std::filesystem::create_directories(scratch.path() / "models", ec);
        std::filesystem::copy_file(assetPath, scratch.path() / "models" / "aston_martin.glb",
                                   std::filesystem::copy_options::overwrite_existing, ec);
        REQUIRE_FALSE(ec);
    }

    const hp::ImportProducts products = hp::produceEngineAssets("models/aston_martin.glb");
    REQUIRE(products.ok);
    for (const auto& asset : products.assets) {
        const std::string state = asset.created ? "created" : "existing";
        MESSAGE("produced " << asset.kind << "/" << asset.key << " -> " << asset.path
                            << " guid " << asset.guid.toString() << " (" << state << ")");
    }

    // T0167's container notes: one material, four embedded PNGs.
    const auto materialCount =
        std::count_if(products.assets.begin(), products.assets.end(),
                      [](const hp::ImportedSubAsset& a) { return a.kind == "material"; });
    const auto imageCount =
        std::count_if(products.assets.begin(), products.assets.end(),
                      [](const hp::ImportedSubAsset& a) { return a.kind == "image"; });
    CHECK(materialCount == 1);
    CHECK(imageCount == 4);

    // **The Done-when, verbatim: the Aston Martin's material is a file with a
    // GUID.** And it parses back as the spec-gloss migration D39 describes.
    const hp::ImportedSubAsset* material = nullptr;
    for (const auto& asset : products.assets) {
        if (asset.kind == "material") {
            material = &asset;
        }
    }
    REQUIRE(material != nullptr);
    CHECK(material->guid.isValid());
    const auto text = hp::Vfs::readText(material->path);
    REQUIRE(text.has_value());
    const auto parsed = hp::parseMaterial(*text, material->path);
    REQUIRE(parsed.has_value());
    CHECK(parsed->baseColourTexture.isValid());
    CHECK(parsed->alphaMode == hp::AlphaMode::Blend);
    CHECK(parsed->doubleSided);
}

TEST_CASE("a version-1 metafile parses and upgrades without changing identity") {
    MountedScratch scratch("v1meta");
    scratch.writeFile("models/thing.gltf", gltfWithMaterials(R"( { "name": "paint" } )"));
    scratch.writeFile("models/thing.gltf.hpmeta",
                      "version: 1\nguid: 00000000deadbeef\ntype: Mesh\nsource: "
                      "models/thing.gltf\n");

    const hp::ImportProducts products = hp::produceEngineAssets("models/thing.gltf");
    REQUIRE(products.ok);

    const auto metaText = hp::Vfs::readText("models/thing.gltf.hpmeta");
    REQUIRE(metaText.has_value());
    const auto meta = hp::parseAssetMeta(*metaText, "meta");
    REQUIRE(meta.has_value());
    // The source's own GUID survives the upgrade; the registry arrives beside
    // it rather than replacing it.
    CHECK(meta->guid.toString() == "00000000deadbeef");
    CHECK(meta->schemaVersion == hp::kAssetMetaVersion);
    CHECK(meta->findSubAsset("material", "paint") != nullptr);
}
