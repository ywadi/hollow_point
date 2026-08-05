// Material assets: parameters, the document, and hand-authoring one (T0060.1).
//
// Bucket: fast. A material owns no GPU resources by design — the textures it
// names are separate assets — so everything about what a material *is* is
// testable with no device. What a material *looks like* needs one and belongs to
// the gpu bucket with 60.11's textured-render guard.

#include <doctest/doctest.h>

#include <hp/Assets.hpp>
#include <hp/Material.hpp>
#include <hp/Reflect.hpp>
#include <hp/Serialize.hpp>
#include <hp/Yaml.hpp>

#include <string>

namespace {

/// A material with every field moved off its default, so a round trip that
/// silently drops one is visible rather than accidentally correct.
hp::Material populated() {
    hp::Material material;
    material.baseColour = hp::float4{0.25F, 0.5F, 0.75F, 0.5F};
    material.emissive = hp::float3{1.0F, 0.5F, 0.0F};
    material.metallic = 0.125F;
    material.roughness = 0.375F;
    material.normalScale = 2.0F;
    material.occlusionStrength = 0.25F;
    material.alphaMode = hp::AlphaMode::Mask;
    material.alphaCutoff = 0.75F;
    material.doubleSided = true;
    material.unlit = true;
    material.baseColourTexture = hp::Guid{0x4f8a12c0d3e5b678ULL};
    material.metallicRoughnessTexture = hp::Guid{0x91aa2b3c4d5e6f70ULL};
    material.normalTexture = hp::Guid{0xc3d4e5f60718293aULL};
    material.occlusionTexture = hp::Guid{0x0102030405060708ULL};
    material.emissiveTexture = hp::Guid{0xa1b2c3d4e5f60718ULL};
    material.uv0.scale = hp::float2{2.0F, 4.0F};
    material.uv0.offset = hp::float2{0.125F, 0.25F};
    material.uv0.rotation = 1.5F;
    material.uv0.wrapU = hp::TextureWrap::Mirror;
    material.uv1.wrapU = hp::TextureWrap::Clamp;
    material.uv1.wrapV = hp::TextureWrap::Clamp;
    material.occlusionUv = 1;
    material.emissiveUv = 1;
    return material;
}

void checkSame(const hp::Material& actual, const hp::Material& expected) {
    CHECK(actual.baseColour.x == doctest::Approx(expected.baseColour.x));
    CHECK(actual.baseColour.w == doctest::Approx(expected.baseColour.w));
    CHECK(actual.emissive.y == doctest::Approx(expected.emissive.y));
    CHECK(actual.metallic == doctest::Approx(expected.metallic));
    CHECK(actual.roughness == doctest::Approx(expected.roughness));
    CHECK(actual.normalScale == doctest::Approx(expected.normalScale));
    CHECK(actual.occlusionStrength == doctest::Approx(expected.occlusionStrength));
    CHECK(actual.alphaMode == expected.alphaMode);
    CHECK(actual.alphaCutoff == doctest::Approx(expected.alphaCutoff));
    CHECK(actual.doubleSided == expected.doubleSided);
    CHECK(actual.unlit == expected.unlit);
    CHECK(actual.baseColourTexture == expected.baseColourTexture);
    CHECK(actual.metallicRoughnessTexture == expected.metallicRoughnessTexture);
    CHECK(actual.normalTexture == expected.normalTexture);
    CHECK(actual.occlusionTexture == expected.occlusionTexture);
    CHECK(actual.emissiveTexture == expected.emissiveTexture);
    CHECK(actual.uv0.scale.x == doctest::Approx(expected.uv0.scale.x));
    CHECK(actual.uv0.scale.y == doctest::Approx(expected.uv0.scale.y));
    CHECK(actual.uv0.offset.x == doctest::Approx(expected.uv0.offset.x));
    CHECK(actual.uv0.rotation == doctest::Approx(expected.uv0.rotation));
    CHECK(actual.uv0.wrapU == expected.uv0.wrapU);
    CHECK(actual.uv1.wrapU == expected.uv1.wrapU);
    CHECK(actual.uv1.wrapV == expected.uv1.wrapV);
    CHECK(actual.baseColourUv == expected.baseColourUv);
    CHECK(actual.occlusionUv == expected.occlusionUv);
    CHECK(actual.emissiveUv == expected.emissiveUv);
}

} // namespace

