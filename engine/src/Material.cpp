// Material assets (T0060.1). See `hp/Material.hpp` for what this models and why.
//
// **There is no per-field code here, and that is the deliverable.** Save and
// load walk the reflected properties, exactly as scene components do (D23), so
// adding a parameter to `Material` is one line in `registerMaterialTypes` and no
// change to this file, to the file format, or to any document already written.

#include <hp/Material.hpp>

#include <hp/Log.hpp>
#include <hp/Profiling.hpp>
#include <hp/Reflect.hpp>
#include <hp/Serialize.hpp>
#include <hp/Vfs.hpp>
#include <hp/Yaml.hpp>

namespace hp {
namespace {

const LogCategory kLog("assets.material");

/// The document's version key, at the root beside the material itself.
constexpr const char* kVersionKey = "version";

/// The key the material's properties nest under.
///
/// **Nested rather than flat at the root**, which costs one indent and buys
/// exactly one thing: `version` can never collide with a material parameter.
/// A flat document would break the day somebody added a `Material::version`,
/// and it would break by silently reading the schema number as the parameter.
constexpr const char* kMaterialKey = "material";

} // namespace

void registerMaterialTypes() {
    static bool done = false;
    if (done) {
        return;
    }
    done = true;

    // **The enum is reflected separately from the struct that holds it**, and
    // must be, because entt cannot enumerate an enum's enumerators and neither
    // can C++20 — every value is named by hand. A missing one is a value the
    // inspector shows as a bare integer and a document writes the same way, so
    // this list gains a line whenever `AlphaMode` does.
    reflect<AlphaMode>("AlphaMode")
        .value<AlphaMode::Opaque>("Opaque")
        .value<AlphaMode::Mask>("Mask")
        .value<AlphaMode::Blend>("Blend");

    reflect<TextureWrap>("TextureWrap")
        .value<TextureWrap::Repeat>("Repeat")
        .value<TextureWrap::Mirror>("Mirror")
        .value<TextureWrap::Clamp>("Clamp");

    // **The two name/value stores** (T0160.3). Reflected like anything else, so
    // save, load and the cook walk them with no per-field code — the same
    // property this file's header claims for every parameter above.
    reflect<MaterialParam>("MaterialParam")
        .property<&MaterialParam::name>("name")
        .property<&MaterialParam::value>("value");

    reflect<MaterialTexture>("MaterialTexture")
        .property<&MaterialTexture::name>("name")
        .property<&MaterialTexture::texture>("texture");

    reflect<UvChannel>("UvChannel")
        .property<&UvChannel::scale>("scale")
        .property<&UvChannel::offset>("offset")
        .property<&UvChannel::rotation>("rotation")
        .property<&UvChannel::wrapU>("wrapU")
        .property<&UvChannel::wrapV>("wrapV");

    reflect<Material>("Material")
        .property<&Material::baseColour>("baseColour")
        .property<&Material::emissive>("emissive")
        .property<&Material::metallic>("metallic")
        .property<&Material::roughness>("roughness")
        .property<&Material::normalScale>("normalScale")
        .property<&Material::occlusionStrength>("occlusionStrength")
        .property<&Material::alphaMode>("alphaMode")
        .property<&Material::alphaCutoff>("alphaCutoff")
        .property<&Material::doubleSided>("doubleSided")
        .property<&Material::unlit>("unlit")
        .property<&Material::baseColourTexture>("baseColourTexture")
        .property<&Material::metallicRoughnessTexture>("metallicRoughnessTexture")
        .property<&Material::normalTexture>("normalTexture")
        .property<&Material::occlusionTexture>("occlusionTexture")
        .property<&Material::emissiveTexture>("emissiveTexture")
        .property<&Material::heightTexture>("heightTexture")
        .property<&Material::heightScale>("heightScale")
        .property<&Material::triplanar>("triplanar")
        .property<&Material::triplanarScale>("triplanarScale")
        .property<&Material::shader>("shader")
        .property<&Material::uv0>("uv0")
        .property<&Material::uv1>("uv1")
        .property<&Material::baseColourUv>("baseColourUv")
        .property<&Material::metallicRoughnessUv>("metallicRoughnessUv")
        .property<&Material::normalUv>("normalUv")
        .property<&Material::occlusionUv>("occlusionUv")
        .property<&Material::emissiveUv>("emissiveUv")
        .property<&Material::params>("params")
        .property<&Material::textures>("textures");
}

std::string writeMaterial(const Material& material) {
    HP_PROFILE_ZONE();

    registerMaterialTypes();

    YamlDocument document;
    YamlNode root = document.root();
    root.set(kVersionKey, static_cast<std::uint64_t>(kMaterialSchemaVersion));
    YamlNode node = root.addMap(kMaterialKey);
    if (!writeProperties(node, entt::forward_as_meta(material))) {
        // Reachable only if reflection is missing, which means the registration
        // above did not run in this process — the `adoptMetaContext` failure
        // mode from T0095, and worth naming rather than emitting a valid-looking
        // document with an empty material in it.
        HP_LOG_ERROR(kLog, "Material has no reflected properties; the document will be empty");
    }
    return document.emit();
}

std::optional<Material> parseMaterial(std::string_view yaml, std::string_view name) {
    HP_PROFILE_ZONE();

    registerMaterialTypes();

    auto document = YamlDocument::parse(yaml, name);
    if (!document) {
        return std::nullopt;
    }
    YamlNode root = document->root();

    const auto version = static_cast<std::uint32_t>(root[kVersionKey].read(std::uint64_t{0}));
    if (version == 0) {
        HP_LOG_ERROR(kLog, "'{}' has no schema version; refusing to guess at its shape", name);
        return std::nullopt;
    }
    if (version > kMaterialSchemaVersion) {
        HP_LOG_ERROR(kLog,
                     "'{}' is material schema version {}, and this build understands {}. Refusing "
                     "to load rather than discarding what it does not recognise",
                     name, version, kMaterialSchemaVersion);
        return std::nullopt;
    }

    Material material;
    YamlNode node = root[kMaterialKey];
    if (!node.valid()) {
        // A version and nothing else. Legal: every parameter has a default, and
        // a material of pure defaults is a perfectly good white surface.
        return material;
    }
    if (!readProperties(node, entt::forward_as_meta(material))) {
        HP_LOG_WARN(kLog, "'{}' has a 'material' key with nothing readable under it", name);
    }
    return material;
}

ResolvedMaterial resolveMaterialSlot(const AssetPool& pool, const std::vector<Guid>& slots,
                                     std::size_t surface) {
    HP_PROFILE_ZONE();

    ResolvedMaterial resolved;

    // **Past the end is `Imported`, not an error**, and that is what lets an
    // untouched import carry an empty vector rather than one default GUID per
    // surface. It also means the renderer can index a slot without first
    // checking the vector's length against the model's material count.
    if (surface >= slots.size()) {
        return resolved;
    }

    const Guid guid = slots[surface];
    if (!guid.isValid()) {
        // Assigned nothing. The common case, and deliberately indistinguishable
        // from having no slot at all.
        return resolved;
    }

    resolved.guid = guid;
    if (auto material = pool.get<Material>(guid)) {
        resolved.state = MaterialSlot::Assigned;
        resolved.material = std::move(material);
        return resolved;
    }

    // Named, and not there. **No logging from here**: this runs per surface per
    // object per frame, and a missing material logged at 60 Hz is thousands of
    // lines a minute that make the console useless -- which is the opposite of
    // what the visible fallback is for. The same trap is written on T0141 for
    // the failed-compile path, and it is the same rule: report on the
    // transition, never from the draw path.
    resolved.state = MaterialSlot::Missing;
    return resolved;
}

Material missingMaterial() {
    Material material;
    // Magenta, matching `makePlaceholderTexture`'s checks (T0023.6) rather than
    // inventing a second colour for the same message.
    material.baseColour = float4{1.0F, 0.0F, 1.0F, 1.0F};
    // **Unlit is the load-bearing half.** A magenta surface standing in shadow
    // reads as plausible art; an unlit one cannot be dimmed into looking
    // deliberate.
    material.unlit = true;
    // Opaque and single-sided: the fallback must not additionally change how the
    // surface sorts or culls, or "it went missing" and "it renders oddly" get
    // conflated.
    material.alphaMode = AlphaMode::Opaque;
    material.doubleSided = false;
    return material;
}

std::shared_ptr<Material> loadMaterial(std::string_view virtualPath) {
    HP_PROFILE_ZONE();

    // Through the VFS, like every other read (D13). A material inside a pack and
    // one in a project folder take the same path here, which is the whole point
    // of T0103 landing before the asset layer.
    const std::optional<std::string> text = Vfs::readText(std::string{virtualPath});
    if (!text) {
        HP_LOG_ERROR(kLog, "'{}' is not in the mount tree", virtualPath);
        return nullptr;
    }
    const std::optional<Material> material = parseMaterial(*text, virtualPath);
    if (!material) {
        return nullptr;
    }
    return std::make_shared<Material>(*material);
}

} // namespace hp
