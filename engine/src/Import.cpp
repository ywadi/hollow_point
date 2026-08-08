// What an import produces (T0169, D39). See hp/Import.hpp for the contract.
//
// **Deviceless on purpose, and that is the design rather than a convenience.**
// Everything here is VFS bytes in, VFS files out: the container is read
// through `GltfPeek`, the JSON through the same vendored nlohmann the
// required-extension warning uses (T0168.6), and the products through
// `writeMaterial` and `writeAssetMeta` — which both existed for a year with
// no caller outside a unit test, and are joined here rather than rebuilt.
// Nothing touches Diligent, so identity, re-import and override survival are
// all provable in the fast suite, where they will actually be run on every
// change.
//
// The mapping this file performs is **from the source JSON, not from
// Diligent's loaded model** — deliberately. The loaded model only exists on a
// device, and it has already dropped fields this file preserves: upstream
// never reads `occlusionTexture.strength` or spec-gloss's `glossinessFactor`
// (both measured on T0168), and a production pass that inherited those drops
// would bake them into files an author then owns.

#include <hp/Import.hpp>

#include <hp/Assets.hpp>
#include <hp/Log.hpp>
#include <hp/Material.hpp>
#include <hp/Profiling.hpp>
#include <hp/Vfs.hpp>

#include "GltfPeek.hpp"

#include <json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hp {
namespace {

const LogCategory kLog("assets.produce");

using Json = nlohmann::json;

// ---------------------------------------------------------------------------
// Small JSON readers. nlohmann with exceptions disabled still throws through
// `at()`, so every read here is find-and-check; a missing field is the
// ordinary case and must cost one branch, not one unwind.
// ---------------------------------------------------------------------------

const Json* member(const Json& node, const char* key) {
    if (!node.is_object()) {
        return nullptr;
    }
    const auto it = node.find(key);
    return it == node.end() ? nullptr : &*it;
}

double numberOr(const Json& node, const char* key, double fallback) {
    const Json* value = member(node, key);
    return value != nullptr && value->is_number() ? value->get<double>() : fallback;
}

int intOr(const Json& node, const char* key, int fallback) {
    const Json* value = member(node, key);
    return value != nullptr && value->is_number_integer() ? value->get<int>() : fallback;
}

bool boolOr(const Json& node, const char* key, bool fallback) {
    const Json* value = member(node, key);
    return value != nullptr && value->is_boolean() ? value->get<bool>() : fallback;
}

std::string stringOr(const Json& node, const char* key, std::string fallback) {
    const Json* value = member(node, key);
    return value != nullptr && value->is_string() ? value->get<std::string>()
                                                  : std::move(fallback);
}

/// Reads up to N numbers of an array into `out`; missing entries keep theirs.
template <std::size_t N>
void floatsOr(const Json& node, const char* key, std::array<float, N>& out) {
    const Json* value = member(node, key);
    if (value == nullptr || !value->is_array()) {
        return;
    }
    for (std::size_t i = 0; i < N && i < value->size(); ++i) {
        const Json& element = (*value)[i];
        if (element.is_number()) {
            out[i] = static_cast<float>(element.get<double>());
        }
    }
}

const Json* extensionOf(const Json& node, const char* name) {
    const Json* extensions = member(node, "extensions");
    return extensions != nullptr ? member(*extensions, name) : nullptr;
}

// ---------------------------------------------------------------------------
// Keys and paths
// ---------------------------------------------------------------------------

/// D39's sanitiser: filename- and key-safe, deterministic, boring.
std::string sanitiseKey(std::string_view name) {
    std::string key;
    key.reserve(name.size());
    for (const char c : name) {
        const auto uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) != 0 || c == '_' || c == '-') {
            key.push_back(c);
        } else {
            key.push_back('_');
        }
    }
    return key;
}

/// The stable keys for one kind, per D39: named-and-unique keeps the name,
/// duplicates count within their name only (`name_2`, `name_3`, …), unnamed
/// fall back to `<kind>_<index>` — the honest floor, since an unnamed
/// sub-asset's only identity *is* its position.
std::vector<std::string> stableKeys(const Json& array, const char* kindPrefix) {
    std::vector<std::string> keys;
    keys.reserve(array.size());
    std::unordered_map<std::string, int> seen;
    for (std::size_t i = 0; i < array.size(); ++i) {
        std::string name = sanitiseKey(stringOr(array[i], "name", std::string{}));
        if (name.empty()) {
            name = std::string(kindPrefix) + "_" + std::to_string(i);
        }
        const int occurrence = ++seen[name];
        if (occurrence > 1) {
            name += "_" + std::to_string(occurrence);
        }
        keys.push_back(std::move(name));
    }
    return keys;
}

