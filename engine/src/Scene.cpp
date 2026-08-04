#include <hp/Scene.hpp>

#include <hp/Log.hpp>
#include <hp/Profiling.hpp>

#include <algorithm>
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
    registerComponent<MeshRenderer>("MeshRenderer")
        .property<&MeshRenderer::mesh>("mesh")
        .property<&MeshRenderer::material>("material");
    registerComponent<Camera>("Camera")
        .property<&Camera::verticalFov>("verticalFov")
        .property<&Camera::nearPlane>("nearPlane")
        .property<&Camera::farPlane>("farPlane")
        .property<&Camera::orthographic>("orthographic")
        .property<&Camera::orthographicSize>("orthographicSize");
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

bool Scene::setParent(Entity child, Entity parent) {
    if (!valid(child)) {
        return false;
    }

    if (parent.raw() == entt::null || parent == Entity{}) {
        detachFromParent(child.raw());
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
    return true;
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

        auto& identity = copy.registry_.get<Id>(target);
        if (ids == CloneIds::Regenerate) {
            identity.guid = Guid::generate();
        }
        copy.byGuid_.emplace(identity.guid, target);
    }

    return copy;
}

} // namespace hp
