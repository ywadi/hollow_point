# `<hp/SceneSerialize.hpp>`

*Generated from `engine/include/hp/SceneSerialize.hpp` — do not edit.*

```cpp
#include <hp/SceneSerialize.hpp>
```

10 public declaration(s), 10 documented.

## `UnknownComponent`

```cpp
struct UnknownComponent
```

 One component from a file that this build has no registered type for.

 **Kept as text, not as a parsed structure.** The moment the subtree is
 decomposed into something this build understands, it is no longer what the
 file said — and writing that back is a lossy guess rather than a round trip.

## `UnknownComponents`

```cpp
struct UnknownComponents
```

 The unknown components an entity carried, so a save can put them back (D23).

 **A component, and therefore on the `Scene`** — not state held by the file
 layer. The round trip that matters is *load → edit → save*, which means the
 blob has to survive everything that happens in between, including a clone
 and a play-mode copy. Registered like any other component so it is copied by
 the same mechanism; marked non-serialized so the generic save loop does not
 write it as a component named `UnknownComponents`, because `saveSceneToString`
 writes its contents back in their own names instead.

## `materialiseUnknownComponents`

```cpp
int materialiseUnknownComponents(Scene & scene)
```

 Turns preserved unknown components into real ones, for types that now exist.

 Call after a gameplay module loads or reloads (T0048, T0062): a type that was
 absent at load — because the module had not built, or was mid-rename — is the
 case `UnknownComponents` exists for, and this is the other half of it. An item
 whose type is still absent is left untouched, so calling this repeatedly is
 safe and costs one registry lookup per preserved item.

 @param scene the scene to walk.
 @returns how many components were materialised. Zero is the ordinary answer.

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
| `Stale` | 3 |

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

 ## Authoring mode (T0139)

 A file written by hand — or by a model — may omit what it does not care
 about, and two leniencies exist for exactly that:

   * **an entity with no `guid` receives a fresh one.** Nothing could have
     referenced an identity that was never written down, so issuing one breaks
     nothing. A `guid` that is *present and malformed* is still refused, and
     that refusal is what makes this safe rather than merely convenient: it
     stops a typo from silently becoming a new entity and orphaning every
     `parent` that named the value the author meant;
   * **`parent` may name an entity instead of naming a GUID**, when the value
     is not 16 hex digits. A GUID always wins, so a file this engine wrote
     never takes that path, and a name carried by more than one entity is
     refused rather than resolved to whichever came first.

 Both are edge affordances only. **Save always writes GUIDs**, so a
 hand-authored file is normalised the first time it is saved and name
 references never exist inside the engine.

 @param scene the scene to fill. Cleared first.
 @param text the document.
 @param name a name for log messages, e.g. the virtual path.
 @returns what happened. On anything but `Ok` the scene is left empty rather
          than half-populated.

## `cookScene`

```cpp
std::vector<std::byte> cookScene(const Scene & scene, std::uint64_t sourceHash)
```

 Cooks a scene into the binary cache form (T0020.4).

 The payload, inside the `hp/Cook.hpp` container and little-endian throughout:

 ```text
   u32     entity count
   per entity:
     string  guid              canonical text, as in the YAML
     string  name
     string  parent guid       empty for a root
     u32     component count
     per component:
       string  type name       the stable name, never a type index (T0095)
       u64     byte length     so a reader can step over a type it lacks
       bytes   cookProperties payload
     u32     preserved-unknown count
     per preserved unknown:
       string  type name
       string  raw YAML subtree
 ```

 **The type name and the length are both there for the same reason the YAML
 path keeps a subtree**: a build whose type set has moved on must be able to
 say *which* type it could not read, and reach the end of the stream to say it
 about all of them, instead of losing sync at the first one.

 @param scene the scene to cook. Not modified.
 @param sourceHash `hashSource` of the YAML this scene came from, which is
        what `loadSceneFromCooked` compares against — **the staleness check,
        and the reason nothing here looks at a timestamp**.
 @returns the complete file contents, ready to write through the VFS.

## `loadSceneFromCooked`

```cpp
SceneLoadResult loadSceneFromCooked(Scene & scene, const std::vector<std::byte> & bytes, std::uint64_t expectedSourceHash, std::string_view name)
```

 Loads a scene from cooked bytes, replacing whatever @p scene held.

 @param scene the scene to fill. Cleared first, and left empty on anything but
        `Ok` — a half-populated scene saved back is how a cache bug becomes a
        data-loss bug.
 @param bytes the file contents, as `cookScene` produced them.
 @param expectedSourceHash `hashSource` of the YAML available now.
 @param name a name for log messages, e.g. the virtual path.
 @returns `Ok`, or `Stale` — which is not an error and means *load the YAML
          and cook it again*. `SceneLoadResult::unknownComponents` still
          counts the types that could not be read, so the log says which.