// --- the asset kind ---------------------------------------------------------

TEST_CASE("a .hpmat is a material, whatever its case") {
    CHECK(hp::assetKindForPath("materials/rust.hpmat") == hp::AssetKind::Material);
    CHECK(hp::assetKindForPath("RUST.HPMAT") == hp::AssetKind::Material);
    // Not `.mat`: that belongs to several other engines and to MATLAB, and
    // claiming it would make this importer wrong about other people's files.
    CHECK(hp::assetKindForPath("rust.mat") == hp::AssetKind::Unknown);
}

TEST_CASE("the kind's name is the name the pool stores under") {
    // These two disagreeing is the failure `AssetTraits` exists to prevent: a
    // metafile would record one type and the pool would key on another, and
    // every lookup would return nothing for an asset that is definitely loaded.
    CHECK(hp::assetKindName(hp::AssetKind::Material) == hp::AssetTraits<hp::Material>::name);
}

// --- defaults ---------------------------------------------------------------

TEST_CASE("the defaults are a plain white surface, and not a glowing one") {
    const hp::Material material;
    CHECK(material.baseColour.x == doctest::Approx(1.0F));
    CHECK(material.baseColour.w == doctest::Approx(1.0F));
    // **Black emissive is load-bearing.** Emissive is added on top of shading,
    // so any other default makes every material in the engine glow.
    CHECK(material.emissive.x == doctest::Approx(0.0F));
    CHECK(material.emissive.y == doctest::Approx(0.0F));
    CHECK(material.emissive.z == doctest::Approx(0.0F));
    // One, not zero: a material that sets a texture and nothing else must come
    // out as the texture says, and a factor of 1 is what leaves it alone.
    CHECK(material.metallic == doctest::Approx(1.0F));
    CHECK(material.roughness == doctest::Approx(1.0F));
    CHECK(material.alphaMode == hp::AlphaMode::Opaque);
    CHECK_FALSE(material.doubleSided);
    CHECK_FALSE(material.unlit);
    CHECK(material.baseColourTexture == hp::Guid{});
}

// --- the document -----------------------------------------------------------

TEST_CASE("a material round-trips through its document") {
    const hp::Material original = populated();
    const std::string text = hp::writeMaterial(original);

    const auto restored = hp::parseMaterial(text);
    REQUIRE(restored);
    checkSame(*restored, original);

    SUBCASE("and writing it again reproduces the document byte for byte") {
        // The same guarantee scenes have. Without it, opening a material and
        // saving it produces a diff containing no change — which is noise in
        // every review forever, and it is how a real change gets missed.
        CHECK(hp::writeMaterial(*restored) == text);
    }
}

TEST_CASE("the alpha mode is written by name, never by number") {
    // `alphaMode: 2` silently means something else the moment a value is
    // inserted into the middle of the enum. It is also the field most likely to
    // be hand-edited, which is the other half of the argument.
    hp::Material material;
    material.alphaMode = hp::AlphaMode::Blend;
    const std::string text = hp::writeMaterial(material);
    CHECK(text.find("alphaMode: Blend") != std::string::npos);
    CHECK(text.find("alphaMode: 2") == std::string::npos);
}

