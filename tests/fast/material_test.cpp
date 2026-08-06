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

#include <memory>
#include <vector>
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
    // The module-declared half (T0160.3): one scalar, one three-component
    // value, one bound texture slot — enough that a round trip which drops the
    // arity, the order or the name is visible rather than accidentally right.
    material.params = {
        hp::MaterialParam{"referencePlane", hp::ShaderValue{{0.5F, 0.0F, 0.0F, 0.0F}, 1}},
        hp::MaterialParam{"tint", hp::ShaderValue{{1.0F, 0.5F, 0.25F, 0.0F}, 3}},
    };
    material.textures = {
        hp::MaterialTexture{"HpTexture0", hp::Guid{0x1122334455667788ULL}},
    };
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

    // The declared-parameter store (T0160.3). **Order is part of the value**:
    // it is the order the shader declares them in, which is the order an
    // inspector shows and a diff reads.
    REQUIRE(actual.params.size() == expected.params.size());
    for (std::size_t i = 0; i < expected.params.size(); ++i) {
        CHECK(actual.params[i].name == expected.params[i].name);
        CHECK(actual.params[i].value.count == expected.params[i].value.count);
        for (std::size_t c = 0; c < expected.params[i].value.count; ++c) {
            CHECK(actual.params[i].value.components[c] ==
                  doctest::Approx(expected.params[i].value.components[c]));
        }
    }
    REQUIRE(actual.textures.size() == expected.textures.size());
    for (std::size_t i = 0; i < expected.textures.size(); ++i) {
        CHECK(actual.textures[i].name == expected.textures[i].name);
        CHECK(actual.textures[i].texture == expected.textures[i].texture);
    }
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

// --- module-declared parameters (T0160.3) ------------------------------------

TEST_CASE("a shader module's parameters are carried by name, in the order authored") {
    // **The whole of 160.3 in one document.** A `.hpmat` that names parameters
    // the engine has never heard of is the point: the names belong to the
    // shader module, this file only holds values for them.
    const auto material = hp::parseMaterial(R"(version: 2
material:
  shader: 1570000000000020
  params:
    - name: referencePlane
      value: 0.75
    - name: tint
      value: [1, 0.5, 0.25]
  textures:
    - name: HpTexture0
      texture: 1122334455667788
)");
    REQUIRE(material);
    REQUIRE(material->params.size() == 2);

    // A scalar stays a scalar, and a three-component value stays three. The
    // shader's declared type is what decides how many floats reach the GPU;
    // this is what the document said, and normalising it away would rewrite a
    // person's file on the next save.
    CHECK(material->params[0].name == "referencePlane");
    CHECK(material->params[0].value.count == 1);
    CHECK(material->params[0].value.components[0] == doctest::Approx(0.75F));
    CHECK(material->params[1].name == "tint");
    CHECK(material->params[1].value.count == 3);
    CHECK(material->params[1].value.components[1] == doctest::Approx(0.5F));
    // Unwritten components are **zero, not undefined** — a `float4` parameter
    // given three numbers gets a zero fourth rather than the previous
    // material's.
    CHECK(material->params[1].value.components[3] == doctest::Approx(0.0F));

    REQUIRE(material->textures.size() == 1);
    CHECK(material->textures[0].name == "HpTexture0");
    CHECK(material->textures[0].texture == hp::Guid{0x1122334455667788ULL});
}

TEST_CASE("a version-1 material still loads, and carries no parameters") {
    // **The bump is a signal in one direction only.** Every `.hpmat` written
    // before T0160 is still valid: reading is lenient, both keys are simply
    // absent, and a material with no custom shader has nothing to say about
    // them anyway.
    const auto material = hp::parseMaterial(R"(version: 1
material:
  roughness: 0.2
)");
    REQUIRE(material);
    CHECK(material->roughness == doctest::Approx(0.2F));
    CHECK(material->params.empty());
    CHECK(material->textures.empty());
}

TEST_CASE("a malformed parameter value leaves the parameter alone") {
    // The same rule every other field has. A value that cannot be read must
    // not be reinterpreted, and must not poison the entries after it.
    const auto material = hp::parseMaterial(R"(version: 2
material:
  params:
    - name: broken
      value: not-a-number
    - name: fine
      value: 2
)");
    REQUIRE(material);
    REQUIRE(material->params.size() == 2);
    CHECK(material->params[0].name == "broken");
    CHECK(material->params[0].value.components[0] == doctest::Approx(0.0F));
    CHECK(material->params[1].name == "fine");
    CHECK(material->params[1].value.components[0] == doctest::Approx(2.0F));
}