std::string directoryOf(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string{} : std::string(path.substr(0, slash + 1));
}

/// Resolves a glTF-relative URI against the model's directory. Handles `./`
/// and `../` segments and `%XX` escapes — the two things a real export
/// actually uses — and nothing more exotic; a URI this cannot resolve simply
/// fails the later VFS read, loudly.
std::string resolveUri(std::string_view modelDirectory, std::string_view uri) {
    std::string decoded;
    decoded.reserve(uri.size());
    for (std::size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size()) {
            const auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') {
                    return c - '0';
                }
                if (c >= 'a' && c <= 'f') {
                    return c - 'a' + 10;
                }
                if (c >= 'A' && c <= 'F') {
                    return c - 'A' + 10;
                }
                return -1;
            };
            const int hi = hex(uri[i + 1]);
            const int lo = hex(uri[i + 2]);
            if (hi >= 0 && lo >= 0) {
                decoded.push_back(static_cast<char>(hi * 16 + lo));
                i += 2;
                continue;
            }
        }
        decoded.push_back(uri[i]);
    }

    std::vector<std::string> segments;
    const std::string full = std::string(modelDirectory) + decoded;
    std::size_t start = 0;
    while (start <= full.size()) {
        const std::size_t end = full.find('/', start);
        const std::string_view segment =
            std::string_view(full).substr(start, end == std::string::npos ? std::string::npos
                                                                          : end - start);
        if (segment == "..") {
            if (!segments.empty()) {
                segments.pop_back();
            }
        } else if (!segment.empty() && segment != ".") {
            segments.emplace_back(segment);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    std::string resolved;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (i > 0) {
            resolved += '/';
        }
        resolved += segments[i];
    }
    return resolved;
}

std::optional<std::vector<std::byte>> base64Decode(std::string_view text) {
    static constexpr auto valueOf = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') {
            return c - 'A';
        }
        if (c >= 'a' && c <= 'z') {
            return c - 'a' + 26;
        }
        if (c >= '0' && c <= '9') {
            return c - '0' + 52;
        }
        if (c == '+') {
            return 62;
        }
        if (c == '/') {
            return 63;
        }
        return -1;
    };
    std::vector<std::byte> out;
    out.reserve(text.size() / 4 * 3);
    int accumulator = 0;
    int bits = 0;
    for (const char c : text) {
        if (c == '=' || c == '\n' || c == '\r') {
            continue;
        }
        const int value = valueOf(c);
        if (value < 0) {
            return std::nullopt;
        }
        accumulator = (accumulator << 6) | value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::byte>((accumulator >> bits) & 0xFF));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// The production pass's working state
// ---------------------------------------------------------------------------

struct Producer {
    std::string modelPath;
    std::string modelDirectory;
    std::string assetsDirectory; ///< `<modelPath>.assets`
    const Json& document;
    std::string_view binChunk; ///< views into the caller's bytes

    AssetMeta meta;          ///< the source's metafile, with the registry
    bool metaChanged = false;

    ImportProducts products;

    /// buffer index -> contents, resolved once. The GLB bin chunk is copied
    /// here too; production is a cold editor path and one copy beats a
    /// lifetime rule.
    std::unordered_map<int, std::optional<std::vector<std::byte>>> buffers;

    /// image index -> the texture GUID a material slot should reference, or
    /// null when the image could not be produced. Cached so two materials
    /// sharing a texture produce one file.
    std::unordered_map<int, Guid> imageGuids;

    std::vector<std::string> imageKeys;

    /// The registry GUID for kind/key, minting and recording when absent.
    Guid registryGuid(const std::string& kind, const std::string& key) {
        if (const SubAssetRecord* record = meta.findSubAsset(kind, key)) {
            return record->guid;
        }
        SubAssetRecord record;
        record.kind = kind;
        record.key = key;
        record.guid = Guid::generate();
        meta.subAssets.push_back(record);
        metaChanged = true;
        return record.guid;
    }

