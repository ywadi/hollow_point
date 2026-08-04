#include <hp/Assets.hpp>

#include <hp/Log.hpp>
#include <hp/Profiling.hpp>
#include <hp/Vfs.hpp>
#include <hp/Yaml.hpp>

#include <unordered_map>

namespace hp {
namespace {

const LogCategory kLog("assets");

/// A pool key: the GUID plus the type's stable name.
///
/// The name rather than a `type_index`, because the pool is reached across the
/// module boundary and T0095 established that entt's type index is not stable
/// there -- a module and the engine disagreeing about it produces a lookup that
/// returns nothing for an asset which is definitely loaded, with no diagnostic.
struct PoolKey {
    Guid guid;
    std::string type;

    bool operator==(const PoolKey& other) const {
        return guid == other.guid && type == other.type;
    }
};

struct PoolKeyHash {
    std::size_t operator()(const PoolKey& key) const {
        const std::size_t a = std::hash<Guid>{}(key.guid);
        const std::size_t b = std::hash<std::string>{}(key.type);
        // Boost's mix. Not chosen for quality -- the inputs are already
        // well-distributed -- but so that two keys differing only by type do not
        // collide through a plain XOR.
        return a ^ (b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2));
    }
};

} // namespace

std::string writeAssetMeta(const AssetMeta& meta) {
    YamlDocument document;
    YamlNode root = document.root();
    root.set("version", static_cast<std::uint64_t>(meta.schemaVersion));
    root.set("guid", meta.guid.toString());
    root.set("type", meta.type);
    root.set("source", meta.sourcePath);
    return document.emit();
}

std::optional<AssetMeta> parseAssetMeta(std::string_view yaml, std::string_view name) {
    HP_PROFILE_ZONE();

    auto document = YamlDocument::parse(yaml, name);
    if (!document) {
        return std::nullopt;
    }
    YamlNode root = document->root();

    AssetMeta meta;
    meta.schemaVersion = static_cast<std::uint32_t>(root["version"].read(std::uint64_t{0}));
    if (meta.schemaVersion == 0 || meta.schemaVersion > kAssetMetaVersion) {
        // A version this build does not understand is a reimport, not a crash.
        // Zero means the field was absent, which is the same answer.
        HP_LOG_WARN(kLog, "metafile '{}' has version {}; this build writes {}. Reimporting.", name,
                    meta.schemaVersion, kAssetMetaVersion);
        return std::nullopt;
    }

    const std::string guidText = root["guid"].read(std::string{});
    const auto guid = Guid::parse(guidText);
    if (!guid) {
        HP_LOG_WARN(kLog, "metafile '{}' has no readable GUID ('{}'); reimporting", name, guidText);
        return std::nullopt;
    }
    meta.guid = *guid;
    meta.type = root["type"].read(std::string{});
    meta.sourcePath = root["source"].read(std::string{});

    if (meta.type.empty() || meta.sourcePath.empty()) {
        HP_LOG_WARN(kLog, "metafile '{}' is missing 'type' or 'source'; reimporting", name);
        return std::nullopt;
    }
    return meta;
}

std::string metaPathFor(std::string_view assetPath) {
    return std::string(assetPath) + kAssetMetaExtension;
}

AssetMeta loadOrCreateAssetMeta(std::string_view assetPath, std::string_view type) {
    HP_PROFILE_ZONE();

    const std::string metaPath = metaPathFor(assetPath);
    if (const auto text = Vfs::readText(metaPath)) {
        if (auto meta = parseAssetMeta(*text, metaPath)) {
            // A metafile that disagrees about the type is a real problem: the
            // GUID is still the asset's identity, so keep it and correct the
            // type rather than minting a new one, which would orphan every
            // scene reference.
            if (meta->type != type) {
                HP_LOG_WARN(kLog,
                            "metafile '{}' says type '{}' but the asset is being loaded as '{}'; "
                            "keeping the GUID and correcting the type",
                            metaPath, meta->type, type);
                meta->type = std::string(type);
            }
            return *meta;
        }
    }

    // No metafile, or an unusable one. Both mean "this asset has not been
    // imported yet", which is the ordinary state of a file someone just dropped
    // into the project.
    AssetMeta fresh;
    fresh.guid = Guid::generate();
    fresh.sourcePath = std::string(assetPath);
    fresh.type = std::string(type);
    fresh.schemaVersion = kAssetMetaVersion;
    return fresh;
}

struct AssetPool::Impl {
    std::unordered_map<PoolKey, std::shared_ptr<void>, PoolKeyHash> assets;
};

AssetPool::AssetPool() : impl_(std::make_unique<Impl>()) {}

AssetPool::~AssetPool() = default;

AssetPool::AssetPool(AssetPool&& other) noexcept = default;

AssetPool& AssetPool::operator=(AssetPool&& other) noexcept = default;

void AssetPool::storeErased(Guid guid, std::string_view type, std::shared_ptr<void> asset) {
    PoolKey key{guid, std::string(type)};
    if (asset == nullptr) {
        impl_->assets.erase(key);
        return;
    }
    impl_->assets[std::move(key)] = std::move(asset);
}

std::shared_ptr<void> AssetPool::getErased(Guid guid, std::string_view type) const {
    const auto found = impl_->assets.find(PoolKey{guid, std::string(type)});
    return found != impl_->assets.end() ? found->second : nullptr;
}

bool AssetPool::removeErased(Guid guid, std::string_view type) {
    return impl_->assets.erase(PoolKey{guid, std::string(type)}) > 0;
}

std::size_t AssetPool::size() const {
    return impl_->assets.size();
}

void AssetPool::clear() {
    impl_->assets.clear();
}

std::vector<Guid> AssetPool::guidsOfType(std::string_view type) const {
    std::vector<Guid> found;
    for (const auto& [key, asset] : impl_->assets) {
        if (key.type == type) {
            found.push_back(key.guid);
        }
    }
    return found;
}

} // namespace hp
