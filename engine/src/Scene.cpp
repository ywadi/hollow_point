#include <hp/Scene.hpp>

#include <hp/Log.hpp>
#include <hp/Profiling.hpp>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

namespace hp {
namespace {

const LogCategory kLog("scene");

/// One registered component type's clone operation.
struct ComponentClone {
    const char* name;
    void (*copy)(const entt::registry&, entt::entity, entt::registry&, entt::entity);
};

/// The registered component types.
///
/// A function-local static rather than a namespace-scope one, because the engine
/// is a shared library and a namespace-scope vector's constructor runs at load
/// time in an order nothing here controls. `registerCoreComponents` can then be
/// called from a `Scene` constructor without depending on that order.
std::vector<ComponentClone>& componentClones() {
    static std::vector<ComponentClone> clones;
    return clones;
}

} // namespace

namespace detail {

void registerComponentClone(const char* name,
                            void (*copy)(const entt::registry&, entt::entity,
                                         entt::registry&, entt::entity)) {
    auto& clones = componentClones();
    const auto existing = std::find_if(clones.begin(), clones.end(),
                                       [name](const ComponentClone& clone) {
                                           return std::string_view{clone.name} == name;
                                       });
    if (existing != clones.end()) {
        // Re-registering the same name is normal: a gameplay module reloads and
        // registers its types again. Replace rather than append, or the clone
        // would run twice and the second call would hold a function pointer into
        // the unloaded module.
        existing->copy = copy;
        return;
    }
    clones.push_back({name, copy});
}

} // namespace detail

void registerCoreComponents() {
    static bool done = false;
    if (done) {
        return;
    }
    done = true;

    registerComponent<Id>("Id");
    registerComponent<Tag>("Tag").property<&Tag::name>("name");
    registerComponent<Transform>("Transform")
        .property<&Transform::position>("position")
        .property<&Transform::rotation>("rotation")
        .property<&Transform::scale>("scale");
    registerComponent<Hierarchy>("Hierarchy");
    // WorldTransform is registered so a clone carries it -- recomputing it in
    // the copy would be correct too, but only after a propagation the caller
    // might not have run before reading it.
    //
    // DirtyTransform deliberately is *not* registered. It is an empty type, and
    // entt does not store those: there is no `try_get` for one, so the generic
    // clone operation does not compile for it. `Scene::clone` marks every cloned
    // entity dirty instead, which is both simpler and strictly safer than
    // copying the flag -- a clone of a stale scene is stale too.
    registerComponent<WorldTransform>("WorldTransform");
    registerComponent<MeshRenderer>("MeshRenderer")
        .property<&MeshRenderer::mesh>("mesh")
        .property<&MeshRenderer::material>("material");
    registerComponent<Camera>("Camera")
        .property<&Camera::verticalFov>("verticalFov")
        .property<&Camera::nearPlane>("nearPlane")
        .property<&Camera::farPlane>("farPlane")
        .property<&Camera::orthographic>("orthographic")
        .property<&Camera::orthographicSize>("orthographicSize")
        .property<&Camera::sensorHeightMm>("sensorHeightMm")
        .property<&Camera::exposureEv100>("exposureEv100")
        .property<&Camera::depthOfField>("depthOfField")
        .property<&Camera::aperture>("aperture")
        .property<&Camera::focusDistance>("focusDistance");
}

bool Entity::valid() const {
    return scene_ != nullptr && scene_->valid(*this);
}

Guid Entity::guid() const {
    if (!valid()) {
        return Guid{};
    }
    return scene_->registry().get<Id>(handle_).guid;
}

Scene::Scene() {
    registerCoreComponents();
}

Scene::~Scene() = default;

Scene::Scene(Scene&& other) noexcept = default;

Scene& Scene::operator=(Scene&& other) noexcept = default;

Entity Scene::create(std::string name) {
    return createWithGuid(Guid::generate(), std::move(name));
}

Entity Scene::createWithGuid(Guid guid, std::string name) {
    HP_PROFILE_ZONE();

    if (const auto existing = byGuid_.find(guid); existing != byGuid_.end()) {
        // Not an assert: this is reachable from a corrupt or hand-edited scene
        // file, and refusing with a message a person can act on beats aborting
        // the editor. The caller gets the entity that already holds the GUID,
        // because returning an invalid handle here just moves the crash.
        HP_LOG_ERROR(kLog, "entity {} already exists; refusing to create a duplicate",
                     guid.toString());
        return Entity{*this, existing->second};
    }

    const auto handle = registry_.create();
    registry_.emplace<Id>(handle, guid);
    registry_.emplace<Tag>(handle, std::move(name));
    registry_.emplace<Transform>(handle);
    registry_.emplace<Hierarchy>(handle);
    registry_.emplace<WorldTransform>(handle);
    // Dirty on creation, so a scene is correct after one propagation rather
    // than after something happens to touch each entity.
    registry_.emplace<DirtyTransform>(handle);
    byGuid_.emplace(guid, handle);
    return Entity{*this, handle};
}

void Scene::detachFromParent(entt::entity child) {
    auto& hierarchy = registry_.get<Hierarchy>(child);
    if (hierarchy.parent == entt::null) {
        return;
    }
    if (auto* parent = registry_.try_get<Hierarchy>(hierarchy.parent)) {
        auto& siblings = parent->children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), child), siblings.end());
    }
    hierarchy.parent = entt::null;
}