    /// Writes/repairs the generated file's sidecar so the standard per-file
    /// path resolves it to the registry identity. **The registry wins on
    /// divergence** (D39): identity anchors in exactly one place.
    void ensureSidecar(const std::string& path, Guid guid, std::string_view typeName) {
        const std::string metaPath = metaPathFor(path);
        if (const auto text = Vfs::readText(metaPath)) {
            if (const auto existing = parseAssetMeta(*text, metaPath)) {
                if (existing->guid == guid) {
                    return;
                }
                HP_LOG_WARN(kLog,
                            "'{}' names GUID {} but the registry in '{}' says {}; the registry "
                            "wins and the sidecar is rewritten (D39)",
                            metaPath, existing->guid.toString(), metaPathFor(modelPath),
                            guid.toString());
            }
        }
        AssetMeta sidecar;
        sidecar.guid = guid;
        sidecar.sourcePath = path;
        sidecar.type = std::string(typeName);
        sidecar.schemaVersion = kAssetMetaVersion;
        if (!Vfs::writeText(metaPath, writeAssetMeta(sidecar))) {
            HP_LOG_WARN(kLog, "could not write '{}'; the generated asset's identity will not "
                              "survive a project reopen", metaPath);
        }
    }

    const std::optional<std::vector<std::byte>>& bufferContents(int index);
    Guid imageGuid(int imageIndex);
    Guid textureGuid(int textureIndex);
    void applyTextureTransform(const Json& textureInfo, Material& material, std::uint8_t uvSet);
    void applySampler(int textureIndex, Material& material, std::uint8_t uvSet);
    std::uint8_t assignTexture(const Json& parent, const char* key, Material& material,
                               Guid& slot);
    Material mapMaterial(const Json& gltfMaterial, const std::string& key);
    void produceMaterials();
    void reportUnproduced();
};

const std::optional<std::vector<std::byte>>& Producer::bufferContents(int index) {
    auto it = buffers.find(index);
    if (it != buffers.end()) {
        return it->second;
    }
    std::optional<std::vector<std::byte>> contents;
    const Json* array = member(document, "buffers");
    if (array != nullptr && array->is_array() && index >= 0 &&
        static_cast<std::size_t>(index) < array->size()) {
        const Json& buffer = (*array)[static_cast<std::size_t>(index)];
        const std::string uri = stringOr(buffer, "uri", std::string{});
        if (uri.empty()) {
            // The GLB bin chunk. Copied, per the header comment.
            if (!binChunk.empty()) {
                const auto* data = reinterpret_cast<const std::byte*>(binChunk.data());
                contents.emplace(data, data + binChunk.size());
            }
        } else if (uri.rfind("data:", 0) == 0) {
            const std::size_t comma = uri.find(',');
            if (comma != std::string::npos) {
                contents = base64Decode(std::string_view(uri).substr(comma + 1));
            }
        } else {
            contents = Vfs::read(resolveUri(modelDirectory, uri));
        }
    }
    return buffers.emplace(index, std::move(contents)).first->second;
}

