// Extension dispatch (T0023.2).
//
// Bucket: fast — deciding what a file *is* from its name needs no device and no
// filesystem. Actually loading one is the gpu bucket's job.

#include <doctest/doctest.h>

#include <hp/Assets.hpp>

TEST_CASE("extensions dispatch to the right kind") {
    CHECK(hp::assetKindForPath("textures/wood.png") == hp::AssetKind::Texture);
    CHECK(hp::assetKindForPath("a.jpg") == hp::AssetKind::Texture);
    CHECK(hp::assetKindForPath("a.jpeg") == hp::AssetKind::Texture);
    CHECK(hp::assetKindForPath("a.tga") == hp::AssetKind::Texture);
    CHECK(hp::assetKindForPath("a.dds") == hp::AssetKind::Texture);
    CHECK(hp::assetKindForPath("a.ktx") == hp::AssetKind::Texture);
    CHECK(hp::assetKindForPath("a.hdr") == hp::AssetKind::Texture);

    CHECK(hp::assetKindForPath("meshes/barrel.gltf") == hp::AssetKind::Mesh);
    CHECK(hp::assetKindForPath("meshes/barrel.glb") == hp::AssetKind::Mesh);
}

TEST_CASE("the extension check is case-insensitive") {
    // A file named .PNG is the same file. Windows produces these routinely, and
    // an importer that skipped them would look like a corrupt asset folder.
    CHECK(hp::assetKindForPath("A.PNG") == hp::AssetKind::Texture);
    CHECK(hp::assetKindForPath("A.Gltf") == hp::AssetKind::Mesh);
    CHECK(hp::assetKindForPath("a.TgA") == hp::AssetKind::Texture);
}

TEST_CASE("anything else is Unknown rather than guessed at") {
    CHECK(hp::assetKindForPath("readme.txt") == hp::AssetKind::Unknown);
    CHECK(hp::assetKindForPath("noextension") == hp::AssetKind::Unknown);
    CHECK(hp::assetKindForPath("") == hp::AssetKind::Unknown);
    CHECK(hp::assetKindForPath("trailing.") == hp::AssetKind::Unknown);
    CHECK(hp::assetKindForPath("a.png.hpmeta") == hp::AssetKind::Unknown);
}

TEST_CASE("a dot in a directory name is not an extension") {
    // "v1.2/model" has no extension. Reading backwards for a dot without
    // checking for a later slash finds one, and the importer would then try to
    // decode a directory-name fragment as a format.
    CHECK(hp::assetKindForPath("v1.2/model") == hp::AssetKind::Unknown);
    CHECK(hp::assetKindForPath("v1.2/model.png") == hp::AssetKind::Texture);
}

TEST_CASE("kind names match the pool's type names") {
    // The metafile's `type` field and the pool's key have to agree, or an asset
    // stores under one name and is looked up under another -- which presents as
    // a load that succeeded and an asset that cannot be found.
    CHECK(hp::assetKindName(hp::AssetKind::Texture) == hp::AssetTraits<hp::TextureAsset>::name);
    CHECK(hp::assetKindName(hp::AssetKind::Unknown) == "Unknown");
    CHECK_FALSE(hp::assetKindName(hp::AssetKind::Mesh).empty());
}
