#include <hp/Settings.hpp>

#include <hp/Log.hpp>
#include <hp/Profiling.hpp>
#include <hp/Yaml.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>

namespace hp {
namespace {

const LogCategory kLog("settings");

/// Splits a dotted path into its segments.
///
/// Empty segments are dropped rather than treated as an error: `a..b` and `a.b`
/// mean the same thing, which is friendlier than rejecting a key over a typo
/// that has an obvious intent.
std::vector<std::string_view> split(std::string_view key) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= key.size()) {
        const std::size_t dot = key.find('.', start);
        const std::size_t end = dot == std::string_view::npos ? key.size() : dot;
        if (end > start) {
            parts.push_back(key.substr(start, end - start));
        }
        if (dot == std::string_view::npos) {
            break;
        }
        start = dot + 1;
    }
    return parts;
}

} // namespace

// --- LayerNames --------------------------------------------------------------

LayerNames::LayerNames() : names_(static_cast<std::size_t>(kMaxLayers)) {
    // Layer 0 is named because it is the one every object lands on by default,
    // and an unnamed default reads in an inspector as though nothing is
    // configured at all.
    names_[static_cast<std::size_t>(kDefaultLayer)] = "Default";
}

std::string_view LayerNames::name(int index) const {
    if (index < 0 || index >= kMaxLayers) {
        return {};
    }
    return names_[static_cast<std::size_t>(index)];
}

void LayerNames::setName(int index, std::string value) {
    if (index < 0 || index >= kMaxLayers) {
        return;
    }
    names_[static_cast<std::size_t>(index)] = std::move(value);
}

int LayerNames::indexOf(std::string_view value) const {
    if (value.empty()) {
        // Otherwise every unnamed layer would match an empty query, and the
        // first one would win -- which is layer 1 on a default table.
        return -1;
    }
    for (int i = 0; i < kMaxLayers; ++i) {
        if (names_[static_cast<std::size_t>(i)] == value) {
            return i;
        }
    }
    return -1;
}

LayerMask LayerNames::mask(const std::vector<std::string>& names) const {
    LayerMask result;
    for (const std::string& entry : names) {
        const int index = indexOf(entry);
        if (index < 0) {
            // Logged rather than skipped silently: a typo in a mask is
            // otherwise indistinguishable from a deliberately narrow one, and
            // the symptom is an object that is simply never lit.
            HP_LOG_WARN(kLog, "unknown layer name '{}'; not added to the mask", entry);
            continue;
        }
        result.add(index);
    }
    return result;
}

const std::vector<std::string>& LayerNames::all() const {
    return names_;
}

// --- SettingsStore -----------------------------------------------------------

struct SettingsStore::Impl {
    /// Held as an optional because `YamlDocument` has no "empty document" state
    /// worth relying on -- an absent document and an empty one are the same to
    /// every getter here, and both produce fallbacks.
    std::optional<YamlDocument> document;

    /// Ensures a document exists so a `set` on a fresh store works.
    YamlNode root() {
        if (!document) {
            document = YamlDocument::parse("{}", "<settings>");
        }
        return document ? document->root() : YamlNode{};
    }

    /// Walks to a node without creating anything.
    [[nodiscard]] YamlNode find(std::string_view key) const {
        if (!document) {
            return {};
        }
        // `root()` is non-const on YamlDocument, and this walk does not mutate.
        YamlNode node = const_cast<YamlDocument&>(*document).root();
        for (std::string_view part : split(key)) {
            if (!node.valid() || !node.isMap() || !node.has(part)) {
                return {};
            }
            node = node[part];
        }
        return node;
    }

    /// Walks to a node's parent, creating intermediate maps.
    /// @returns the parent and the final segment, or an invalid node for an
    ///          empty key.
    std::pair<YamlNode, std::string_view> parentOf(std::string_view key) {
        const std::vector<std::string_view> parts = split(key);
        if (parts.empty()) {
            return {YamlNode{}, {}};
        }
        YamlNode node = root();
        for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
            if (!node.valid()) {
                return {YamlNode{}, {}};
            }
            node = node.has(parts[i]) ? node[parts[i]] : node.addMap(parts[i]);
        }
        return {node, parts.back()};
    }
};

SettingsStore::SettingsStore() : impl_(std::make_unique<Impl>()) {}
SettingsStore::~SettingsStore() = default;
SettingsStore::SettingsStore(SettingsStore&&) noexcept = default;
SettingsStore& SettingsStore::operator=(SettingsStore&&) noexcept = default;

