// Scenes to and from `.hpscene` YAML (T0022). See hp/SceneSerialize.hpp.

#include <hp/SceneSerialize.hpp>

#include <hp/Cook.hpp>
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

/// Looks a component type up by its stable name.
///
/// Linear over a table with a handful of entries, called once per component per
/// entity. A map would be faster and is not worth the second structure to keep
/// in step -- the point of `registeredComponents` is that there is one list.
const detail::ComponentOps* findOps(std::string_view typeName) {
    const auto& types = detail::registeredComponents();
    const auto it = std::find_if(types.begin(), types.end(),
                                 [typeName](const detail::ComponentOps& ops) {
                                     return std::string_view{ops.name} == typeName;
                                 });
    return it == types.end() ? nullptr : &*it;
}

/// Whether this build can read and write a component of this name generically.
bool isSerializable(const detail::ComponentOps* ops) {
    return ops != nullptr && ops->serialized && ops->get != nullptr && ops->set != nullptr;
}

/// Default-constructs a component through reflection, ready to be read into.
entt::meta_any construct(const detail::ComponentOps& ops) {
    const entt::meta_type type = resolveType(ops.name);
    return type ? type.construct() : entt::meta_any{};
}

/// Every entity, in the order they were created.
///
/// **Reversed, and that is the whole point.** entt walks a storage's packed
/// array from the back, so iterating `view<Id>` yields *reverse* creation order
/// — which means a load (which creates in file order) followed by a save emits
/// the file backwards, and the save after that puts it back. Measured: a scene
/// of four entities round-tripped to a document whose entity order flipped every
/// time, so **every save-after-load produced a whole-file diff** with nothing
/// changed in it. That defeats the "readable and git-diffable" the schema exists
/// for, and it would have been found by a person wondering why their commit
/// touched every line.
///
/// `view<Id>` rather than the registry's entity storage: every entity carries an
/// `Id` (T0021 adds it on create), so this is a documented set rather than
/// whatever the entity pool happens to expose.
std::vector<entt::entity> entitiesInCreationOrder(const entt::registry& registry) {
    const auto view = registry.view<const Id>();
    std::vector<entt::entity> handles(view.begin(), view.end());
    std::reverse(handles.begin(), handles.end());
    return handles;
}

} // namespace

