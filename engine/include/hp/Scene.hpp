// Scene, entities and the core components (T0021, D17, D21).
//
// A Scene owns an `entt::registry` and everything in it. It is the container the
// game and the editor both manipulate, and it is deliberately the first piece of
// the data model — assets and projects both reference scenes, so it cannot come
// after them.
//
// **All persistent state lives here, owned by the engine, never in a gameplay
// module.** Statics inside a module are destroyed when it unloads, so anything
// kept there is gone after a hot reload (T0048). That single rule is what decides
// whether hot reload is reliable or a source of baffling bugs, and it is why the
// registry is engine-owned rather than handed to gameplay to keep.
//
// **Components reference assets by GUID, never by pointer.** Pointers do not
// serialize and do not survive an asset reload. This is the most important rule
// in the data model and there is no exception to it.
//
// Two things this header does *not* do, both on purpose:
//
//   * **It does not decide when propagation runs.** `propagateTransforms` is a
//     single pass the frame calls at phases 7 and 9 (D17); the Scene knows how to
//     recompute, not when.
//   * **It does not defer structural changes.** `create` and `destroy` take
//     effect immediately. D17 places queued entity creation and destruction at
//     frame phase 5 (structural apply), and the queue belongs to whatever drives
//     the frame — not to the container. Calling `destroy` from inside an
//     iteration is the caller's problem, exactly as it is with entt.
#pragma once

#include <hp/Api.hpp>
// `Camera` is a scene component but its vocabulary and its projection maths
// belong together (T0130), so it is defined there and included here rather than
// being split across two headers that have to agree.
#include <hp/Camera.hpp>
#include <hp/Guid.hpp>
#include <hp/Math.hpp>
#include <hp/Reflect.hpp>

#include <entt/entity/registry.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hp {

class Scene;

/// Stable identity, added automatically to every entity.
///
/// The GUID is what survives serialization, a play-mode clone and a reload;
/// `entt::entity` is a slot index that is reused and means nothing outside the
/// registry that issued it. Anything persisted refers to the GUID.
struct Id {
    /// The entity's stable identity.
    Guid guid;
};

/// A human-readable label, added automatically to every entity.
///
/// **This is the editor's name for the entity, not player-facing text**, and it
/// must not be a string-table key — see T0112's convention in
/// `06-engine-conventions.md`. `Door_01` and `PlayerSpawn` are what belongs
/// here. A name a player reads is a different, keyed component that the game
/// defines.
struct Tag {
    /// The label. Not required to be unique; nothing looks entities up by it.
    std::string name;
};

/// Position, rotation and scale **relative to the parent**.
///
/// Pure data, and pure *local* data. The world matrix is not stored here and is
/// not this ticket's concern (T0101). Keeping the local transform free of any
/// derived state is what lets a transform be copied, serialized and diffed
/// without needing to know whether a cached world matrix is stale.
struct Transform {
    /// Translation relative to the parent, or to the world when unparented.
    float3 position{0.0F, 0.0F, 0.0F};

    /// Rotation relative to the parent. Identity is `(0, 0, 0, 1)`.
    Quaternion rotation{0.0F, 0.0F, 0.0F, 1.0F};

    /// Scale relative to the parent.
    float3 scale{1.0F, 1.0F, 1.0F};
};

/// The world transform, plus the one from the previous propagation (T0101).
///
/// Derived state, written **only** by `Scene::propagateTransforms`. Assigning to
/// it by hand is silently undone on the next pass, which is why there is no API
/// that hands it out mutably.
///
/// `previous` exists from day one rather than being retrofitted: physics
/// interpolation (T0057's alpha) needs a previous/current pair, and motion
/// vectors for TAA and motion blur need one eventually (T0096 leaves the hook
/// open). One extra matrix per entity now is cheaper than touching every
/// transform consumer later. An entity that did not move has
/// `previous == current`, which is the correct answer rather than a special case.
struct WorldTransform {
    /// The world matrix as of the last propagation.
    float4x4 current;