void Scene::destroyRecursive(entt::entity entity) {
    // Copy the child list before recursing: destroying a child mutates the
    // parent's vector through `detachFromParent`, and iterating it while it is
    // being erased from is the classic way this function corrupts memory.
    const auto children = registry_.get<Hierarchy>(entity).children;
    for (const auto child : children) {
        if (registry_.valid(child)) {
            destroyRecursive(child);
        }
    }
    byGuid_.erase(registry_.get<Id>(entity).guid);
    registry_.destroy(entity);
}

void Scene::destroy(Entity entity) {
    HP_PROFILE_ZONE();

    if (!valid(entity)) {
        return;
    }
    detachFromParent(entity.raw());
    destroyRecursive(entity.raw());
}

bool Scene::valid(Entity entity) const {
    return entity.scene() == this && entity.raw() != entt::null
           && registry_.valid(entity.raw());
}

std::optional<Entity> Scene::find(Guid guid) const {
    const auto found = byGuid_.find(guid);
    if (found == byGuid_.end()) {
        return std::nullopt;
    }
    // const_cast because a handle is a mutable view and `find` is logically a
    // lookup. The alternative is a ConstEntity type whose only difference is
    // that it cannot call `add`, which doubles the handle API to express
    // something no caller here has asked for.
    return Entity{const_cast<Scene&>(*this), found->second};
}

bool Scene::setParent(Entity child, Entity parent, Reparent mode) {
    if (!valid(child)) {
        return false;
    }

    // Captured before the link changes, because KeepWorld has to solve for a
    // local transform that reproduces it under the *new* parent.
    const float4x4 worldBefore = worldTransform(child);

    if (parent.raw() == entt::null || parent == Entity{}) {
        detachFromParent(child.raw());
        if (mode == Reparent::KeepWorld) {
            applyWorldTransform(child.raw(), worldBefore, float4x4::Identity());
        }
        markTransformDirty(child);
        return true;
    }

    if (!valid(parent) || child == parent) {
        return false;
    }

    // Walk up from the prospective parent: if we meet the child, this would
    // close a cycle. Refused rather than accepted, because every hierarchy
    // traversal in the engine assumes termination and a cycle turns the first
    // one that runs into a hang with no clue where it came from.
    for (auto ancestor = parent.raw(); ancestor != entt::null;
         ancestor = registry_.get<Hierarchy>(ancestor).parent) {
        if (ancestor == child.raw()) {
            HP_LOG_ERROR(kLog, "refusing to parent {} to its own descendant",
                         child.guid().toString());
            return false;
        }
    }

    detachFromParent(child.raw());
    registry_.get<Hierarchy>(child.raw()).parent = parent.raw();
    registry_.get<Hierarchy>(parent.raw()).children.push_back(child.raw());
    if (mode == Reparent::KeepWorld) {
        applyWorldTransform(child.raw(), worldBefore, worldTransform(parent));
    }
    markTransformDirty(child);
    return true;
}

