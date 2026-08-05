# `<hp/Settings.hpp>`

*Generated from `engine/include/hp/Settings.hpp` — do not edit.*

```cpp
#include <hp/Settings.hpp>
```

28 public declaration(s), 28 documented.

## `LayerNames`

```cpp
class LayerNames
```

 Named object layers, as project settings (T0085.1).

 **This is what stops `layer 7` appearing in code.** The mask machinery in
 `hp/Layers.hpp` is deliberately free of names so it costs nothing in the hot
 loop; names live here, in the settings the team commits, and are resolved at
 the edges — authoring, the inspector, and any code that says which layer it
 means.

 **One table, shared by four subsystems**: camera culling (T0085), light
 illumination (T0079), shadow casting (T0086) and physics collision (T0051).
 A second table anywhere is the drift this exists to prevent, and it is
 invisible until something collides or lights that visually should not.

## `LayerNames::LayerNames`

```cpp
LayerNames()
```

 Constructs the default table: layer 0 is named, the rest are empty.

## `LayerNames::name`

```cpp
std::string_view name(int index) const
```

 The name of a layer.
 @param index the layer, 0 to `kMaxLayers - 1`.
 @returns the name, or an empty view for an unnamed or out-of-range layer.
          **Empty is not an error** — most projects name a handful of
          layers and leave the rest.

## `LayerNames::setName`

```cpp
void setName(int index, std::string value)
```

 Names a layer.
 @param index the layer, 0 to `kMaxLayers - 1`. Out of range is ignored.
 @param value the name. Empty clears it.
 @returns nothing.

## `LayerNames::indexOf`

```cpp
int indexOf(std::string_view value) const
```

 Looks a layer up by name.
 @param value the name to find. Comparison is exact and case-sensitive —
        a case-insensitive match would make `Player` and `player` the same
        layer in code and different ones in a diff.
 @returns the layer index, or **-1 when the name is unknown**. Callers
          must check: silently returning 0 would put an object on the
          default layer, which is visible to everything.

## `LayerNames::mask`

```cpp
LayerMask mask(const std::vector<std::string> & names) const
```

 Builds a mask from names.
 @param names the layers to include. **Unknown names are skipped and
        logged**, rather than silently contributing nothing — a typo in a
        mask is otherwise indistinguishable from a deliberately narrow
        one.
 @returns the mask. Empty when no name resolved.

## `LayerNames::all`

```cpp
const std::vector<std::string> & all() const
```

 @returns every name, indexed by layer. Always `kMaxLayers` long, with
          empty strings for unnamed layers.

## `SettingsStore`

```cpp
class SettingsStore
```

 A typed key-value store backed by a YAML file.

 Keys are **dotted paths** — `render.shadows.enabled` — which nest in the
 file, so a settings file reads as a grouped document rather than a flat list
 of long keys. Nesting is created on demand by `set`.

 **Every getter takes a fallback and none of them can fail.** A missing key, a
 key of the wrong type, and a file that would not parse all produce the
 fallback. That is the property that makes it impossible for a config file to
 break a program that reads it correctly.

## `SettingsStore::SettingsStore`

```cpp
SettingsStore()
```

 Constructs an empty store holding no values.

## `SettingsStore::SettingsStore`

```cpp
SettingsStore(const SettingsStore &)
```

 Not copyable: it owns a parsed document.

## `SettingsStore::operator=`

```cpp
SettingsStore & operator=(const SettingsStore &)
```

 Not copyable; see the copy constructor.
 @returns nothing -- deleted.

## `SettingsStore::SettingsStore`

```cpp
SettingsStore(SettingsStore && other)
```

 Moves the store.
 @param other the store to move from.

## `SettingsStore::operator=`

```cpp
SettingsStore & operator=(SettingsStore && other)
```

 Moves the store.
 @param other the store to move from.
 @returns this store.

## `SettingsStore::load`

```cpp
bool load(const std::string & path)
```

 Loads from a file on the native filesystem.

 **Never fails in a way that stops a caller starting up.** A missing file
 leaves the store empty and returns true — a project that has never saved
 settings is not an error. A file that exists but will not parse is
 reported, logged, and leaves the store empty; the caller may then choose
 to preserve the bad file rather than overwrite it.
 @param path the file to read. Native path, not a VFS path — settings are
        read before mounts are decided.
 @returns false **only** when the file existed and could not be parsed.

## `SettingsStore::loadFromString`

```cpp
bool loadFromString(std::string_view text)
```

 Replaces the contents from YAML text.
 @param text the document.
 @returns whether it parsed. The store is left empty when it did not.

## `SettingsStore::save`

```cpp
bool save(const std::string & path) const
```

 Writes to a file on the native filesystem, creating parent directories.
 @param path the file to write.
 @returns whether it was written.

## `SettingsStore::toString`

```cpp
std::string toString() const
```

 @returns the store as YAML text.

## `SettingsStore::has`

```cpp
bool has(std::string_view key) const
```

 @param key a dotted path.
 @returns whether a value exists at that key.

## `SettingsStore::getBool`

```cpp
bool getBool(std::string_view key, bool fallback) const
```

 @param key a dotted path.
 @param fallback returned when the key is absent or not a boolean.
 @returns the value or the fallback.

## `SettingsStore::getInt`

```cpp
std::int64_t getInt(std::string_view key, std::int64_t fallback) const
```

 @param key a dotted path.
 @param fallback returned when the key is absent or not an integer.
 @returns the value or the fallback.

## `SettingsStore::getFloat`

```cpp
double getFloat(std::string_view key, double fallback) const
```

 @param key a dotted path.
 @param fallback returned when the key is absent or not a number.
 @returns the value or the fallback.

## `SettingsStore::getString`

```cpp
std::string getString(std::string_view key, std::string fallback) const
```

 @param key a dotted path.
 @param fallback returned when the key is absent.
 @returns the value or the fallback.

## `SettingsStore::setBool`

```cpp
void setBool(std::string_view key, bool value)
```

 Sets a boolean.
 @param key a dotted path; intermediate maps are created.
 @param value the value.
 @returns nothing.

## `SettingsStore::setInt`

```cpp
void setInt(std::string_view key, std::int64_t value)
```

 Sets an integer.
 @param key a dotted path; intermediate maps are created.
 @param value the value.
 @returns nothing.

## `SettingsStore::setFloat`

```cpp
void setFloat(std::string_view key, double value)
```

 Sets a number.
 @param key a dotted path; intermediate maps are created.
 @param value the value.
 @returns nothing.

## `SettingsStore::setString`

```cpp
void setString(std::string_view key, std::string_view value)
```

 Sets a string.
 @param key a dotted path; intermediate maps are created.
 @param value the value.
 @returns nothing.

## `SettingsStore::readLayerNames`

```cpp
LayerNames readLayerNames() const
```

 Reads the layer-name table out of this store (T0085.1).

 Stored under `layers` as a sequence, so the file reads as a list of names
 in layer order rather than as numbered keys.
 @returns the table. Layers absent from the file are unnamed.

## `SettingsStore::writeLayerNames`

```cpp
void writeLayerNames(const LayerNames & names)
```

 Writes the layer-name table into this store.
 @param names the table to write. Trailing unnamed layers are omitted, so
        a project naming three layers gets three lines rather than 32.
 @returns nothing.