    /// The world matrix as of the propagation before that.
    float4x4 previous;
};

/// Marks an entity whose world transform needs recomputing.
///
/// A tag component rather than a bool inside `Transform`, so propagation can ask
/// entt for the dirty set instead of visiting every entity to read a flag — and
/// so an unchanged subtree genuinely costs nothing.
struct DirtyTransform {};

/// The parent link and the child list, maintained **only** by `Scene`.
///
/// Separate from `Transform` rather than folded into it, and the split is the
/// whole reason this compiles into something maintainable: a transform is data a
/// caller may assign freely, while a hierarchy carries an invariant — a parent's
/// `children` must contain exactly the entities whose `parent` is that parent.
/// A `parent` field sitting inside `Transform` invites `t.parent = e`, which
/// silently breaks that invariant and produces a child that is transformed by a
/// parent unaware of it. Use `Scene::setParent`.
///
/// Present on every entity, including roots, because attaching one later would
/// invalidate every pointer entt handed out for the existing pool.
struct Hierarchy {
    /// The parent, or `entt::null` for a root entity.
    entt::entity parent{entt::null};

    /// Direct children, in insertion order. Order is stable and is not sorted;
    /// serialization and the hierarchy panel both rely on that.
    std::vector<entt::entity> children;
};

/// Draws a mesh with a material. Both are asset GUIDs, never pointers.
struct MeshRenderer {
    /// GUID of the mesh asset. A default-constructed GUID means "nothing to
    /// draw", which is a legitimate state rather than an error — an entity can
    /// exist before its asset is assigned.
    Guid mesh;

    /// GUID of the material asset. Default means the renderer's fallback
    /// material, so a mesh with no material assigned is still visible rather
    /// than silently absent.
    Guid material;
};

/// A cheap, non-owning handle to one entity in one scene.
///
/// Copy it freely; it is a pointer and an index. **Do not store it across a
/// frame** — it does not know whether the entity still exists, and a destroyed
/// entity's slot is reused, so a stale handle can silently address a different
/// entity. Persist a `Guid` and resolve it with `Scene::find` instead.
///
/// `HP_API` because the engine builds with hidden visibility: `valid` and `guid`
/// are the two members defined out of line, and without the export they link
/// inside the engine and are undefined in every consumer.
class HP_API Entity {
public:
    /// Constructs a null handle that belongs to no scene.
    Entity() = default;

    /// Constructs a handle. Prefer `Scene::create`; this exists for the scene
    /// itself and for code that already holds a raw entt entity.
    /// @param scene the owning scene. Must outlive the handle.
    /// @param handle the entt entity, which must come from `scene`'s registry.
    Entity(Scene& scene, entt::entity handle) : scene_(&scene), handle_(handle) {}

    /// @returns whether this handle refers to a live entity in its scene. False
    ///          for a default-constructed handle and for one whose entity has
    ///          been destroyed.
    [[nodiscard]] bool valid() const;

    /// @returns the entity's stable identity, or a default GUID when the handle
    ///          is not valid.
    [[nodiscard]] Guid guid() const;

    /// @returns the owning scene, or nullptr for a default-constructed handle.
    [[nodiscard]] Scene* scene() const { return scene_; }

    /// @returns the underlying entt entity, for code working with the registry
    ///          directly. `entt::null` when the handle is default-constructed.
    [[nodiscard]] entt::entity raw() const { return handle_; }

    /// Adds a component, constructing it in place, and replaces any existing one.
    ///
    /// Replacing rather than asserting because the alternative — a distinct
    /// `add` and `replace` — is a coin flip at every call site that ends in a
    /// runtime error nobody can act on.
    /// @param args arguments forwarded to the component's constructor.
    /// @returns a reference to the component, valid until the pool reallocates.
    template <typename Component, typename... Args>
    Component& add(Args&&... args);

    /// @returns whether the entity has this component.
    template <typename Component>
    [[nodiscard]] bool has() const;