void Scene::setLocalTransform(Entity entity, const Transform& transform) {
    if (!valid(entity)) {
        return;
    }
    registry_.get<Transform>(entity.raw()) = transform;
    markTransformDirty(entity);
}

void Scene::markTransformDirty(Entity entity) {
    if (valid(entity)) {
        // Only the entity itself. Propagation carries dirtiness down the tree as
        // it walks, so marking a whole subtree here would be the same work done
        // twice -- once to mark, once to visit.
        registry_.emplace_or_replace<DirtyTransform>(entity.raw());
    }
}

void Scene::applyWorldTransform(entt::entity entity, const float4x4& world,
                                const float4x4& parentWorld) {
    // Solve local = world * inverse(parentWorld). Inversion rather than
    // decomposing the parent into TRS, because a parent chain carrying
    // non-uniform scale does not decompose cleanly and the inverse is correct
    // regardless.
    const float4x4 local = world * parentWorld.Inverse();
    auto& transform = registry_.get<Transform>(entity);

    transform.position = float3{local._41, local._42, local._43};

    // Scale is the length of each basis row. Extracting it first is what lets
    // the rotation be read off the normalised rows below.
    const float3 row0{local._11, local._12, local._13};
    const float3 row1{local._21, local._22, local._23};
    const float3 row2{local._31, local._32, local._33};
    const float scaleX = length(row0);
    const float scaleY = length(row1);
    const float scaleZ = length(row2);
    transform.scale = float3{scaleX, scaleY, scaleZ};

    const float invX = scaleX != 0.0F ? 1.0F / scaleX : 1.0F;
    const float invY = scaleY != 0.0F ? 1.0F / scaleY : 1.0F;
    const float invZ = scaleZ != 0.0F ? 1.0F / scaleZ : 1.0F;
    const float m00 = row0.x * invX, m01 = row0.y * invX, m02 = row0.z * invX;
    const float m10 = row1.x * invY, m11 = row1.y * invY, m12 = row1.z * invY;
    const float m20 = row2.x * invZ, m21 = row2.y * invZ, m22 = row2.z * invZ;

    // Rotation matrix to quaternion, Shepperd's method: pick the largest of the
    // four possible divisors. Written out rather than pulled from Diligent
    // because Diligent has no such conversion -- `MakeQuaternion` takes four
    // components, not a matrix. The branch is not an optimisation: the naive
    // single-formula version divides by a near-zero number for rotations near
    // 180 degrees and loses most of its precision there.
    Quaternion result;
    const float trace = m00 + m11 + m22;
    if (trace > 0.0F) {
        const float s = 0.5F / std::sqrt(trace + 1.0F);
        result.q = float4{(m12 - m21) * s, (m20 - m02) * s, (m01 - m10) * s, 0.25F / s};
    } else if (m00 > m11 && m00 > m22) {
        const float s = 2.0F * std::sqrt(1.0F + m00 - m11 - m22);
        result.q = float4{0.25F * s, (m10 + m01) / s, (m20 + m02) / s, (m12 - m21) / s};
    } else if (m11 > m22) {
        const float s = 2.0F * std::sqrt(1.0F + m11 - m00 - m22);
        result.q = float4{(m10 + m01) / s, 0.25F * s, (m21 + m12) / s, (m20 - m02) / s};
    } else {
        const float s = 2.0F * std::sqrt(1.0F + m22 - m00 - m11);
        result.q = float4{(m20 + m02) / s, (m21 + m12) / s, 0.25F * s, (m01 - m10) / s};
    }
    transform.rotation = result;
}