TEST_CASE("a material can be written by hand, with almost nothing in it") {
    // The authoring case T0139 established for scenes, applied here: a person
    // or a model types this, and every parameter it does not mention keeps its
    // default rather than becoming zero.
    const auto material = hp::parseMaterial(R"(version: 1
material:
  baseColour: [1, 0, 0, 1]
  roughness: 0.2
  alphaMode: Mask
)");
    REQUIRE(material);
    CHECK(material->baseColour.x == doctest::Approx(1.0F));
    CHECK(material->baseColour.y == doctest::Approx(0.0F));
    CHECK(material->roughness == doctest::Approx(0.2F));
    CHECK(material->alphaMode == hp::AlphaMode::Mask);
    // Absent, so still at its default — not zero, which would make an
    // unmentioned surface non-metallic by accident rather than by choice.
    CHECK(material->metallic == doctest::Approx(1.0F));
    CHECK(material->normalScale == doctest::Approx(1.0F));
    CHECK(material->emissiveTexture == hp::Guid{});
}

TEST_CASE("a version and nothing else is a valid material of pure defaults") {
    const auto material = hp::parseMaterial("version: 1\n");
    REQUIRE(material);
    CHECK(material->baseColour.x == doctest::Approx(1.0F));
    CHECK(material->alphaMode == hp::AlphaMode::Opaque);
}

TEST_CASE("a document with no version is refused rather than guessed at") {
    // A file without a version can only be guessed at, never migrated, and the
    // guess cannot be revised once files are in the wild.
    CHECK_FALSE(hp::parseMaterial("material:\n  roughness: 0.5\n"));
}

TEST_CASE("a newer schema is refused, not partially loaded") {
    // Loading the half this build understands would write the loss back on the
    // next save, which destroys work in a way nobody notices until they reopen
    // the project in the newer build.
    CHECK_FALSE(hp::parseMaterial("version: 99\nmaterial:\n  roughness: 0.5\n"));
}

TEST_CASE("reading is lenient in both directions") {
    SUBCASE("a field this build does not have is ignored, not fatal") {
        const auto material = hp::parseMaterial(R"(version: 1
material:
  roughness: 0.25
  clearcoatFactor: 0.8
)");
        REQUIRE(material);
        CHECK(material->roughness == doctest::Approx(0.25F));
    }

    SUBCASE("a malformed field leaves its target alone and does not poison the rest") {
        const auto material = hp::parseMaterial(R"(version: 1
material:
  roughness: not-a-number
  metallic: 0.5
)");
        REQUIRE(material);
        CHECK(material->roughness == doctest::Approx(1.0F));
        CHECK(material->metallic == doctest::Approx(0.5F));
    }

    SUBCASE("an alpha mode this build does not have is refused, not cast blindly") {
        // The switch that reads `AlphaMode` has no default case by design, so a
        // value from a future build must leave the field at something valid.
        const auto material = hp::parseMaterial(R"(version: 1
material:
  alphaMode: Dithered
)");
        REQUIRE(material);
        CHECK(material->alphaMode == hp::AlphaMode::Opaque);
    }
}

TEST_CASE("malformed YAML fails cleanly rather than producing an empty material") {
    CHECK_FALSE(hp::parseMaterial("\t: not: yaml: ["));
}

// --- the binary path --------------------------------------------------------

TEST_CASE("a material cooks and comes back") {
    // Materials go into a cook alongside scenes at export (Phase 8), so the
    // binary path has to carry every field the YAML path does. A type that YAML
    // can write and binary cannot shows up as a cook that silently drops a
    // field, which is exactly what this catches.
    hp::registerMaterialTypes();
    const hp::Material original = populated();

    std::vector<std::byte> payload;
    REQUIRE(hp::cookProperties(entt::forward_as_meta(original), payload));
    CHECK_FALSE(payload.empty());

    hp::Material restored;
    std::size_t cursor = 0;
    REQUIRE(hp::readCookedProperties(payload, cursor, entt::forward_as_meta(restored)));
    checkSame(restored, original);
}

