// Scene draw submission (T0028): entities in, an explicit draw list out.
//
// The parse step is deliberately **free of the device, the asset pool and the
// camera**. That is not tidiness -- it is what lets a sort or a cull pass be
// inserted between parse and submit without restructuring anything (T0045), and
// what lets the whole step be tested in the fast bucket with no GPU at all.
//
// It is also what T0120.2 requires: submission must stay callable per-camera
// against an arbitrary target, for portals, security monitors and thumbnails.
// A parse step that reached for "the" camera or "the" viewport would have to be
// restructured to get there, which is the retrofit T0120 was filed to avoid.
#pragma once

#include <hp/Api.hpp>
#include <hp/Guid.hpp>
#include <hp/Math.hpp>
#include <hp/Scene.hpp>

#include <cstddef>
#include <vector>

namespace hp {

/// One thing to draw: where it is, and which assets it needs.
///
/// A value, valid for the frame that produced it. It holds **GUIDs, not asset
/// pointers**, because parsing must not require a loaded pool -- an entity can
/// legitimately reference an asset that has not been loaded yet, and deciding
/// what to do about that is the resolve step's job, not the parse step's.
struct DrawItem {
    /// The entity this came from. Kept so a later pass -- picking, a debug
    /// overlay, a per-entity material override -- can get back to the scene
    /// without a second walk.
    entt::entity entity{entt::null};

    /// The world matrix, copied from `WorldTransform::current`.
    ///
    /// Copied rather than pointed at: the parse output outlives the walk, and a
    /// pointer into component storage dangles the moment anything creates an
    /// entity, which entt is free to do by reallocating.
    float4x4 world{};

    /// GUID of the mesh to draw. Never default here -- an entity whose mesh GUID
    /// is unset is dropped during parsing rather than carried as a hole every
    /// later stage has to check.
    Guid mesh;

    /// GUID of the material, or default for the fallback material.
    ///
    /// Default is **not** an error and is not dropped: `MeshRenderer` documents
    /// a default material GUID as meaning the renderer's fallback, so that a
    /// mesh with no material assigned is still visible rather than silently
    /// absent (28.2).
    Guid material;
};

/// The parse step's output, in registry order.
///
/// A plain vector, deliberately: it is the explicit list a sort or cull pass is
/// inserted around. Registry order is **not** a stable guarantee and nothing
/// here should depend on it -- once T0045 sorts, the order is whatever the sort
/// says.
using DrawList = std::vector<DrawItem>;

/// What a parse pass saw, for logging and for tests.
///
/// Separate from the list because "how many were skipped and why" is the part
/// worth reporting when a scene renders nothing, and stuffing it into the list
/// would make every consumer step over it.
struct DrawParseStats {
    /// Entities carrying both a `WorldTransform` and a `MeshRenderer`.
    std::size_t considered = 0;

    /// Entities that produced a `DrawItem`.
    std::size_t drawn = 0;

    /// Entities dropped for an unset mesh GUID.
    ///
    /// **Not an error.** `MeshRenderer` documents a default mesh GUID as "nothing
    /// to draw", which is a legitimate state -- an entity can exist before its
    /// asset is assigned. Counted so a scene that draws nothing can say why.
    std::size_t withoutMesh = 0;
};

/// Collects everything drawable in a scene.
///
/// Filters to entities carrying **both** `WorldTransform` and `MeshRenderer`,
/// dropping those whose mesh GUID is unset. `Transform` alone is not enough:
/// the world matrix is what a draw needs, and it exists only after propagation
/// (T0101), so parsing off `Transform` would silently draw everything at its
/// local position the first frame a hierarchy is built.
///
/// @param scene the scene to walk. Not modified.
/// @param stats optional; filled with what the pass saw. Null to ignore.
/// @returns the draw list, empty when nothing is drawable.
[[nodiscard]] HP_API DrawList parseScene(const Scene& scene, DrawParseStats* stats = nullptr);

} // namespace hp