std::string saveSceneToString(const Scene& scene) {
    HP_PROFILE_ZONE_NAMED("saveScene");

    YamlDocument document;
    YamlNode root = document.root();
    root.set(kVersionKey, static_cast<std::int64_t>(kSceneSchemaVersion));
    YamlNode entities = root.addSequence(kEntitiesKey);

    const entt::registry& registry = scene.registry();
    const auto& types = detail::registeredComponents();

    for (const entt::entity handle : entitiesInCreationOrder(registry)) {
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
        std::vector<std::string_view> written;
        for (const detail::ComponentOps& ops : types) {
            if (!ops.serialized || ops.get == nullptr) {
                continue;
            }
            entt::meta_any value = ops.get(registry, handle);
            if (!value) {
                continue;
            }
            written.emplace_back(ops.name);
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

        // **The blob, put back verbatim** (D23). Written after the known types
        // rather than in the file's original order, because the order of a
        // mapping's keys carries no meaning and reconstructing it would mean
        // storing a position for every component in every entity to preserve
        // something no reader depends on.
        if (const auto* unknown = registry.try_get<const UnknownComponents>(handle)) {
            for (const UnknownComponent& item : unknown->items) {
                // A type that has since been registered *and* attached is
                // already above. Emitting the blob too would produce a
                // duplicate key -- a corrupt document, from the mechanism that
                // exists to stop data being lost.
                if (std::find(written.begin(), written.end(), std::string_view{item.type})
                    != written.end()) {
                    continue;
                }
                if (!components.graft(item.yaml)) {
                    HP_LOG_ERROR(kLog,
                                 "could not write back preserved component '{}' on entity {}; "
                                 "its data is lost from this save",
                                 item.type, id.guid.toString());
                }
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

            // A **registered** type that is deliberately excluded from the file
            // — `Hierarchy`, `Id`, `WorldTransform`, `Tag` — is not unknown, and
            // must not be preserved: writing it back would resurrect the corrupt
            // key the schema exists to keep out. Only a name this build has
            // never heard of is a candidate for the blob.
            if (ops != nullptr && !ops->serialized) {
                HP_LOG_WARN(kLog,
                            "'{}' entity {} carries component '{}', which this schema does not "
                            "store on a component. Ignoring it",
                            name, i, typeName);
                continue;
            }

            if (!isSerializable(ops)) {
                // A type this build does not have: a gameplay module that failed
                // to build, or a rename mid-refactor. **Preserved verbatim**
                // (D23) rather than dropped, so the next save does not destroy
                // data belonging to a type that is merely absent today.
                ++result.unknownComponents;
                const std::string raw = components.at(c).emitSubtree();
                if (raw.empty()) {
                    HP_LOG_ERROR(kLog,
                                 "'{}' entity {} carries unregistered component '{}' and its "
                                 "subtree could not be captured. Its data is dropped",
                                 name, i, typeName);
                    continue;
                }
                // `get_or_emplace`, not `Entity::add` — that one replaces, and
                // an entity with two unknown components would keep only the
                // second.
                scene.registry()
                    .get_or_emplace<UnknownComponents>(entity.handle())
                    .items.push_back(UnknownComponent{std::string{typeName}, raw});
                HP_LOG_WARN(kLog,
                            "'{}' entity {} carries component '{}', which is not registered in "
                            "this build. It is kept as text and written back on save; call "
                            "materialiseUnknownComponents once the type exists",
                            name, i, typeName);
                continue;
            }

            // Default-construct through reflection, read into it, then store.
            // `set` copies, so the temporary's lifetime ends here safely.
            entt::meta_any value = construct(*ops);
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

int materialiseUnknownComponents(Scene& scene) {
    HP_PROFILE_ZONE();

    int materialised = 0;
    entt::registry& registry = scene.registry();

    // Collected first rather than mutated during iteration: attaching a
    // component and removing `UnknownComponents` are both structural changes,
    // and entt does not promise a view survives them.
    std::vector<entt::entity> carriers;
    for (const entt::entity handle : registry.view<const UnknownComponents>()) {
        carriers.push_back(handle);
    }

    for (const entt::entity handle : carriers) {
        auto& store = registry.get<UnknownComponents>(handle);
        std::vector<UnknownComponent> remaining;
        remaining.reserve(store.items.size());

        for (UnknownComponent& item : store.items) {
            const detail::ComponentOps* ops = findOps(item.type);
            if (!isSerializable(ops)) {
                remaining.push_back(std::move(item));
                continue;
            }

            // The blob carries its own key, so parsing it gives a mapping with
            // exactly one entry named after the type — which is what makes a
            // fragment self-describing enough to have been stored alone.
            std::optional<YamlDocument> fragment = YamlDocument::parse(item.yaml, item.type);
            entt::meta_any value = fragment ? construct(*ops) : entt::meta_any{};
            if (!fragment || !value
                || !readProperties(fragment->root()[item.type], value.as_ref())
                || !ops->set(registry, handle, value)) {
                // Kept, not discarded. The type exists but its stored form did
                // not read — a renamed field, a changed shape — and dropping the
                // text now would destroy the only record of what the file said.
                HP_LOG_ERROR(kLog,
                             "component '{}' is registered again but its preserved data would "
                             "not read; keeping it as text",
                             item.type);
                remaining.push_back(std::move(item));
                continue;
            }
            ++materialised;
            HP_LOG_INFO(kLog, "materialised preserved component '{}'", item.type);
        }

        if (remaining.empty()) {
            registry.remove<UnknownComponents>(handle);
        } else {
            store.items = std::move(remaining);
        }
    }
    return materialised;
}

// --- the binary cache ------------------------------------------------------

std::vector<std::byte> cookScene(const Scene& scene, std::uint64_t sourceHash) {
    HP_PROFILE_ZONE_NAMED("cookScene");

    std::vector<std::byte> payload;
    const entt::registry& registry = scene.registry();
    const auto& types = detail::registeredComponents();

    const std::vector<entt::entity> handles = entitiesInCreationOrder(registry);
    writeU32(payload, static_cast<std::uint32_t>(handles.size()));

    for (const entt::entity handle : handles) {
        writeString(payload, registry.get<const Id>(handle).guid.toString());

        const auto* tag = registry.try_get<const Tag>(handle);
        writeString(payload, tag != nullptr ? tag->name : std::string{});

        // The parent as a GUID, for the reason the YAML schema gives: an
        // `entt::entity` is a reused slot index, so persisting one silently
        // addresses a different entity on load.
        std::string parent;
        if (const auto* hierarchy = registry.try_get<const Hierarchy>(handle)) {
            if (hierarchy->parent != entt::null && registry.valid(hierarchy->parent)) {
                if (const auto* parentId = registry.try_get<const Id>(hierarchy->parent)) {
                    parent = parentId->guid.toString();
                }
            }
        }
        writeString(payload, parent);

        // Counted by cooking into a scratch buffer first. The alternative is
        // patching a placeholder count, and a count is not a length: getting it
        // wrong desynchronises the whole stream rather than one record.
        std::vector<std::byte> componentBytes;
        std::uint32_t componentCount = 0;
        for (const detail::ComponentOps& ops : types) {
            if (!ops.serialized || ops.get == nullptr) {
                continue;
            }
            entt::meta_any value = ops.get(registry, handle);
            if (!value) {
                continue;
            }
            std::vector<std::byte> one;
            if (!cookProperties(value, one)) {
                HP_LOG_WARN(kLog, "component '{}' has no reflected properties; not cooked",
                            ops.name);
                continue;
            }
            writeString(componentBytes, ops.name);
            writeU64(componentBytes, static_cast<std::uint64_t>(one.size()));
            componentBytes.insert(componentBytes.end(), one.begin(), one.end());
            ++componentCount;
        }
        writeU32(payload, componentCount);
        payload.insert(payload.end(), componentBytes.begin(), componentBytes.end());

        const auto* unknown = registry.try_get<const UnknownComponents>(handle);
        writeU32(payload, unknown == nullptr
                              ? 0U
                              : static_cast<std::uint32_t>(unknown->items.size()));
        if (unknown != nullptr) {
            // Carried through as text, exactly as the YAML path carries it. The
            // cook is a cache of a document, and a cache that quietly holds less
            // than the document is the stale-data bug `Cook.hpp` is built to
            // make impossible.
            for (const UnknownComponent& item : unknown->items) {
                writeString(payload, item.type);
                writeString(payload, item.yaml);
            }
        }
    }

    return writeCook(payload, sourceHash, static_cast<std::uint32_t>(kSceneSchemaVersion));
}

SceneLoadResult loadSceneFromCooked(Scene& scene, const std::vector<std::byte>& bytes,
                                    std::uint64_t expectedSourceHash, std::string_view name) {
    HP_PROFILE_ZONE_NAMED("loadSceneFromCooked");

    SceneLoadResult result;

    std::vector<std::byte> payload;
    if (const CookStatus status = readCook(bytes, expectedSourceHash,
                                           static_cast<std::uint32_t>(kSceneSchemaVersion),
                                           payload);
        status != CookStatus::Ok) {
        HP_LOG_INFO(kLog, "'{}' cook is unusable ({}); re-cook from the YAML", name,
                    describe(status));
        result.status = SceneLoadStatus::Stale;
        return result;
    }

    Scene loaded;
    std::vector<std::pair<Guid, Guid>> links;   // child -> parent
    std::size_t cursor = 0;

    const auto fail = [&](std::string_view why) {
        HP_LOG_WARN(kLog, "'{}' cook is malformed ({}); re-cook from the YAML", name, why);
        result.status = SceneLoadStatus::Stale;
        result.entities = 0;
        return result;
    };

    std::uint32_t entityCount = 0;
    if (!readU32(payload, cursor, entityCount)) {
        return fail("no entity count");
    }

    for (std::uint32_t e = 0; e < entityCount; ++e) {
        std::string guidText;
        std::string entityName;
        std::string parentText;
        if (!readString(payload, cursor, guidText) || !readString(payload, cursor, entityName)
            || !readString(payload, cursor, parentText)) {
            return fail("truncated entity record");
        }
        const std::optional<Guid> guid = Guid::parse(guidText);
        if (!guid) {
            return fail("unreadable guid");
        }

        Entity entity = loaded.createWithGuid(*guid, entityName);
        if (!entity.valid()) {
            return fail("duplicate guid");
        }
        ++result.entities;
        if (const std::optional<Guid> parent = Guid::parse(parentText)) {
            links.emplace_back(*guid, *parent);
        }

        std::uint32_t componentCount = 0;
        if (!readU32(payload, cursor, componentCount)) {
            return fail("no component count");
        }
        for (std::uint32_t c = 0; c < componentCount; ++c) {
            std::string typeName;
            std::uint64_t length = 0;
            if (!readString(payload, cursor, typeName) || !readU64(payload, cursor, length)
                || length > payload.size() - cursor) {
                return fail("truncated component record");
            }
            const std::size_t next = cursor + static_cast<std::size_t>(length);

            const detail::ComponentOps* ops = findOps(typeName);
            if (!isSerializable(ops)) {
                // **The one asymmetry with the YAML path**, and the reason this
                // reports rather than drops: cooked bytes for a type nobody can
                // name are not something a save could write back, so preserving
                // them is not possible. The YAML beside this cook still has the
                // text, so the honest answer is to send the caller there.
                ++result.unknownComponents;
                HP_LOG_WARN(kLog,
                            "'{}' cook names component '{}', which is not registered in this "
                            "build. A cook cannot preserve what it cannot read",
                            name, typeName);
                cursor = next;
                continue;
            }

            entt::meta_any value = construct(*ops);
            if (!value || !readCookedProperties(payload, cursor, value.as_ref())
                || !ops->set(loaded.registry(), entity.handle(), value)) {
                return fail("component would not read");
            }
            cursor = next;
        }

        std::uint32_t unknownCount = 0;
        if (!readU32(payload, cursor, unknownCount)) {
            return fail("no preserved-unknown count");
        }
        for (std::uint32_t u = 0; u < unknownCount; ++u) {
            UnknownComponent item;
            if (!readString(payload, cursor, item.type)
                || !readString(payload, cursor, item.yaml)) {
                return fail("truncated preserved-unknown record");
            }
            loaded.registry()
                .get_or_emplace<UnknownComponents>(entity.handle())
                .items.push_back(std::move(item));
        }
    }

    if (result.unknownComponents > 0) {
        result.status = SceneLoadStatus::Stale;
        result.entities = 0;
        return result;
    }

    for (const auto& [childGuid, parentGuid] : links) {
        const std::optional<Entity> child = loaded.find(childGuid);
        const std::optional<Entity> parent = loaded.find(parentGuid);
        if (!child || !parent) {
            HP_LOG_WARN(kLog, "'{}' parent {} of {} is not in this cook; leaving it a root",
                        name, parentGuid.toString(), childGuid.toString());
            continue;
        }
        loaded.setParent(*child, *parent, Reparent::KeepLocal);
    }

    loaded.propagateTransforms();

    // Built into a scratch scene and moved in at the end, so a cook that turns
    // out to be malformed halfway through leaves the caller's scene untouched
    // rather than half-replaced.
    scene = std::move(loaded);
    return result;
}

} // namespace hp