Guid Producer::imageGuid(int imageIndex) {
    if (const auto it = imageGuids.find(imageIndex); it != imageGuids.end()) {
        return it->second;
    }
    Guid guid; // null: could not produce
    const Json* images = member(document, "images");
    if (images != nullptr && images->is_array() && imageIndex >= 0 &&
        static_cast<std::size_t>(imageIndex) < images->size()) {
        const Json& image = (*images)[static_cast<std::size_t>(imageIndex)];
        const std::string uri = stringOr(image, "uri", std::string{});
        if (!uri.empty() && uri.rfind("data:", 0) != 0) {
            // An external image is already an asset with its own identity;
            // the registry records nothing and the file's own metafile rules,
            // exactly as if the author had imported it by hand.
            const std::string path = resolveUri(modelDirectory, uri);
            if (Vfs::exists(path)) {
                const AssetMeta imageMeta =
                    loadOrCreateAssetMeta(path, assetKindName(AssetKind::Texture));
                guid = imageMeta.guid;
                const std::string metaPath = metaPathFor(path);
                if (!Vfs::exists(metaPath)) {
                    if (!Vfs::writeText(metaPath, writeAssetMeta(imageMeta))) {
                        HP_LOG_WARN(kLog, "could not write '{}'", metaPath);
                    }
                }
            } else {
                HP_LOG_WARN(kLog, "'{}' references image '{}', which is not in the mount tree",
                            modelPath, path);
            }
        } else {
            // Embedded, either as a data URI or a bufferView into the bin.
            // The bytes are the PNG/JPEG stream verbatim — extraction is a
            // write, never a re-encode (that is T0097's territory, on
            // purpose).
            std::string mime = stringOr(image, "mimeType", std::string{});
            std::optional<std::vector<std::byte>> bytes;
            if (!uri.empty()) {
                const std::size_t comma = uri.find(',');
                if (comma != std::string::npos) {
                    bytes = base64Decode(std::string_view(uri).substr(comma + 1));
                    if (mime.empty()) {
                        const std::size_t colon = uri.find(':');
                        const std::size_t semi = uri.find(';');
                        if (colon != std::string::npos && semi != std::string::npos &&
                            semi > colon) {
                            mime = uri.substr(colon + 1, semi - colon - 1);
                        }
                    }
                }
            } else if (const Json* viewIndex = member(image, "bufferView");
                       viewIndex != nullptr && viewIndex->is_number_integer()) {
                const Json* views = member(document, "bufferViews");
                const int index = viewIndex->get<int>();
                if (views != nullptr && views->is_array() && index >= 0 &&
                    static_cast<std::size_t>(index) < views->size()) {
                    const Json& view = (*views)[static_cast<std::size_t>(index)];
                    const auto offset = static_cast<std::size_t>(numberOr(view, "byteOffset", 0));
                    const auto length = static_cast<std::size_t>(numberOr(view, "byteLength", 0));
                    const auto& buffer = bufferContents(intOr(view, "buffer", 0));
                    if (buffer && offset + length <= buffer->size()) {
                        bytes.emplace(buffer->begin() + static_cast<std::ptrdiff_t>(offset),
                                      buffer->begin() +
                                          static_cast<std::ptrdiff_t>(offset + length));
                    }
                }
            }

            if (bytes && !bytes->empty()) {
                const char* extension = mime == "image/jpeg" ? ".jpg" : ".png";
                const std::string key = imageKeys[static_cast<std::size_t>(imageIndex)];
                guid = registryGuid("image", key);
                const std::string path = assetsDirectory + "/" + key + extension;
                bool created = false;
                if (!Vfs::exists(path)) {
                    if (Vfs::createDirectory(assetsDirectory) && Vfs::write(path, *bytes)) {
                        created = true;
                    } else {
                        HP_LOG_WARN(kLog, "could not write '{}'", path);
                        guid = Guid{};
                    }
                }
                if (guid.isValid()) {
                    ensureSidecar(path, guid, assetKindName(AssetKind::Texture));
                    products.assets.push_back(
                        ImportedSubAsset{"image", key, guid, path, created});
                }
            } else {
                HP_LOG_WARN(kLog, "'{}' image {} could not be read from its container",
                            modelPath, imageIndex);
            }
        }
    }
    imageGuids.emplace(imageIndex, guid);
    return guid;
}

Guid Producer::textureGuid(int textureIndex) {
    const Json* textures = member(document, "textures");
    if (textures == nullptr || !textures->is_array() || textureIndex < 0 ||
        static_cast<std::size_t>(textureIndex) >= textures->size()) {
        return Guid{};
    }
    const int source = intOr((*textures)[static_cast<std::size_t>(textureIndex)], "source", -1);
    return source >= 0 ? imageGuid(source) : Guid{};
}

