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
#pragma once

#include <hp/Api.hpp>

#include <string>
#include <string_view>

namespace hp {

class Scene;

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
};

/// The outcome of a load.
struct SceneLoadResult {
    SceneLoadStatus status = SceneLoadStatus::Ok;
    /// Entities created.
    int entities = 0;
    /// Component types named in the file that no longer exist here — a gameplay
    /// module that failed to build, or a type renamed mid-refactor.
    ///
    /// **Counted and logged, not preserved**, which is a known shortfall rather
    /// than the intended end state: D23's policy is to keep the raw subtree and
    /// re-emit it, so that a save-after-load does not destroy data belonging to
    /// a type that is merely absent today. See T0022's notes.
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

} // namespace hp
