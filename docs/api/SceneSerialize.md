# `<hp/SceneSerialize.hpp>`

*Generated from `engine/include/hp/SceneSerialize.hpp` — do not edit.*

```cpp
#include <hp/SceneSerialize.hpp>
```

5 public declaration(s), 5 documented.

## `kSceneSchemaVersion`

```cpp
inline constexpr int kSceneSchemaVersion = 1
```

 The schema version written by this build.

 Bump it when the *shape* changes — a field renamed, a component split, a
 meaning revised — and add the matching migration on T0082. Adding a property
 to a component does **not** need a bump: reading is lenient, so an older file
 simply leaves the new field at its default.

## `saveSceneToString`

```cpp
std::string saveSceneToString(const Scene & scene)
```

 Serializes a scene to `.hpscene` YAML.

 @param scene the scene to write. Not modified.
 @returns the document text. Never empty on success — an empty scene still
          carries its `version` and an empty `entities` sequence, which is
          what distinguishes "saved nothing" from "failed".

## `SceneLoadStatus`

```cpp
enum class SceneLoadStatus
```

| Enumerator | Value |
|---|---|
| `Ok` | 0 |
| `Malformed` | 1 |
| `NewerSchema` | 2 |

 Why a load failed, for a message a person can act on.

## `SceneLoadResult`

```cpp
struct SceneLoadResult
```

 The outcome of a load.

## `loadSceneFromString`

```cpp
SceneLoadResult loadSceneFromString(Scene & scene, std::string_view text, std::string_view name)
```

 Loads a scene from `.hpscene` YAML, replacing whatever @p scene held.

 **GUIDs are preserved**, not regenerated. Regenerating them breaks every
 reference into the scene from outside and corrupts a project subtly enough
 to be worth an explicit test.

 @param scene the scene to fill. Cleared first.
 @param text the document.
 @param name a name for log messages, e.g. the virtual path.
 @returns what happened. On anything but `Ok` the scene is left empty rather
          than half-populated.