std::size_t Scene::propagateSubtree(entt::entity entity, const float4x4& parentWorld,
                                    bool ancestorDirty) {
    const bool dirty = ancestorDirty || registry_.all_of<DirtyTransform>(entity);
    auto& world = registry_.get<WorldTransform>(entity);
    std::size_t recomputed = 0;

    if (dirty) {
        const auto& local = registry_.get<Transform>(entity);
        // Diligent is Direct3D-style: matrices multiply left to right in the
        // order the transforms apply, so scale, then rotate, then translate,
        // then the parent. Reversing this is the classic way a hierarchy comes
        // out mirrored.
        const float4x4 localMatrix = float4x4::Scale(local.scale)
                                     * local.rotation.ToMatrix()
                                     * float4x4::Translation(local.position);
        world.previous = world.current;
        world.current = localMatrix * parentWorld;
        registry_.remove<DirtyTransform>(entity);
        ++recomputed;
    }

    // The child list is copied because a child's propagation cannot resize it,
    // but the parent's WorldTransform reference above would dangle if any pool
    // reallocated; taking the matrix by value avoids depending on that.
    const float4x4 worldHere = world.current;
    const auto children = registry_.get<Hierarchy>(entity).children;
    for (const auto child : children) {
        if (registry_.valid(child)) {
            recomputed += propagateSubtree(child, worldHere, dirty);
        }
    }
    return recomputed;
}

std::size_t Scene::propagateTransforms() {
    HP_PROFILE_ZONE();

    std::size_t recomputed = 0;
    const auto identity = float4x4::Identity();
    for (const auto entity : registry_.view<Hierarchy>()) {
        if (registry_.get<Hierarchy>(entity).parent == entt::null) {
            recomputed += propagateSubtree(entity, identity, false);
        }
    }
    return recomputed;
}

const float4x4& Scene::worldTransform(Entity entity) const {
    static const float4x4 identity = float4x4::Identity();
    if (!valid(entity)) {
        return identity;
    }
    return registry_.get<WorldTransform>(entity.raw()).current;
}

const float4x4& Scene::previousWorldTransform(Entity entity) const {
    static const float4x4 identity = float4x4::Identity();
    if (!valid(entity)) {
        return identity;
    }
    return registry_.get<WorldTransform>(entity.raw()).previous;
}

std::vector<Entity> Scene::roots() {
    std::vector<Entity> found;
    for (const auto entity : registry_.view<Hierarchy>()) {
        if (registry_.get<Hierarchy>(entity).parent == entt::null) {
            found.emplace_back(*this, entity);
        }
    }
    return found;
}

std::size_t Scene::size() const {
    return byGuid_.size();
}

Scene Scene::clone(CloneIds ids) const {
    HP_PROFILE_ZONE();

    Scene copy;

    // Create every entity first, so the remap is complete before any component
    // that refers to another entity is copied.
    std::unordered_map<entt::entity, entt::entity> remap;
    remap.reserve(byGuid_.size());
    for (const auto source : registry_.view<Id>()) {
        remap.emplace(source, copy.registry_.create());
    }

    for (const auto& clone : componentClones()) {
        for (const auto& [source, target] : remap) {
            clone.copy(registry_, source, copy.registry_, target);
        }
    }

    for (const auto& [source, target] : remap) {
        // The hierarchy was copied verbatim and now holds entities belonging to
        // the *source* registry, which are meaningless here. Remap both ends.
        auto& hierarchy = copy.registry_.get<Hierarchy>(target);
        if (hierarchy.parent != entt::null) {
            hierarchy.parent = remap.at(hierarchy.parent);
        }
        for (auto& child : hierarchy.children) {
            child = remap.at(child);
        }

        // Every clone starts dirty: it is one bit per entity and it makes a
        // stale source impossible to inherit silently.
        copy.registry_.emplace_or_replace<DirtyTransform>(target);

        auto& identity = copy.registry_.get<Id>(target);
        if (ids == CloneIds::Regenerate) {
            identity.guid = Guid::generate();
        }
        copy.byGuid_.emplace(identity.guid, target);
    }

    return copy;
}

} // namespace hp