/// `KHR_texture_transform` → the channel's `UvChannel` (T0168 closed the
/// *render* half; this is the authored half). Per-channel rather than
/// per-texture is `hp::Material`'s deliberate shape, so the first texture on
/// a channel decides and a disagreeing second is warned about — recorded as
/// lossy in the production table.
void Producer::applyTextureTransform(const Json& textureInfo, Material& material,
                                     std::uint8_t uvSet) {
    const Json* transform = extensionOf(textureInfo, "KHR_texture_transform");
    if (transform == nullptr) {
        return;
    }
    UvChannel& channel = uvSet == 0 ? material.uv0 : material.uv1;
    UvChannel incoming = channel;
    std::array<float, 2> scale{incoming.scale.x, incoming.scale.y};
    std::array<float, 2> offset{incoming.offset.x, incoming.offset.y};
    floatsOr(*transform, "scale", scale);
    floatsOr(*transform, "offset", offset);
    incoming.scale = float2{scale[0], scale[1]};
    incoming.offset = float2{offset[0], offset[1]};
    incoming.rotation = static_cast<float>(numberOr(*transform, "rotation", incoming.rotation));

    const bool channelDefault = channel.scale.x == 1.0F && channel.scale.y == 1.0F &&
                                channel.offset.x == 0.0F && channel.offset.y == 0.0F &&
                                channel.rotation == 0.0F;
    const bool agrees = channel.scale.x == incoming.scale.x &&
                        channel.scale.y == incoming.scale.y &&
                        channel.offset.x == incoming.offset.x &&
                        channel.offset.y == incoming.offset.y &&
                        channel.rotation == incoming.rotation;
    if (channelDefault) {
        channel = incoming;
    } else if (!agrees) {
        HP_LOG_WARN(kLog,
                    "'{}': two textures on UV{} carry different KHR_texture_transform values; "
                    "the first one wins in the generated .hpmat (per-channel transforms are the "
                    "engine's authored shape)",
                    modelPath, uvSet);
    }
}

/// The file's sampler wrap modes → the channel, same first-wins rule.
void Producer::applySampler(int textureIndex, Material& material, std::uint8_t uvSet) {
    const Json* textures = member(document, "textures");
    if (textures == nullptr || !textures->is_array() || textureIndex < 0 ||
        static_cast<std::size_t>(textureIndex) >= textures->size()) {
        return;
    }
    const int samplerIndex =
        intOr((*textures)[static_cast<std::size_t>(textureIndex)], "sampler", -1);
    const Json* samplers = member(document, "samplers");
    if (samplerIndex < 0 || samplers == nullptr || !samplers->is_array() ||
        static_cast<std::size_t>(samplerIndex) >= samplers->size()) {
        return;
    }
    const Json& sampler = (*samplers)[static_cast<std::size_t>(samplerIndex)];
    const auto wrapOf = [](int mode) {
        switch (mode) {
        case 33071: // CLAMP_TO_EDGE
            return TextureWrap::Clamp;
        case 33648: // MIRRORED_REPEAT
            return TextureWrap::Mirror;
        default: // 10497 REPEAT, and the spec's default
            return TextureWrap::Repeat;
        }
    };
    UvChannel& channel = uvSet == 0 ? material.uv0 : material.uv1;
    channel.wrapU = wrapOf(intOr(sampler, "wrapS", 10497));
    channel.wrapV = wrapOf(intOr(sampler, "wrapT", 10497));
}

/// Reads `parent[key]` as a glTF textureInfo into `slot`, returning the UV
/// set it selects. Applies the transform and sampler to that channel.
std::uint8_t Producer::assignTexture(const Json& parent, const char* key, Material& material,
                                     Guid& slot) {
    const Json* info = member(parent, key);
    if (info == nullptr || !info->is_object()) {
        return 0;
    }
    const int index = intOr(*info, "index", -1);
    if (index < 0) {
        return 0;
    }
    const auto uvSet = static_cast<std::uint8_t>(intOr(*info, "texCoord", 0) == 1 ? 1 : 0);
    const Guid guid = textureGuid(index);
    if (!guid.isValid()) {
        HP_LOG_WARN(kLog, "'{}': texture for '{}' could not be produced; the slot is left empty",
                    modelPath, key);
        return uvSet;
    }
    slot = guid;
    applyTextureTransform(*info, material, uvSet);
    applySampler(index, material, uvSet);
    return uvSet;
}