bool SettingsStore::load(const std::string& path) {
    HP_PROFILE_ZONE();

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        // **Not an error.** A project that has never saved settings is the
        // normal state of a new project, and returning false here would make
        // every caller write a "unless it is just missing" branch.
        HP_LOG_DEBUG(kLog, "no settings at '{}'; using defaults", path);
        impl_->document.reset();
        return true;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        HP_LOG_WARN(kLog, "could not open '{}'; using defaults", path);
        impl_->document.reset();
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return loadFromString(buffer.str());
}

bool SettingsStore::loadFromString(std::string_view text) {
    impl_->document = YamlDocument::parse(text, "<settings>");
    if (!impl_->document) {
        // **Logged and survived, never fatal.** A malformed settings file that
        // stopped the editor launching would be infuriating and is entirely
        // avoidable; the caller can preserve the bad file if it wants to.
        HP_LOG_ERROR(kLog, "settings did not parse; falling back to defaults");
        return false;
    }
    return true;
}

bool SettingsStore::save(const std::string& path) const {
    HP_PROFILE_ZONE();

    const std::filesystem::path target(path);
    std::error_code ec;
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), ec);
    }
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        HP_LOG_ERROR(kLog, "could not write settings to '{}'", path);
        return false;
    }
    file << toString();
    return file.good();
}

std::string SettingsStore::toString() const {
    return impl_->document ? impl_->document->emit() : std::string{};
}

bool SettingsStore::has(std::string_view key) const {
    return impl_->find(key).valid();
}

bool SettingsStore::getBool(std::string_view key, bool fallback) const {
    const YamlNode node = impl_->find(key);
    return node.valid() ? node.read(fallback) : fallback;
}

std::int64_t SettingsStore::getInt(std::string_view key, std::int64_t fallback) const {
    const YamlNode node = impl_->find(key);
    return node.valid() ? node.read(fallback) : fallback;
}

double SettingsStore::getFloat(std::string_view key, double fallback) const {
    const YamlNode node = impl_->find(key);
    return node.valid() ? node.read(fallback) : fallback;
}

std::string SettingsStore::getString(std::string_view key, std::string fallback) const {
    const YamlNode node = impl_->find(key);
    return node.valid() ? node.read(fallback) : fallback;
}

void SettingsStore::setBool(std::string_view key, bool value) {
    auto [parent, leaf] = impl_->parentOf(key);
    if (parent.valid()) {
        parent.set(leaf, value);
    }
}

void SettingsStore::setInt(std::string_view key, std::int64_t value) {
    auto [parent, leaf] = impl_->parentOf(key);
    if (parent.valid()) {
        parent.set(leaf, value);
    }
}

void SettingsStore::setFloat(std::string_view key, double value) {
    auto [parent, leaf] = impl_->parentOf(key);
    if (parent.valid()) {
        parent.set(leaf, value);
    }
}

void SettingsStore::setString(std::string_view key, std::string_view value) {
    auto [parent, leaf] = impl_->parentOf(key);
    if (parent.valid()) {
        parent.set(leaf, value);
    }
}

LayerNames SettingsStore::readLayerNames() const {
    LayerNames names;
    const YamlNode node = impl_->find("layers");
    if (!node.valid() || !node.isSequence()) {
        return names;
    }
    const std::size_t count =
        std::min<std::size_t>(node.size(), static_cast<std::size_t>(kMaxLayers));
    for (std::size_t i = 0; i < count; ++i) {
        std::string value;
        if (node.at(i).tryRead(value)) {
            names.setName(static_cast<int>(i), std::move(value));
        }
    }
    return names;
}

void SettingsStore::writeLayerNames(const LayerNames& names) {
    const std::vector<std::string>& all = names.all();

    // Trailing unnamed layers are dropped, so a project that names three layers
    // gets three lines rather than thirty-two -- most of them empty strings that
    // say nothing and make the file look configured when it is not.
    std::size_t last = 0;
    bool any = false;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (!all[i].empty()) {
            last = i;
            any = true;
        }
    }
    if (!any) {
        return;
    }

    YamlNode root = impl_->root();
    if (!root.valid()) {
        return;
    }
    YamlNode sequence = root.addSequence("layers");
    if (!sequence.valid()) {
        return;
    }
    for (std::size_t i = 0; i <= last; ++i) {
        // Gaps are written as empty strings rather than skipped: position **is**
        // the layer index, so omitting an unnamed layer 2 would silently rename
        // layer 3 on the next read.
        sequence.append(std::string_view{all[i]});
    }
}

} // namespace hp
