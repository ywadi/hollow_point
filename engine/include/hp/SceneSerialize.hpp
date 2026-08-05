// Scenes to and from `.hpscene` YAML (T0022).
//
// **Nothing here knows what a component is.** Save walks the component registry
// (`hp::detail::registeredComponents`) and writes each type's reflected
// properties through T0020's serializer; load does the reverse. Adding a
// component type touches its one `registerComponent<T>` declaration and nothing
// in this file, which is the whole reason T0053 exists — a central `if/else`
// over component types is the thing that rots fastest in an engine of this
// shape.
//
// ## The schema
//
// ```yaml
// version: 1
// entities:
//   - guid: 0123456789abcdef
//     name: Player
//     parent: fedcba9876543210      # absent for a root
//     components:
//       Transform:
//         position: [1, 2, 3]
//         rotation: [0, 0, 0, 1]
//         scale: [1, 1, 1]
//       MeshRenderer:
//         mesh: 00112233445566ff
//         layers: 1
// ```
//
// **`name` and `parent` sit on the entity, not among its components**, and that
// is a schema decision rather than a convenience. `Tag` and `Hierarchy` are both
// registered component types, so the generic loop would happily write them —
// and writing `Hierarchy` produces a corrupt file that looks fine, because its
// fields are `entt::entity` handles: registry slot indices, reused after a
// destroy, so on load they silently address *different* entities. The link is
// therefore written as a **GUID and resolved in a second pass**, which is
// T0101.1's representation decision consumed rather than reinvented.
//
// `Id` and `WorldTransform` are excluded for the same family of reason: `Id`
// duplicates the entity's own `guid` key, and `WorldTransform` is derived from
// `Transform` by propagation, so storing it lets two truths disagree. All four
// stay *reflected* — the inspector still wants them — and are marked
// non-serialized explicitly rather than being inferred from having no properties
// today, which would start writing garbage the moment somebody added one.
//
// ## Versioning
//
// The `version` field exists from the first file this engine ever writes,
// because it cannot be added retroactively to files already in the wild — a
// file with no version can only be guessed at, never migrated. **Migration
// itself is T0082's**, and is deliberately not built here. What this layer does
// is the half that must exist now: stamp the version, and *refuse* a file from a
// newer schema rather than loading it partially and saving the loss back.
//
// ## Two forms, one of which is authoritative
//
// **YAML is the truth and the cook is a cache** (T0020). `cookScene` writes the
// binary form and `loadSceneFromCooked` reads it, and every way that read can
// fail — wrong container version, wrong schema, changed source, truncated, or a
// component type this build no longer has — collapses to a single answer:
// `SceneLoadStatus::Stale`, meaning *load the YAML instead and cook it again*.
// A caller never has to distinguish them, which is why there is one status
// rather than six.
//
// The last of those is worth naming, because it is the one asymmetry between
// the two paths. The YAML path **preserves** a component it cannot interpret,
// as text (see `UnknownComponents`). The binary path cannot: it holds cooked
// bytes, and bytes for a type that is not registered cannot be turned back into
// anything a save could write. So rather than dropping data the YAML beside it
// still has, a cook that names an absent type declares itself stale.
#pragma once

#include <hp/Api.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hp {

class Scene;

/// One component from a file that this build has no registered type for.
///
/// **Kept as text, not as a parsed structure.** The moment the subtree is
/// decomposed into something this build understands, it is no longer what the
/// file said — and writing that back is a lossy guess rather than a round trip.
struct UnknownComponent {
    /// The stable type name, exactly as the file spelled it.
    std::string type;

    /// The raw YAML subtree, key included, as `YamlNode::emitSubtree` produced
    /// it. Grafted back verbatim on save.
    std::string yaml;
};

/// The unknown components an entity carried, so a save can put them back (D23).
///
/// **A component, and therefore on the `Scene`** — not state held by the file
/// layer. The round trip that matters is *load → edit → save*, which means the
/// blob has to survive everything that happens in between, including a clone
/// and a play-mode copy. Registered like any other component so it is copied by
/// the same mechanism; marked non-serialized so the generic save loop does not
/// write it as a component named `UnknownComponents`, because `saveSceneToString`
/// writes its contents back in their own names instead.
struct UnknownComponents {
    /// The preserved subtrees, in the order the file listed them.
    std::vector<UnknownComponent> items;
};

/// Turns preserved unknown components into real ones, for types that now exist.
///
/// Call after a gameplay module loads or reloads (T0048, T0062): a type that was
/// absent at load — because the module had not built, or was mid-rename — is the
/// case `UnknownComponents` exists for, and this is the other half of it. An item
/// whose type is still absent is left untouched, so calling this repeatedly is
/// safe and costs one registry lookup per preserved item.
///
/// @param scene the scene to walk.
/// @returns how many components were materialised. Zero is the ordinary answer.
[[nodiscard]] HP_API int materialiseUnknownComponents(Scene& scene);