Material Producer::mapMaterial(const Json& gltfMaterial, const std::string& key) {
    Material material;

    // ---- core -------------------------------------------------------------
    const Json* pbr = member(gltfMaterial, "pbrMetallicRoughness");
    if (pbr != nullptr) {
        std::array<float, 4> base{1.0F, 1.0F, 1.0F, 1.0F};
        floatsOr(*pbr, "baseColorFactor", base);
        material.baseColour = float4{base[0], base[1], base[2], base[3]};
        material.metallic = static_cast<float>(numberOr(*pbr, "metallicFactor", 1.0));
        material.roughness = static_cast<float>(numberOr(*pbr, "roughnessFactor", 1.0));
        material.baseColourUv =
            assignTexture(*pbr, "baseColorTexture", material, material.baseColourTexture);
        material.metallicRoughnessUv = assignTexture(*pbr, "metallicRoughnessTexture", material,
                                                     material.metallicRoughnessTexture);
    }

    if (const Json* normal = member(gltfMaterial, "normalTexture")) {
        material.normalScale = static_cast<float>(numberOr(*normal, "scale", 1.0));
    }
    material.normalUv =
        assignTexture(gltfMaterial, "normalTexture", material, material.normalTexture);

    // `strength` — preserved here although the render path's loader drops it
    // (upstream never reads the field; the matrix records that row).
    if (const Json* occlusion = member(gltfMaterial, "occlusionTexture")) {
        material.occlusionStrength = static_cast<float>(numberOr(*occlusion, "strength", 1.0));
    }
    material.occlusionUv =
        assignTexture(gltfMaterial, "occlusionTexture", material, material.occlusionTexture);

    std::array<float, 3> emissive{0.0F, 0.0F, 0.0F};
    floatsOr(gltfMaterial, "emissiveFactor", emissive);
    material.emissive = float3{emissive[0], emissive[1], emissive[2]};
    material.emissiveUv =
        assignTexture(gltfMaterial, "emissiveTexture", material, material.emissiveTexture);

    const std::string alphaMode = stringOr(gltfMaterial, "alphaMode", "OPAQUE");
    material.alphaMode = alphaMode == "BLEND"  ? AlphaMode::Blend
                         : alphaMode == "MASK" ? AlphaMode::Mask
                                               : AlphaMode::Opaque;
    material.alphaCutoff = static_cast<float>(numberOr(gltfMaterial, "alphaCutoff", 0.5));
    material.doubleSided = boolOr(gltfMaterial, "doubleSided", false);

    // ---- extensions, in the matrix's order ---------------------------------
    material.unlit = extensionOf(gltfMaterial, "KHR_materials_unlit") != nullptr;

    if (const Json* strength = extensionOf(gltfMaterial, "KHR_materials_emissive_strength")) {
        const auto factor = static_cast<float>(numberOr(*strength, "emissiveStrength", 1.0));
        material.emissive =
            float3{material.emissive.x * factor, material.emissive.y * factor,
                   material.emissive.z * factor};
    }

    if (const Json* specGloss =
            extensionOf(gltfMaterial, "KHR_materials_pbrSpecularGlossiness")) {
        // **The migration boundary D39 names.** The imported model renders SG
        // natively (T0168.2); a `.hpmat` is metallic-roughness by the standing
        // format decision, so extraction converts what has an MR meaning and
        // says plainly what it leaves behind. `glossinessFactor` is read here
        // although the render path's loader never does.
        std::array<float, 4> diffuse{1.0F, 1.0F, 1.0F, 1.0F};
        floatsOr(*specGloss, "diffuseFactor", diffuse);
        material.baseColour = float4{diffuse[0], diffuse[1], diffuse[2], diffuse[3]};
        material.metallic = 0.0F;
        material.roughness =
            1.0F - static_cast<float>(numberOr(*specGloss, "glossinessFactor", 1.0));
        material.baseColourUv =
            assignTexture(*specGloss, "diffuseTexture", material, material.baseColourTexture);
        if (member(*specGloss, "specularGlossinessTexture") != nullptr) {
            HP_LOG_WARN(kLog,
                        "'{}' material '{}': the specular-glossiness texture has no "
                        "metallic-roughness equivalent and is not converted (texel-space "
                        "conversion is T0097's); the generated .hpmat is a migration off the "
                        "archived workflow, not a re-skin — the imported model itself still "
                        "renders spec-gloss natively",
                        modelPath, key);
        }
    }

    if (const Json* clearcoat = extensionOf(gltfMaterial, "KHR_materials_clearcoat")) {
        material.clearcoat = static_cast<float>(numberOr(*clearcoat, "clearcoatFactor", 0.0));
        material.clearcoatRoughness =
            static_cast<float>(numberOr(*clearcoat, "clearcoatRoughnessFactor", 0.0));
        material.clearcoatUv =
            assignTexture(*clearcoat, "clearcoatTexture", material, material.clearcoatTexture);
        material.clearcoatRoughnessUv = assignTexture(
            *clearcoat, "clearcoatRoughnessTexture", material, material.clearcoatRoughnessTexture);
        if (const Json* coatNormal = member(*clearcoat, "clearcoatNormalTexture")) {
            material.clearcoatNormalScale =
                static_cast<float>(numberOr(*coatNormal, "scale", 1.0));
        }
        material.clearcoatNormalUv = assignTexture(*clearcoat, "clearcoatNormalTexture", material,
                                                   material.clearcoatNormalTexture);
    }

    if (const Json* sheen = extensionOf(gltfMaterial, "KHR_materials_sheen")) {
        std::array<float, 3> colour{0.0F, 0.0F, 0.0F};
        floatsOr(*sheen, "sheenColorFactor", colour);
        material.sheenColour = float3{colour[0], colour[1], colour[2]};
        material.sheenRoughness =
            static_cast<float>(numberOr(*sheen, "sheenRoughnessFactor", 0.0));
        material.sheenColourUv =
            assignTexture(*sheen, "sheenColorTexture", material, material.sheenColourTexture);
        material.sheenRoughnessUv = assignTexture(*sheen, "sheenRoughnessTexture", material,
                                                  material.sheenRoughnessTexture);
    }

    if (const Json* anisotropy = extensionOf(gltfMaterial, "KHR_materials_anisotropy")) {
        material.anisotropyStrength =
            static_cast<float>(numberOr(*anisotropy, "anisotropyStrength", 0.0));
        material.anisotropyRotation =
            static_cast<float>(numberOr(*anisotropy, "anisotropyRotation", 0.0));
        material.anisotropyUv =
            assignTexture(*anisotropy, "anisotropyTexture", material, material.anisotropyTexture);
    }

    if (const Json* iridescence = extensionOf(gltfMaterial, "KHR_materials_iridescence")) {
        material.iridescence =
            static_cast<float>(numberOr(*iridescence, "iridescenceFactor", 0.0));
        material.iridescenceIor =
            static_cast<float>(numberOr(*iridescence, "iridescenceIor", 1.3));
        material.iridescenceThicknessMin =
            static_cast<float>(numberOr(*iridescence, "iridescenceThicknessMinimum", 100.0));
        material.iridescenceThicknessMax =
            static_cast<float>(numberOr(*iridescence, "iridescenceThicknessMaximum", 400.0));
        material.iridescenceUv = assignTexture(*iridescence, "iridescenceTexture", material,
                                               material.iridescenceTexture);
        material.iridescenceThicknessUv =
            assignTexture(*iridescence, "iridescenceThicknessTexture", material,
                          material.iridescenceThicknessTexture);
    }

    if (const Json* transmission = extensionOf(gltfMaterial, "KHR_materials_transmission")) {
        material.transmission =
            static_cast<float>(numberOr(*transmission, "transmissionFactor", 0.0));
        material.transmissionUv = assignTexture(*transmission, "transmissionTexture", material,
                                                material.transmissionTexture);
    }

    if (const Json* ior = extensionOf(gltfMaterial, "KHR_materials_ior")) {
        // Read standalone, although the render path honours it only through
        // transmission — the authored file keeps the truth (matrix row).
        material.ior = static_cast<float>(numberOr(*ior, "ior", 1.5));
    }

    if (const Json* volume = extensionOf(gltfMaterial, "KHR_materials_volume")) {
        material.thickness = static_cast<float>(numberOr(*volume, "thicknessFactor", 0.0));
        material.attenuationDistance =
            static_cast<float>(numberOr(*volume, "attenuationDistance", 0.0));
        std::array<float, 3> attenuation{1.0F, 1.0F, 1.0F};
        floatsOr(*volume, "attenuationColor", attenuation);
        material.attenuationColour = float3{attenuation[0], attenuation[1], attenuation[2]};
        material.thicknessUv =
            assignTexture(*volume, "thicknessTexture", material, material.thicknessTexture);
    }

    return material;
}