    /// @returns a reference to the component. Undefined if it is absent — check
    ///          with `has`, or use `tryGet`.
    template <typename Component>
    [[nodiscard]] Component& get();

    /// @returns a pointer to the component, or nullptr when absent.
    template <typename Component>
    [[nodiscard]] Component* tryGet();

    /// Removes the component if present.
    /// @returns the number of components removed: 1, or 0 when absent.
    template <typename Component>
    std::size_t remove();

    /// @param a the left-hand handle.
    /// @param b the right-hand handle.
    /// @returns whether two handles refer to the same entity in the same scene.
    ///          Two null handles compare equal.
    friend bool operator==(Entity a, Entity b) {
        return a.scene_ == b.scene_ && a.handle_ == b.handle_;
    }

    /// @param a the left-hand handle.
    /// @param b the right-hand handle.
    /// @returns whether two handles differ; see `operator==`.
    friend bool operator!=(Entity a, Entity b) { return !(a == b); }

private:
    Scene* scene_{nullptr};
    entt::entity handle_{entt::null};
};

/// What a reparent does to the entity's position in the world (T0101).
enum class Reparent {
    /// Keep the local transform; the entity moves with its new parent.
    ///
    /// The default because it is what code means: parenting a weapon to a hand
    /// socket should snap it to the socket.
    KeepLocal,

    /// Keep the world transform; the local transform is recomputed so the entity
    /// does not visibly move.
    ///
    /// What an editor drag in the hierarchy panel must do (T0035). Dropping an
    /// object onto a new parent and watching it teleport is the behaviour users
    /// read as a bug.
    KeepWorld,
};

/// How a cloned scene treats the identities inside it.
///
/// The two modes are not variations on one operation — they have **opposite**
/// semantics, and conflating them is how a prefab instance ends up silently
/// sharing state with its template.
enum class CloneIds {
    /// Keep every GUID (T0037's play-mode clone). Entity references, signal
    /// connections and save data all keep resolving inside the clone.
    ///
    /// The consequence is binding on the rest of the engine: **GUID lookup must
    /// be per-scene and never global**, because two live scenes now contain the
    /// same GUIDs.
    Preserve,

    /// Issue fresh GUIDs (editor duplicate, prefab instantiate).
    ///
    /// The hierarchy is remapped, so a duplicated subtree parents to its own
    /// copies. **References held in components are not remapped** — that needs
    /// the old-to-new map and the reference type itself, both of which are
    /// T0071's, which must extend this when it lands.
    Regenerate,
};

/// Owns an `entt::registry` and every entity in it.
///
/// Not copyable, because a copy has to answer the GUID question above and there
/// is no correct default answer. Use `clone` and say which you mean.
class HP_API Scene {
public:
    /// Constructs an empty scene.
    Scene();

    /// Destroys every entity in the scene.
    ~Scene();

    /// Not copyable — see the class comment. Use `clone`.
    Scene(const Scene&) = delete;

    /// Not copyable; see the copy constructor.
    /// @returns nothing -- deleted.
    Scene& operator=(const Scene&) = delete;

    /// Moves the scene. Handles referring to the moved-from scene are invalid.
    /// @param other the scene to move from.
    Scene(Scene&& other) noexcept;

    /// Moves the scene, destroying whatever this one held.
    /// @param other the scene to move from.
    /// @returns this scene.
    Scene& operator=(Scene&& other) noexcept;

    /// Creates an entity carrying `Id`, `Tag`, `Transform` and `Hierarchy`.
    ///
    /// All four are automatic because every consumer assumes them: the editor
    /// needs a name and a transform to show, serialization needs an identity,
    /// and an optional hierarchy would mean a null check at every traversal step.
    /// @param name the entity's editor label. Not player-facing text (T0112).
    /// @returns a handle to the new entity.
    Entity create(std::string name = "Entity");

    /// Creates an entity with a specific identity, for deserialization.
    ///
    /// @param guid the identity to give it. Must not already exist in this
    ///        scene; the scene asserts rather than silently producing a
    ///        duplicate that would make `find` ambiguous.
    /// @param name the entity's editor label.
    /// @returns a handle to the new entity.
    Entity createWithGuid(Guid guid, std::string name = "Entity");