/// The schema version written by this build.
///
/// Bump it when the *shape* changes — a field renamed, a component split, a
/// meaning revised — and add the matching migration on T0082. Adding a property
/// to a component does **not** need a bump: reading is lenient, so an older file
/// simply leaves the new field at its default.
inline constexpr int kSceneSchemaVersion = 1;

/// Serializes a scene to `.hpscene` YAML.
///
/// @param scene the scene to write. Not modified.
/// @returns the document text. Never empty on success — an empty scene still
///          carries its `version` and an empty `entities` sequence, which is
///          what distinguishes "saved nothing" from "failed".
[[nodiscard]] HP_API std::string saveSceneToString(const Scene& scene);

/// Why a load failed, for a message a person can act on.
enum class SceneLoadStatus {
    Ok,
    /// The text is not YAML, or not a mapping.
    Malformed,
    /// Written by a newer build than this one. **Refused deliberately**: the
    /// alternative is loading the fields we understand and silently discarding
    /// the rest on the next save, which destroys work.
    NewerSchema,

    /// The cooked bytes cannot be used; load the YAML and cook it again.
    ///
    /// Only `loadSceneFromCooked` returns this, and it is **not an error** —
    /// a cache miss is the ordinary cost of the source having moved on. The
    /// specific `CookStatus` is logged.
    Stale,
};

/// The outcome of a load.
struct SceneLoadResult {
    SceneLoadStatus status = SceneLoadStatus::Ok;
    /// Entities created.
    int entities = 0;
    /// Component types named in the file that no longer exist here — a gameplay
    /// module that failed to build, or a type renamed mid-refactor.
    ///
    /// From YAML these are **preserved**, not dropped: each is kept verbatim in
    /// an `UnknownComponents` component and written back by the next save (D23),
    /// so this counts what was set aside rather than what was lost. From a cook
    /// there is nothing to preserve, and the load reports `Stale` instead.
    int unknownComponents = 0;
};

/// Loads a scene from `.hpscene` YAML, replacing whatever @p scene held.
///
/// **GUIDs are preserved**, not regenerated. Regenerating them breaks every
/// reference into the scene from outside and corrupts a project subtly enough
/// to be worth an explicit test.
///
/// @param scene the scene to fill. Cleared first.
/// @param text the document.
/// @param name a name for log messages, e.g. the virtual path.
/// @returns what happened. On anything but `Ok` the scene is left empty rather
///          than half-populated.
[[nodiscard]] HP_API SceneLoadResult loadSceneFromString(Scene& scene, std::string_view text,
                                                         std::string_view name = "<memory>");

/// Cooks a scene into the binary cache form (T0020.4).
///
/// The payload, inside the `hp/Cook.hpp` container and little-endian throughout:
///
/// ```text
///   u32     entity count
///   per entity:
///     string  guid              canonical text, as in the YAML
///     string  name
///     string  parent guid       empty for a root
///     u32     component count
///     per component:
///       string  type name       the stable name, never a type index (T0095)
///       u64     byte length     so a reader can step over a type it lacks
///       bytes   cookProperties payload
///     u32     preserved-unknown count
///     per preserved unknown:
///       string  type name
///       string  raw YAML subtree
/// ```
///
/// **The type name and the length are both there for the same reason the YAML
/// path keeps a subtree**: a build whose type set has moved on must be able to
/// say *which* type it could not read, and reach the end of the stream to say it
/// about all of them, instead of losing sync at the first one.
///
/// @param scene the scene to cook. Not modified.
/// @param sourceHash `hashSource` of the YAML this scene came from, which is
///        what `loadSceneFromCooked` compares against — **the staleness check,
///        and the reason nothing here looks at a timestamp**.
/// @returns the complete file contents, ready to write through the VFS.
[[nodiscard]] HP_API std::vector<std::byte> cookScene(const Scene& scene,
                                                      std::uint64_t sourceHash);

/// Loads a scene from cooked bytes, replacing whatever @p scene held.
///
/// @param scene the scene to fill. Cleared first, and left empty on anything but
///        `Ok` — a half-populated scene saved back is how a cache bug becomes a
///        data-loss bug.
/// @param bytes the file contents, as `cookScene` produced them.
/// @param expectedSourceHash `hashSource` of the YAML available now.
/// @param name a name for log messages, e.g. the virtual path.
/// @returns `Ok`, or `Stale` — which is not an error and means *load the YAML
///          and cook it again*. `SceneLoadResult::unknownComponents` still
///          counts the types that could not be read, so the log says which.
[[nodiscard]] HP_API SceneLoadResult loadSceneFromCooked(Scene& scene,
                                                         const std::vector<std::byte>& bytes,
                                                         std::uint64_t expectedSourceHash,
                                                         std::string_view name = "<memory>");

} // namespace hp