void Producer::produceMaterials() {
    const Json* materials = member(document, "materials");
    if (materials == nullptr || !materials->is_array() || materials->empty()) {
        return;
    }
    const std::vector<std::string> keys = stableKeys(*materials, "material");

    for (std::size_t i = 0; i < materials->size(); ++i) {
        const std::string& key = keys[i];
        const Guid guid = registryGuid("material", key);
        const std::string path = assetsDirectory + "/" + key + kMaterialExtension;

        bool created = false;
        if (!Vfs::exists(path)) {
            // Extract-once (D39): only a file that does not exist is written,
            // so an author's edits survive every re-import structurally, and
            // deleting the generated file is "reset to source" with the same
            // GUID — scene references survive both.
            const Material material = mapMaterial((*materials)[i], key);
            if (!Vfs::createDirectory(assetsDirectory) ||
                !Vfs::writeText(path, writeMaterial(material))) {
                HP_LOG_WARN(kLog, "could not write '{}'; the material stays unproduced", path);
                continue;
            }
            created = true;
        }
        ensureSidecar(path, guid, AssetTraits<Material>::name);
        products.assets.push_back(ImportedSubAsset{"material", key, guid, path, created});
    }
}

/// T0169.8's first rule: a type the pass does not produce is named, once,
/// with its count — never silently dropped. The rows live in the production
/// table (`14-asset-import-matrix.md`).
void Producer::reportUnproduced() {
    std::string skipped;
    const auto note = [&](const char* key, const char* label) {
        const Json* node = member(document, key);
        if (node != nullptr && node->is_array() && !node->empty()) {
            if (!skipped.empty()) {
                skipped += ", ";
            }
            skipped += std::to_string(node->size());
            skipped += ' ';
            skipped += label;
        }
    };
    note("meshes", "meshes (sub-mesh identity: T0023's named gap)");
    note("animations", "animations (T0041/T0049)");
    note("skins", "skins (T0049)");
    note("cameras", "cameras (T0169's table: decision recorded, not produced)");
    if (!skipped.empty()) {
        HP_LOG_INFO(kLog,
                    "'{}' also carries {}; these stay inside the imported model — the "
                    "production table in 14-asset-import-matrix.md says what each becomes",
                    modelPath, skipped);
    }
}

} // namespace

