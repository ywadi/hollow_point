// Settings and configuration (T0078).
//
// **Three kinds of configuration, and conflating them is the actual risk.** It
// shows up as friction rather than as bugs: user preferences committed to the
// repository cause a merge conflict on every window resize, and project settings
// stored per-user mean the team silently runs different physics rates.
//
// | Kind | Scope | Example |
// |---|---|---|
// | Project settings | Committed, shared by the team | startup scene, layer names, tick rate |
// | User preferences | Per machine, never committed | window size, editor layout |
// | Game options | Set by the player at run time | resolution, volume, keybinds |
//
// The test is one question: *would another developer on this project want this
// value?* Yes means project, no means user.
//
// **All three are the same mechanism** — a `SettingsStore` over a file — and
// differ only in where the file lives and who is allowed to commit it. One class
// rather than three keeps the corrupt-file behaviour, the typed accessors and
// the defaults identical, which is what stops the three drifting into three
// slightly different ideas of what a missing key means.
//
// **A config file never blocks startup.** A malformed preferences file that
// stops the editor launching is infuriating and entirely avoidable: the store
// logs, keeps its defaults, and carries on. The caller decides whether that
// matters; nothing here throws or aborts.
//
// **No singleton, deliberately.** Whoever owns the project owns its settings —
// T0024's `ProjectManager` when it exists. A global here would be a second
// lifetime to invalidate on project switch, which is the same class of dangling
// state `CameraSystem` avoids by resolving rather than caching.
#pragma once

#include <hp/Api.hpp>
#include <hp/Layers.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace hp {

/// Named object layers, as project settings (T0085.1).
///
/// **This is what stops `layer 7` appearing in code.** The mask machinery in
/// `hp/Layers.hpp` is deliberately free of names so it costs nothing in the hot
/// loop; names live here, in the settings the team commits, and are resolved at
/// the edges — authoring, the inspector, and any code that says which layer it
/// means.
///
/// **One table, shared by four subsystems**: camera culling (T0085), light
/// illumination (T0079), shadow casting (T0086) and physics collision (T0051).
/// A second table anywhere is the drift this exists to prevent, and it is
/// invisible until something collides or lights that visually should not.
class HP_API LayerNames {
public:
    /// Constructs the default table: layer 0 is named, the rest are empty.
    LayerNames();

    /// The name of a layer.
    /// @param index the layer, 0 to `kMaxLayers - 1`.
    /// @returns the name, or an empty view for an unnamed or out-of-range layer.
    ///          **Empty is not an error** — most projects name a handful of
    ///          layers and leave the rest.
    [[nodiscard]] std::string_view name(int index) const;

    /// Names a layer.
    /// @param index the layer, 0 to `kMaxLayers - 1`. Out of range is ignored.
    /// @param value the name. Empty clears it.
    /// @returns nothing.
    void setName(int index, std::string value);

    /// Looks a layer up by name.
    /// @param value the name to find. Comparison is exact and case-sensitive —
    ///        a case-insensitive match would make `Player` and `player` the same
    ///        layer in code and different ones in a diff.
    /// @returns the layer index, or **-1 when the name is unknown**. Callers
    ///          must check: silently returning 0 would put an object on the
    ///          default layer, which is visible to everything.
    [[nodiscard]] int indexOf(std::string_view value) const;

    /// Builds a mask from names.
    /// @param names the layers to include. **Unknown names are skipped and
    ///        logged**, rather than silently contributing nothing — a typo in a
    ///        mask is otherwise indistinguishable from a deliberately narrow
    ///        one.
    /// @returns the mask. Empty when no name resolved.
    [[nodiscard]] LayerMask mask(const std::vector<std::string>& names) const;

    /// @returns every name, indexed by layer. Always `kMaxLayers` long, with
    ///          empty strings for unnamed layers.
    [[nodiscard]] const std::vector<std::string>& all() const;

private:
    std::vector<std::string> names_;
};