TEST_CASE("a parameter value round-trips through the document as it was written") {
    // Writing is exact: a scalar comes back a scalar. The regression this
    // guards is a saver that normalises every value to four components, which
    // reads as a diff on every material the first time anyone saves one.
    hp::Material material;
    material.params = {
        hp::MaterialParam{"scalar", hp::ShaderValue{{0.25F, 0.0F, 0.0F, 0.0F}, 1}},
        hp::MaterialParam{"quad", hp::ShaderValue{{1.0F, 2.0F, 3.0F, 4.0F}, 4}},
    };
    const std::string document = hp::writeMaterial(material);
    const auto restored = hp::parseMaterial(document);
    REQUIRE(restored);
    REQUIRE(restored->params.size() == 2);
    CHECK(restored->params[0].value.count == 1);
    CHECK(restored->params[0].value.components[0] == doctest::Approx(0.25F));
    CHECK(restored->params[1].value.count == 4);
    CHECK(restored->params[1].value.components[3] == doctest::Approx(4.0F));
    // A scalar is written as a scalar, not as a one-element sequence.
    CHECK(document.find("value: 0.25") != std::string::npos);
}

TEST_CASE("the reserved slot names are one table, and out of range is nothing") {
    // The signature, the shader contract and the binder all spell these, so
    // they come from one place and a bad index is a null rather than a
    // fabricated name the signature does not carry.
    CHECK(std::string{hp::shaderTextureSlotName(0)} == "HpTexture0");
    CHECK(std::string{hp::shaderTextureSamplerName(0)} == "HpTexture0_sampler");
    CHECK(std::string{hp::shaderTextureSlotName(hp::kShaderTextureSlots - 1)} == "HpTexture3");
    CHECK(hp::shaderTextureSlotName(hp::kShaderTextureSlots) == nullptr);
    CHECK(hp::shaderTextureSamplerName(hp::kShaderTextureSlots) == nullptr);
}