ImportProducts produceEngineAssets(std::string_view modelPath) {
    HP_PROFILE_ZONE();

    ImportProducts failed;
    const std::string path(modelPath);
    const auto bytes = Vfs::read(path);
    if (!bytes) {
        HP_LOG_ERROR(kLog, "'{}' could not be read through the VFS", path);
        return failed;
    }
    const std::string_view json = gltfJsonOf(*bytes);
    if (json.empty()) {
        HP_LOG_ERROR(kLog, "'{}' is not a glTF container this build can slice", path);
        return failed;
    }
    const Json document = Json::parse(json.begin(), json.end(), /*cb=*/nullptr,
                                      /*allow_exceptions=*/false);
    if (document.is_discarded() || !document.is_object()) {
        HP_LOG_ERROR(kLog, "'{}' does not parse as glTF JSON", path);
        return failed;
    }

    Producer producer{path,
                      directoryOf(path),
                      path + ".assets",
                      document,
                      gltfBinChunkOf(*bytes),
                      loadOrCreateAssetMeta(path, "Mesh")};

    const Json* images = member(document, "images");
    producer.imageKeys = images != nullptr && images->is_array()
                             ? stableKeys(*images, "image")
                             : std::vector<std::string>{};

    producer.produceMaterials();

    // **Every image, not only the referenced ones.** Extraction through the
    // material slots alone leaves any image those slots do not map locked in
    // the container — measured on the Aston Martin, whose spec-gloss texture
    // is deliberately not converted to a `.hpmat` slot (D39's migration
    // boundary) and whose 21 MiB PNG therefore never landed. The image is the
    // author's content either way — a migration off spec-gloss *needs* the
    // file — so images are produced eagerly, and the lazy path above becomes
    // a cache hit.
    if (producer.imageKeys.size() > 0) {
        for (std::size_t i = 0; i < producer.imageKeys.size(); ++i) {
            (void)producer.imageGuid(static_cast<int>(i));
        }
    }

    producer.reportUnproduced();

    if (producer.metaChanged) {
        producer.meta.schemaVersion = kAssetMetaVersion;
        if (!Vfs::writeText(metaPathFor(path), writeAssetMeta(producer.meta))) {
            HP_LOG_WARN(kLog,
                        "could not write '{}'; sub-asset identities will be re-minted on the "
                        "next import, which will detach any scene reference to them",
                        metaPathFor(path));
        }
    }

    producer.products.ok = true;
    return producer.products;
}

} // namespace hp
