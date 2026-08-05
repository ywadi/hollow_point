// Scenes to and from `.hpscene` YAML (T0022). See hp/SceneSerialize.hpp.

#include <hp/SceneSerialize.hpp>

#include <hp/Log.hpp>
#include <hp/Profiling.hpp>
#include <hp/Scene.hpp>
#include <hp/Reflect.hpp>
#include <hp/Serialize.hpp>
#include <hp/Yaml.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace hp {
namespace {

const LogCategory kLog("scene.serialize");

constexpr std::string_view kVersionKey = "version";
constexpr std::string_view kEntitiesKey = "entities";
constexpr std::string_view kGuidKey = "guid";
constexpr std::string_view kNameKey = "name";
constexpr std::string_view kParentKey = "parent";
constexpr std::string_view kComponentsKey = "components";

} // namespace

std::string saveSceneToString(const Scene& scene) {
    HP_PROFILE_ZONE_NAMED("saveScene");

    YamlDocument document;
    YamlNode root = document.root();
    root.set(kVersionKey, static_cast<std::int64_t>(kSceneSchemaVersion));
    YamlNode entities = root.addSequence(kEntitiesKey);

    const entt::registry& registry = scene.registry();
    const auto& types = detail::registeredComponents();

    // `view<Id>` rather than `storage<entity>`: every entity carries an `Id`
    // (T0021 adds it on create), and iterating the component gives a stable,
    // documented set rather than whatever the registry's entity storage happens
    // to expose.
    for (const entt::entity handle : registry.view<const Id>()) {
        YamlNode node = entities.appendMap();
        const Id& id = registry.get<const Id>(handle);
        node.set(kGuidKey, id.guid.toString());

        if (const auto* tag = registry.try_get<const Tag>(handle)) {
            node.set(kNameKey, tag->name);
        }

        // The hierarchy link, written as the parent's **GUID**. The runtime
        // representation is an `entt::entity`, which is a registry slot index
        // and is reused after a destroy — persisting one means silently
        // addressing a different entity on load.
        if (const auto* hierarchy = registry.try_get<const Hierarchy>(handle)) {
            if (hierarchy->parent != entt::null && registry.valid(hierarchy->parent)) {
                if (const auto* parentId = registry.try_get<const Id>(hierarchy->parent)) {
                    node.set(kParentKey, parentId->guid.toString());
                }
            }
        }

        YamlNode components = node.addMap(kComponentsKey);
        for (const detail::ComponentOps& ops : types) {
            if (!ops.serialized || ops.get == nullptr) {
                continue;
            }
            entt::meta_any value = ops.get(registry, handle);
            if (!value) {
                continue;
            }
            YamlNode slot = components.addMap(ops.name);
            if (!writeProperties(slot, value)) {
                // A registered type with no reflected properties. Harmless in
                // the file, but it means someone added a component and never
                // described its fields — so it is said out loud rather than
                // producing an empty map nobody notices.
                HP_LOG_WARN(kLog,
                            "component '{}' has no reflected properties; it will round-trip "
                            "as nothing. Add .property<> lines to its registerComponent call",
                            ops.name);
            }
        }
    }

    return document.emit();
}