    /// Destroys an entity **and its descendants**, immediately.
    ///
    /// Descendants go too because the alternative is orphaned entities holding a
    /// parent link into a reused slot — which does not fail, it silently
    /// reparents them to whatever is created next.
    /// @param entity the entity to destroy. A stale or null handle is ignored.
    void destroy(Entity entity);

    /// @returns whether the handle refers to a live entity in this scene.
    /// @param entity the handle to check.
    [[nodiscard]] bool valid(Entity entity) const;

    /// Looks an entity up by its stable identity.
    ///
    /// Per-scene, never global — `CloneIds::Preserve` puts the same GUIDs in two
    /// live scenes at once, so a global map would be ambiguous by construction.
    /// @param guid the identity to resolve.
    /// @returns the entity, or nullopt when this scene has no such GUID.
    [[nodiscard]] std::optional<Entity> find(Guid guid) const;

    /// Reparents an entity, maintaining both sides of the link.
    ///
    /// The only supported way to change a `Hierarchy`. Detaches from the old
    /// parent first, so calling it repeatedly cannot duplicate a child entry.
    /// @param child the entity to reparent.
    /// @param parent the new parent, or a null handle to make `child` a root.
    /// @param mode whether to keep the local or the world transform. Defaults to
    ///        keeping the local one, which is what code usually means; an editor
    ///        drag wants `KeepWorld`.
    /// @returns false when the handles are invalid or when `parent` is `child`
    ///          or one of its descendants — a cycle would make traversal
    ///          non-terminating, so it is refused rather than accepted.
    bool setParent(Entity child, Entity parent, Reparent mode = Reparent::KeepLocal);

    /// Writes an entity's local transform and marks its subtree for propagation.
    ///
    /// **The only write that is guaranteed to reach the world transform.**
    /// `Entity::get<Transform>()` still hands out a mutable reference — entt's
    /// storage is not going to stop it — and a write through that reference is
    /// invisible to propagation until something marks the entity dirty. Prefer
    /// this; if you must write directly, call `markTransformDirty` after.
    /// @param entity the entity to move.
    /// @param transform the new local transform.
    /// @returns nothing.
    void setLocalTransform(Entity entity, const Transform& transform);

    /// Marks an entity's world transform stale, so the next propagation
    /// recomputes it and everything below it.
    /// @param entity the entity whose local transform changed.
    /// @returns nothing.
    void markTransformDirty(Entity entity);

    /// Recomputes every stale world transform, in one pass, parents before
    /// children.
    ///
    /// Belongs at frame phases 7 and 9 (D17): phase 7 serves the followers that
    /// run at phase 8, and phase 9 catches whatever phase 8 itself moved. A
    /// clean scene walks the hierarchy and writes nothing.
    /// @returns how many entities were recomputed, which is what a test asserts
    ///          on to prove an unchanged subtree cost nothing.
    std::size_t propagateTransforms();

    /// @returns the entity's world matrix as of the last propagation, or
    ///          identity when the handle is invalid.
    ///
    /// A lookup, never a parent-chain walk — the walk is the propagation pass's
    /// job, done once for the whole scene rather than once per query.
    /// @param entity the entity to read.
    [[nodiscard]] const float4x4& worldTransform(Entity entity) const;

    /// @returns the entity's world matrix as of the propagation before last, for
    ///          interpolation and motion vectors. Identity for an invalid handle.
    /// @param entity the entity to read.
    [[nodiscard]] const float4x4& previousWorldTransform(Entity entity) const;

    /// @returns every root entity, in creation order.
    [[nodiscard]] std::vector<Entity> roots();

    /// @returns how many entities the scene holds.
    [[nodiscard]] std::size_t size() const;