/// A typed key-value store backed by a YAML file.
///
/// Keys are **dotted paths** — `render.shadows.enabled` — which nest in the
/// file, so a settings file reads as a grouped document rather than a flat list
/// of long keys. Nesting is created on demand by `set`.
///
/// **Every getter takes a fallback and none of them can fail.** A missing key, a
/// key of the wrong type, and a file that would not parse all produce the
/// fallback. That is the property that makes it impossible for a config file to
/// break a program that reads it correctly.
class HP_API SettingsStore {
public:
    /// Constructs an empty store holding no values.
    SettingsStore();

    /// Destroys the store. Does not save; saving is explicit.
    ~SettingsStore();

    /// Not copyable: it owns a parsed document.
    SettingsStore(const SettingsStore&) = delete;

    /// Not copyable; see the copy constructor.
    /// @returns nothing -- deleted.
    SettingsStore& operator=(const SettingsStore&) = delete;

    /// Moves the store.
    /// @param other the store to move from.
    SettingsStore(SettingsStore&& other) noexcept;

    /// Moves the store.
    /// @param other the store to move from.
    /// @returns this store.
    SettingsStore& operator=(SettingsStore&& other) noexcept;

    /// Loads from a file on the native filesystem.
    ///
    /// **Never fails in a way that stops a caller starting up.** A missing file
    /// leaves the store empty and returns true — a project that has never saved
    /// settings is not an error. A file that exists but will not parse is
    /// reported, logged, and leaves the store empty; the caller may then choose
    /// to preserve the bad file rather than overwrite it.
    /// @param path the file to read. Native path, not a VFS path — settings are
    ///        read before mounts are decided.
    /// @returns false **only** when the file existed and could not be parsed.
    bool load(const std::string& path);

    /// Replaces the contents from YAML text.
    /// @param text the document.
    /// @returns whether it parsed. The store is left empty when it did not.
    bool loadFromString(std::string_view text);

    /// Writes to a file on the native filesystem, creating parent directories.
    /// @param path the file to write.
    /// @returns whether it was written.
    bool save(const std::string& path) const;

    /// @returns the store as YAML text.
    [[nodiscard]] std::string toString() const;

    /// @param key a dotted path.
    /// @returns whether a value exists at that key.
    [[nodiscard]] bool has(std::string_view key) const;

    /// @param key a dotted path.
    /// @param fallback returned when the key is absent or not a boolean.
    /// @returns the value or the fallback.
    [[nodiscard]] bool getBool(std::string_view key, bool fallback) const;

    /// @param key a dotted path.
    /// @param fallback returned when the key is absent or not an integer.
    /// @returns the value or the fallback.
    [[nodiscard]] std::int64_t getInt(std::string_view key, std::int64_t fallback) const;

    /// @param key a dotted path.
    /// @param fallback returned when the key is absent or not a number.
    /// @returns the value or the fallback.
    [[nodiscard]] double getFloat(std::string_view key, double fallback) const;

    /// @param key a dotted path.
    /// @param fallback returned when the key is absent.
    /// @returns the value or the fallback.
    [[nodiscard]] std::string getString(std::string_view key, std::string fallback) const;

    /// Sets a boolean.
    /// @param key a dotted path; intermediate maps are created.
    /// @param value the value.
    /// @returns nothing.
    void setBool(std::string_view key, bool value);

    /// Sets an integer.
    /// @param key a dotted path; intermediate maps are created.
    /// @param value the value.
    /// @returns nothing.
    void setInt(std::string_view key, std::int64_t value);

    /// Sets a number.
    /// @param key a dotted path; intermediate maps are created.
    /// @param value the value.
    /// @returns nothing.
    void setFloat(std::string_view key, double value);

    /// Sets a string.
    /// @param key a dotted path; intermediate maps are created.
    /// @param value the value.
    /// @returns nothing.
    void setString(std::string_view key, std::string_view value);

    /// Reads the layer-name table out of this store (T0085.1).
    ///
    /// Stored under `layers` as a sequence, so the file reads as a list of names
    /// in layer order rather than as numbered keys.
    /// @returns the table. Layers absent from the file are unnamed.
    [[nodiscard]] LayerNames readLayerNames() const;

    /// Writes the layer-name table into this store.
    /// @param names the table to write. Trailing unnamed layers are omitted, so
    ///        a project naming three layers gets three lines rather than 32.
    /// @returns nothing.
    void writeLayerNames(const LayerNames& names);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hp