// --- UV channels (T0060.1, second pass) --------------------------------------
//
// The plumbing for these was already live in the shader — `SelectUV` picks
// between UV0 and UV1 per texture slot and `TransformUV` applies the scale,
// rotation and bias — and the first cut of this asset could not express any of
// it. A parameter the renderer reads and the material cannot set is the mirror
// image of a parameter nothing reads, and just as much of a bug.

TEST_CASE("a UV channel defaults to untransformed and tiling") {
    const hp::UvChannel channel;
    CHECK(channel.scale.x == doctest::Approx(1.0F));
    CHECK(channel.scale.y == doctest::Approx(1.0F));
    CHECK(channel.offset.x == doctest::Approx(0.0F));
    CHECK(channel.rotation == doctest::Approx(0.0F));
    // Repeat, not Clamp: a surface texture tiles, and clamping one by default
    // would smear the edge pixel across everything beyond 0..1.
    CHECK(channel.wrapU == hp::TextureWrap::Repeat);
    CHECK(channel.wrapV == hp::TextureWrap::Repeat);
}

TEST_CASE("every slot samples UV0 until it is told otherwise") {
    const hp::Material material;
    CHECK(material.baseColourUv == 0);
    CHECK(material.metallicRoughnessUv == 0);
    CHECK(material.normalUv == 0);
    CHECK(material.occlusionUv == 0);
    CHECK(material.emissiveUv == 0);
}

TEST_CASE("the UV channels nest, and wrap modes are written by name") {
    hp::Material material;
    material.uv0.scale = hp::float2{4.0F, 4.0F};
    material.uv1.wrapU = hp::TextureWrap::Clamp;
    const std::string text = hp::writeMaterial(material);

    CHECK(text.find("uv0:") != std::string::npos);
    CHECK(text.find("uv1:") != std::string::npos);
    CHECK(text.find("wrapU: Clamp") != std::string::npos);
    CHECK(text.find("wrapU: 2") == std::string::npos);
}

TEST_CASE("a slot can be pointed at the second UV channel by hand") {
    // The case this exists for: a tiling base colour plus a uniquely unwrapped
    // occlusion map. One material-wide selector could not express it, which is
    // why the selector is per slot and the transform is per channel.
    const auto material = hp::parseMaterial(R"(version: 1
material:
  uv0:
    scale: [8, 8]
  uv1:
    wrapU: Clamp
    wrapV: Clamp
  occlusionUv: 1
)");
    REQUIRE(material);
    CHECK(material->uv0.scale.x == doctest::Approx(8.0F));
    CHECK(material->occlusionUv == 1);
    CHECK(material->baseColourUv == 0);
    CHECK(material->uv1.wrapU == hp::TextureWrap::Clamp);
    // Untouched by the above, and still tiling.
    CHECK(material->uv0.wrapU == hp::TextureWrap::Repeat);
}

TEST_CASE("a float2 survives the cook, which had no case for one at all") {
    // `float2` was missing from every one of the four leaf paths — write, read,
    // cook and uncook — because nothing had used one until UV channels did.
    hp::registerMaterialTypes();
    hp::UvChannel original;
    original.scale = hp::float2{3.0F, 0.5F};
    original.offset = hp::float2{-0.25F, 0.75F};
    original.wrapV = hp::TextureWrap::Mirror;

    std::vector<std::byte> payload;
    REQUIRE(hp::cookProperties(entt::forward_as_meta(original), payload));

    hp::UvChannel restored;
    std::size_t cursor = 0;
    REQUIRE(hp::readCookedProperties(payload, cursor, entt::forward_as_meta(restored)));
    CHECK(restored.scale.x == doctest::Approx(3.0F));
    CHECK(restored.scale.y == doctest::Approx(0.5F));
    CHECK(restored.offset.x == doctest::Approx(-0.25F));
    CHECK(restored.offset.y == doctest::Approx(0.75F));
    CHECK(restored.wrapV == hp::TextureWrap::Mirror);
}