    /// Copies the scene.
    /// @param ids whether to preserve or regenerate identities. There is no
    ///        default: the two have opposite semantics and picking one silently
    ///        is the failure this parameter exists to prevent.
    /// @returns the new scene.
    [[nodiscard]] Scene clone(CloneIds ids) const;

    /// @returns the underlying registry, for views, groups and anything this
    ///          API does not wrap. Using it is normal, not an escape hatch —
    ///          entt's iteration is the point of using entt.
    [[nodiscard]] entt::registry& registry() { return registry_; }

    /// @returns the underlying registry; see the non-const overload.
    [[nodiscard]] const entt::registry& registry() const { return registry_; }

private:
    void detachFromParent(entt::entity child);
    void destroyRecursive(entt::entity entity);
    std::size_t propagateSubtree(entt::entity entity, const float4x4& parentWorld,
                                 bool ancestorDirty);
    void applyWorldTransform(entt::entity entity, const float4x4& world,
                             const float4x4& parentWorld);

    entt::registry registry_;

    // A map rather than a scan over `view<Id>`. Selection resolves a GUID every
    // frame the inspector is open and deserialization resolves one per reference,
    // so the linear version is O(n) in the two places it is called most.
    std::unordered_map<Guid, entt::entity> byGuid_;
};

/// Registers a component type for reflection **and** for scene cloning.
///
/// One call, deliberately. T0053 landed reflection with no registry to hook into
/// and left a second list to keep in sync; the whole point of this function is
/// that there is no second list. A type registered here is inspectable,
/// serializable and clonable, and a type that is not registered is silently
/// dropped by `Scene::clone` — which is the failure mode to watch for, and the
/// reason the engine's own components are registered in one place at startup.
///
/// Identity is the **name**, never `entt::type_index`: T0095 measured that index
/// to be a per-module number with no meaning across the module boundary.
/// @param name the stable type name, e.g. "Transform". Must be unique and must
///        not change once anything has been serialized with it.
/// @returns the same builder `reflect` returns, so fields are described in the
///          same expression that registers the type — which is what stops the
///          field list and the type list from drifting apart.
template <typename Component>
TypeBuilder<Component> registerComponent(const char* name);

namespace detail {

/// Records the clone operation for a component type.
/// @param name the stable type name.
/// @param copy a function copying the component from one registry to another.
/// @returns nothing.
HP_API void registerComponentClone(const char* name,
                                   void (*copy)(const entt::registry&, entt::entity,
                                                entt::registry&, entt::entity));

} // namespace detail

/// Registers the engine's own components: `Id`, `Tag`, `Transform`, `Hierarchy`,
/// `MeshRenderer` and `Camera`.
///
/// Called by `Scene`'s first construction, so a scene is never missing them.
/// Safe to call repeatedly.
/// @returns nothing.
HP_API void registerCoreComponents();

// --- template definitions -------------------------------------------------
//
// Below the API rather than beside it: the declarations above are what a reader
// needs, and interleaving bodies with them costs more than the split does.

template <typename Component, typename... Args>
Component& Entity::add(Args&&... args) {
    return scene_->registry().emplace_or_replace<Component>(handle_,
                                                            std::forward<Args>(args)...);
}

template <typename Component>
bool Entity::has() const {
    return scene_ != nullptr && scene_->registry().all_of<Component>(handle_);
}

template <typename Component>
Component& Entity::get() {
    return scene_->registry().get<Component>(handle_);
}

template <typename Component>
Component* Entity::tryGet() {
    return scene_ == nullptr ? nullptr : scene_->registry().try_get<Component>(handle_);
}

template <typename Component>
std::size_t Entity::remove() {
    return scene_->registry().remove<Component>(handle_);
}

template <typename Component>
TypeBuilder<Component> registerComponent(const char* name) {
    detail::registerComponentClone(
        name,
        [](const entt::registry& from, entt::entity source, entt::registry& to,
           entt::entity target) {
            if (const auto* component = from.try_get<Component>(source)) {
                to.emplace_or_replace<Component>(target, *component);
            }
        });
    return reflect<Component>(name);
}

} // namespace hp