TEST_CASE("a declared parameter presents exactly as a reflected C++ field does") {
    // **The unification D28 anticipated, as a check rather than a claim**: an
    // inspector consumes one description whichever reflection produced it. A
    // `hp::Material` field's metadata is a `PropertyMeta` from `entt::meta`; a
    // module's parameter hands back the same struct, filled from
    // `[HpRange]` and `[HpTooltip]`.
    hp::ShaderParam param;
    param.name = "referencePlane";
    param.type = hp::ShaderParamType::Float;
    param.min = 0.0;
    param.max = 1.0;
    param.tooltip = "Which height sits on the polygon plane.";

    const hp::PropertyMeta meta = param.meta();
    CHECK(meta.min == doctest::Approx(0.0));
    CHECK(meta.max == doctest::Approx(1.0));
    REQUIRE(meta.tooltip != nullptr);
    CHECK(std::string{meta.tooltip} == "Which height sits on the polygon plane.");
    CHECK_FALSE(meta.hidden);

    hp::ShaderParamLayout layout;
    layout.params.push_back(param);
    layout.blockBytes = 4;
    REQUIRE(layout.find("referencePlane") != nullptr);
    CHECK(layout.find("referencePlane")->type == hp::ShaderParamType::Float);
    // A name the module does not declare is nothing, not a fabricated entry —
    // which is what lets a shader drop a parameter without invalidating every
    // material that set it.
    CHECK(layout.find("gone") == nullptr);
    CHECK_FALSE(layout.empty());
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

// --- the missing-material convention (T0060.10) ------------------------------
//
// **Three states, and the first two must never be conflated.** If "nothing
// assigned" and "assigned but broken" both rendered the fallback, every
// unassigned surface in a project would look like an error — which would make
// the error signal worthless, since it would be everywhere.
//
// The policy is testable with no device, which is why it lives in the data model
// and not in the renderer. T0141.12 draws whatever these states say.

TEST_CASE("an empty override vector means every surface uses the import") {
    const hp::AssetPool pool;
    const std::vector<hp::Guid> none;
    for (std::size_t surface = 0; surface < 4; ++surface) {
        const hp::ResolvedMaterial resolved = hp::resolveMaterialSlot(pool, none, surface);
        CHECK(resolved.state == hp::MaterialSlot::Imported);
        CHECK_FALSE(resolved.material);
        CHECK(resolved.guid == hp::Guid{});
    }
}

TEST_CASE("a surface past the end of the overrides is imported, not an error") {
    // This is what lets the renderer index a slot without first checking the
    // vector's length against the model's material count.
    const hp::AssetPool pool;
    const std::vector<hp::Guid> one{hp::Guid{}};
    CHECK(hp::resolveMaterialSlot(pool, one, 7).state == hp::MaterialSlot::Imported);
}

TEST_CASE("a default GUID in the middle is a hole, not a terminator") {
    // Surface 1 keeps the model's material while 0 and 2 are overridden. A
    // resolver that stopped at the first default would silently drop surface 2.
    hp::AssetPool pool;
    const hp::Guid first{0xAA};
    const hp::Guid third{0xCC};
    pool.store(first, std::make_shared<hp::Material>());
    pool.store(third, std::make_shared<hp::Material>());

    const std::vector<hp::Guid> slots{first, hp::Guid{}, third};
    CHECK(hp::resolveMaterialSlot(pool, slots, 0).state == hp::MaterialSlot::Assigned);
    CHECK(hp::resolveMaterialSlot(pool, slots, 1).state == hp::MaterialSlot::Imported);
    CHECK(hp::resolveMaterialSlot(pool, slots, 2).state == hp::MaterialSlot::Assigned);
}

TEST_CASE("an assigned material comes back with the material itself") {
    hp::AssetPool pool;
    const hp::Guid guid{0x1234};
    auto stored = std::make_shared<hp::Material>();
    stored->roughness = 0.125F;
    pool.store(guid, stored);

    const hp::ResolvedMaterial resolved = hp::resolveMaterialSlot(pool, {guid}, 0);
    REQUIRE(resolved.state == hp::MaterialSlot::Assigned);
    REQUIRE(resolved.material);
    CHECK(resolved.material->roughness == doctest::Approx(0.125F));
    CHECK(resolved.guid == guid);
}

TEST_CASE("a GUID naming nothing in the pool is Missing, and says which") {
    // **`Missing` is the only state that is an error**, and the GUID has to
    // survive it: "which asset is missing" is the only question a developer has
    // at that point, and the pattern on screen cannot answer it.
    const hp::AssetPool pool;
    const hp::Guid absent{0xDEAD};

    const hp::ResolvedMaterial resolved = hp::resolveMaterialSlot(pool, {absent}, 0);
    CHECK(resolved.state == hp::MaterialSlot::Missing);
    CHECK(resolved.guid == absent);
    CHECK_FALSE(resolved.material);
}

TEST_CASE("a material stored under a different type does not resolve") {
    // The pool is keyed per type, so a texture and a material can share a GUID.
    // What must not happen is a texture resolving as a material.
    hp::AssetPool pool;
    const hp::Guid guid{0x777};
    pool.store(guid, std::make_shared<hp::TextureAsset>());
    CHECK(hp::resolveMaterialSlot(pool, {guid}, 0).state == hp::MaterialSlot::Missing);
}

TEST_CASE("the fallback material is unlit, and that is the half that gets missed") {
    const hp::Material fallback = hp::missingMaterial();
    // A magenta surface standing in shadow reads as plausible art. An unlit one
    // cannot be dimmed into looking deliberate.
    CHECK(fallback.unlit);
    CHECK(fallback.baseColour.x == doctest::Approx(1.0F));
    CHECK(fallback.baseColour.y == doctest::Approx(0.0F));
    CHECK(fallback.baseColour.z == doctest::Approx(1.0F));
    // Fully opaque and visible. **Never invisible**: a mesh that disappears is a
    // much harder bug to find than an ugly one.
    CHECK(fallback.baseColour.w == doctest::Approx(1.0F));
    CHECK(fallback.alphaMode == hp::AlphaMode::Opaque);
    // The fallback must not additionally change how the surface sorts or culls,
    // or "it went missing" and "it renders oddly" get conflated.
    CHECK_FALSE(fallback.doubleSided);
}