SceneLoadResult loadSceneFromString(Scene& scene, std::string_view text,
                                    std::string_view name) {
    HP_PROFILE_ZONE_NAMED("loadScene");

    SceneLoadResult result;

    std::optional<YamlDocument> document = YamlDocument::parse(text, name);
    if (!document) {
        HP_LOG_ERROR(kLog, "'{}' is not valid YAML", name);
        result.status = SceneLoadStatus::Malformed;
        return result;
    }

    YamlNode root = document->root();
    if (!root.isMap()) {
        HP_LOG_ERROR(kLog, "'{}' is not a mapping at the root", name);
        result.status = SceneLoadStatus::Malformed;
        return result;
    }

    // **Refusing a newer file matters as much as reading an older one.** Loading
    // the fields this build understands and dropping the rest would write the
    // loss back on the next save, which destroys work in a way nobody sees until
    // they open the project in the newer build again.
    const auto version = static_cast<int>(root[kVersionKey].read(std::int64_t{0}));
    if (version > kSceneSchemaVersion) {
        HP_LOG_ERROR(kLog,
                     "'{}' is schema version {}, and this build understands {}. Refusing to "
                     "load rather than discarding what it does not recognise",
                     name, version, kSceneSchemaVersion);
        result.status = SceneLoadStatus::NewerSchema;
        return result;
    }

    scene = Scene{};

    YamlNode entities = root[kEntitiesKey];
    if (!entities.isSequence()) {
        // An empty scene is legal and is not an error: a new project has one.
        return result;
    }

    const auto& types = detail::registeredComponents();
    const auto findOps = [&types](std::string_view typeName) -> const detail::ComponentOps* {
        const auto it = std::find_if(types.begin(), types.end(),
                                     [typeName](const detail::ComponentOps& ops) {
                                         return std::string_view{ops.name} == typeName;
                                     });
        return it == types.end() ? nullptr : &*it;
    };

    // **Two passes, and the second is what makes forward references work.** A
    // parent may appear after its child in the file, so every entity must exist
    // before any link is resolved.
    std::vector<std::pair<Guid, Guid>> links;   // child -> parent
    links.reserve(entities.size());

    for (std::size_t i = 0; i < entities.size(); ++i) {
        YamlNode node = entities.at(i);
        if (!node.isMap()) {
            continue;
        }

        const std::string guidText = node[kGuidKey].read(std::string{});
        const std::optional<Guid> guid = Guid::parse(guidText);
        if (!guid) {
            HP_LOG_WARN(kLog, "'{}' entity {} has no usable guid ('{}'); skipping it", name, i,
                        guidText);
            continue;
        }

        Entity entity = scene.createWithGuid(*guid, node[kNameKey].read(std::string{"Entity"}));
        if (!entity.valid()) {
            HP_LOG_WARN(kLog, "'{}' entity {} could not be created; a duplicate guid?", name, i);
            continue;
        }
        ++result.entities;

        if (const std::string parentText = node[kParentKey].read(std::string{});
            !parentText.empty()) {
            if (const std::optional<Guid> parent = Guid::parse(parentText)) {
                links.emplace_back(*guid, *parent);
            }
        }

        YamlNode components = node[kComponentsKey];
        if (!components.isMap()) {
            continue;
        }
        for (std::size_t c = 0; c < components.size(); ++c) {
            const std::string_view typeName = components.keyAt(c);
            const detail::ComponentOps* ops = findOps(typeName);
            if (ops == nullptr || !ops->serialized || ops->set == nullptr ||
                ops->get == nullptr) {
                // A type this build does not have: a gameplay module that failed
                // to build, or a rename mid-refactor. Counted and named rather
                // than passed over in silence — and **not yet preserved**, which
                // is T0022's remaining gap, recorded on the ticket.
                ++result.unknownComponents;
                HP_LOG_WARN(kLog,
                            "'{}' entity {} carries component '{}', which is not registered "
                            "in this build. Its data is dropped and a save will not write it "
                            "back",
                            name, i, typeName);
                continue;
            }

            // Default-construct through reflection, read into it, then store.
            // `set` copies, so the temporary's lifetime ends here safely.
            entt::meta_type type = resolveType(ops->name);
            entt::meta_any value = type ? type.construct() : entt::meta_any{};
            if (!value) {
                HP_LOG_WARN(kLog, "component '{}' is registered but not default-constructible",
                            typeName);
                continue;
            }
            if (!readProperties(components.at(c), value.as_ref())) {
                HP_LOG_WARN(kLog, "'{}' entity {} component '{}' would not read", name, i,
                            typeName);
                continue;
            }
            if (!ops->set(scene.registry(), entity.handle(), value)) {
                HP_LOG_WARN(kLog, "'{}' entity {} component '{}' would not attach", name, i,
                            typeName);
            }
        }
    }

    for (const auto& [childGuid, parentGuid] : links) {
        const std::optional<Entity> child = scene.find(childGuid);
        const std::optional<Entity> parent = scene.find(parentGuid);
        if (!child || !parent) {
            HP_LOG_WARN(kLog, "'{}' parent {} of {} is not in this scene; leaving it a root",
                        name, parentGuid.toString(), childGuid.toString());
            continue;
        }
        // `KeepLocal`: the file stores a *local* transform, so reparenting must
        // not recompute it to preserve a world position the file never claimed.
        scene.setParent(*child, *parent, Reparent::KeepLocal);
    }

    scene.propagateTransforms();
    return result;
}

} // namespace hp
