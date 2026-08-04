# `<hp/Scene.hpp>`

*Generated from `engine/include/hp/Scene.hpp` — do not edit.*

```cpp
#include <hp/Scene.hpp>
```

54 public declaration(s), 54 documented.

## `Id`

```cpp
struct Id
```

 Stable identity, added automatically to every entity.

 The GUID is what survives serialization, a play-mode clone and a reload;
 `entt::entity` is a slot index that is reused and means nothing outside the
 registry that issued it. Anything persisted refers to the GUID.

## `Tag`

```cpp
struct Tag
```

 A human-readable label, added automatically to every entity.

 **This is the editor's name for the entity, not player-facing text**, and it
 must not be a string-table key — see T0112's convention in
 `06-engine-conventions.md`. `Door_01` and `PlayerSpawn` are what belongs
 here. A name a player reads is a different, keyed component that the game
 defines.

## `Transform`

```cpp
struct Transform
```

 Position, rotation and scale **relative to the parent**.

 Pure data, and pure *local* data. The world matrix is not stored here and is
 not this ticket's concern (T0101). Keeping the local transform free of any
 derived state is what lets a transform be copied, serialized and diffed
 without needing to know whether a cached world matrix is stale.

## `WorldTransform`

```cpp
struct WorldTransform
```

 The world transform, plus the one from the previous propagation (T0101).

 Derived state, written **only** by `Scene::propagateTransforms`. Assigning to
 it by hand is silently undone on the next pass, which is why there is no API
 that hands it out mutably.

 `previous` exists from day one rather than being retrofitted: physics
 interpolation (T0057's alpha) needs a previous/current pair, and motion
 vectors for TAA and motion blur need one eventually (T0096 leaves the hook
 open). One extra matrix per entity now is cheaper than touching every
 transform consumer later. An entity that did not move has
 `previous == current`, which is the correct answer rather than a special case.

## `DirtyTransform`

```cpp
struct DirtyTransform
```

 Marks an entity whose world transform needs recomputing.

 A tag component rather than a bool inside `Transform`, so propagation can ask
 entt for the dirty set instead of visiting every entity to read a flag — and
 so an unchanged subtree genuinely costs nothing.

## `Hierarchy`

```cpp
struct Hierarchy
```

 The parent link and the child list, maintained **only** by `Scene`.

 Separate from `Transform` rather than folded into it, and the split is the
 whole reason this compiles into something maintainable: a transform is data a
 caller may assign freely, while a hierarchy carries an invariant — a parent's
 `children` must contain exactly the entities whose `parent` is that parent.
 A `parent` field sitting inside `Transform` invites `t.parent = e`, which
 silently breaks that invariant and produces a child that is transformed by a
 parent unaware of it. Use `Scene::setParent`.

 Present on every entity, including roots, because attaching one later would
 invalidate every pointer entt handed out for the existing pool.

## `MeshRenderer`

```cpp
struct MeshRenderer
```

 Draws a mesh with a material. Both are asset GUIDs, never pointers.

## `Entity`

```cpp
class Entity
```

 A cheap, non-owning handle to one entity in one scene.

 Copy it freely; it is a pointer and an index. **Do not store it across a
 frame** — it does not know whether the entity still exists, and a destroyed
 entity's slot is reused, so a stale handle can silently address a different
 entity. Persist a `Guid` and resolve it with `Scene::find` instead.

 `HP_API` because the engine builds with hidden visibility: `valid` and `guid`
 are the two members defined out of line, and without the export they link
 inside the engine and are undefined in every consumer.

## `Entity::Entity`

```cpp
Entity()
```

 Constructs a null handle that belongs to no scene.

## `Entity::Entity`

```cpp
Entity(Scene & scene, entt::entity handle)
```

 Constructs a handle. Prefer `Scene::create`; this exists for the scene
 itself and for code that already holds a raw entt entity.
 @param scene the owning scene. Must outlive the handle.
 @param handle the entt entity, which must come from `scene`'s registry.

## `Entity::valid`

```cpp
bool valid() const
```

 @returns whether this handle refers to a live entity in its scene. False
          for a default-constructed handle and for one whose entity has
          been destroyed.

## `Entity::guid`

```cpp
Guid guid() const
```

 @returns the entity's stable identity, or a default GUID when the handle
          is not valid.

## `Entity::scene`

```cpp
Scene * scene() const
```

 @returns the owning scene, or nullptr for a default-constructed handle.

## `Entity::raw`

```cpp
entt::entity raw() const
```

 @returns the underlying entt entity, for code working with the registry
          directly. `entt::null` when the handle is default-constructed.

## `Entity::add`

```cpp
Component & add(Args &&... args)
```

 Adds a component, constructing it in place, and replaces any existing one.

 Replacing rather than asserting because the alternative — a distinct
 `add` and `replace` — is a coin flip at every call site that ends in a
 runtime error nobody can act on.
 @param args arguments forwarded to the component's constructor.
 @returns a reference to the component, valid until the pool reallocates.

## `Entity::has`

```cpp
bool has() const
```

 @returns whether the entity has this component.

## `Entity::get`

```cpp
Component & get()
```

 @returns a reference to the component. Undefined if it is absent — check
          with `has`, or use `tryGet`.

## `Entity::tryGet`

```cpp
Component * tryGet()
```

 @returns a pointer to the component, or nullptr when absent.

## `Entity::remove`

```cpp
std::size_t remove()
```

 Removes the component if present.
 @returns the number of components removed: 1, or 0 when absent.

## `Entity::operator==`

```cpp
bool operator==(Entity a, Entity b)
```

 @param a the left-hand handle.
 @param b the right-hand handle.
 @returns whether two handles refer to the same entity in the same scene.
          Two null handles compare equal.

## `Entity::operator!=`

```cpp
bool operator!=(Entity a, Entity b)
```

 @param a the left-hand handle.
 @param b the right-hand handle.
 @returns whether two handles differ; see `operator==`.

## `Reparent`

```cpp
enum class Reparent
```

| Enumerator | Value |
|---|---|
| `KeepLocal` | 0 |
| `KeepWorld` | 1 |

 What a reparent does to the entity's position in the world (T0101).

## `CloneIds`

```cpp
enum class CloneIds
```

| Enumerator | Value |
|---|---|
| `Preserve` | 0 |
| `Regenerate` | 1 |

 How a cloned scene treats the identities inside it.

 The two modes are not variations on one operation — they have **opposite**
 semantics, and conflating them is how a prefab instance ends up silently
 sharing state with its template.

## `Scene`

```cpp
class Scene
```

 Owns an `entt::registry` and every entity in it.

 Not copyable, because a copy has to answer the GUID question above and there
 is no correct default answer. Use `clone` and say which you mean.

## `Scene::Scene`

```cpp
Scene()
```

 Constructs an empty scene.

## `Scene::Scene`

```cpp
Scene(const Scene &)
```

 Not copyable — see the class comment. Use `clone`.

## `Scene::operator=`

```cpp
Scene & operator=(const Scene &)
```

 Not copyable; see the copy constructor.
 @returns nothing -- deleted.

## `Scene::Scene`

```cpp
Scene(Scene && other)
```

 Moves the scene. Handles referring to the moved-from scene are invalid.
 @param other the scene to move from.

## `Scene::operator=`

```cpp
Scene & operator=(Scene && other)
```

 Moves the scene, destroying whatever this one held.
 @param other the scene to move from.
 @returns this scene.

## `Scene::create`

```cpp
Entity create(std::string name)
```

 Creates an entity carrying `Id`, `Tag`, `Transform` and `Hierarchy`.

 All four are automatic because every consumer assumes them: the editor
 needs a name and a transform to show, serialization needs an identity,
 and an optional hierarchy would mean a null check at every traversal step.
 @param name the entity's editor label. Not player-facing text (T0112).
 @returns a handle to the new entity.

## `Scene::createWithGuid`

```cpp
Entity createWithGuid(Guid guid, std::string name)
```

 Creates an entity with a specific identity, for deserialization.

 @param guid the identity to give it. Must not already exist in this
        scene; the scene asserts rather than silently producing a
        duplicate that would make `find` ambiguous.
 @param name the entity's editor label.
 @returns a handle to the new entity.

## `Scene::destroy`

```cpp
void destroy(Entity entity)
```

 Destroys an entity **and its descendants**, immediately.

 Descendants go too because the alternative is orphaned entities holding a
 parent link into a reused slot — which does not fail, it silently
 reparents them to whatever is created next.
 @param entity the entity to destroy. A stale or null handle is ignored.

## `Scene::valid`

```cpp
bool valid(Entity entity) const
```

 @returns whether the handle refers to a live entity in this scene.
 @param entity the handle to check.

## `Scene::find`

```cpp
std::optional<Entity> find(Guid guid) const
```

 Looks an entity up by its stable identity.

 Per-scene, never global — `CloneIds::Preserve` puts the same GUIDs in two
 live scenes at once, so a global map would be ambiguous by construction.
 @param guid the identity to resolve.
 @returns the entity, or nullopt when this scene has no such GUID.

## `Scene::setParent`

```cpp
bool setParent(Entity child, Entity parent, Reparent mode)
```

 Reparents an entity, maintaining both sides of the link.

 The only supported way to change a `Hierarchy`. Detaches from the old
 parent first, so calling it repeatedly cannot duplicate a child entry.
 @param child the entity to reparent.
 @param parent the new parent, or a null handle to make `child` a root.
 @param mode whether to keep the local or the world transform. Defaults to
        keeping the local one, which is what code usually means; an editor
        drag wants `KeepWorld`.
 @returns false when the handles are invalid or when `parent` is `child`
          or one of its descendants — a cycle would make traversal
          non-terminating, so it is refused rather than accepted.

## `Scene::setLocalTransform`

```cpp
void setLocalTransform(Entity entity, const Transform & transform)
```

 Writes an entity's local transform and marks its subtree for propagation.

 **The only write that is guaranteed to reach the world transform.**
 `Entity::get<Transform>()` still hands out a mutable reference — entt's
 storage is not going to stop it — and a write through that reference is
 invisible to propagation until something marks the entity dirty. Prefer
 this; if you must write directly, call `markTransformDirty` after.
 @param entity the entity to move.
 @param transform the new local transform.
 @returns nothing.

## `Scene::markTransformDirty`

```cpp
void markTransformDirty(Entity entity)
```

 Marks an entity's world transform stale, so the next propagation
 recomputes it and everything below it.
 @param entity the entity whose local transform changed.
 @returns nothing.

## `Scene::propagateTransforms`

```cpp
std::size_t propagateTransforms()
```

 Recomputes every stale world transform, in one pass, parents before
 children.

 Belongs at frame phases 7 and 9 (D17): phase 7 serves the followers that
 run at phase 8, and phase 9 catches whatever phase 8 itself moved. A
 clean scene walks the hierarchy and writes nothing.
 @returns how many entities were recomputed, which is what a test asserts
          on to prove an unchanged subtree cost nothing.

## `Scene::worldTransform`

```cpp
const float4x4 & worldTransform(Entity entity) const
```

 @returns the entity's world matrix as of the last propagation, or
          identity when the handle is invalid.

 A lookup, never a parent-chain walk — the walk is the propagation pass's
 job, done once for the whole scene rather than once per query.
 @param entity the entity to read.

## `Scene::previousWorldTransform`

```cpp
const float4x4 & previousWorldTransform(Entity entity) const
```

 @returns the entity's world matrix as of the propagation before last, for
          interpolation and motion vectors. Identity for an invalid handle.
 @param entity the entity to read.

## `Scene::roots`

```cpp
int roots()
```

 @returns every root entity, in creation order.

## `Scene::size`

```cpp
std::size_t size() const
```

 @returns how many entities the scene holds.

## `Scene::clone`

```cpp
Scene clone(CloneIds ids) const
```

 Copies the scene.
 @param ids whether to preserve or regenerate identities. There is no
        default: the two have opposite semantics and picking one silently
        is the failure this parameter exists to prevent.
 @returns the new scene.

## `Scene::registry`

```cpp
int & registry()
```

 @returns the underlying registry, for views, groups and anything this
          API does not wrap. Using it is normal, not an escape hatch —
          entt's iteration is the point of using entt.

## `Scene::registry`

```cpp
const int & registry() const
```

 @returns the underlying registry; see the non-const overload.

## `registerComponent`

```cpp
TypeBuilder<Component> registerComponent(const char * name)
```

 Registers a component type for reflection **and** for scene cloning.

 One call, deliberately. T0053 landed reflection with no registry to hook into
 and left a second list to keep in sync; the whole point of this function is
 that there is no second list. A type registered here is inspectable,
 serializable and clonable, and a type that is not registered is silently
 dropped by `Scene::clone` — which is the failure mode to watch for, and the
 reason the engine's own components are registered in one place at startup.

 Identity is the **name**, never `entt::type_index`: T0095 measured that index
 to be a per-module number with no meaning across the module boundary.
 @param name the stable type name, e.g. "Transform". Must be unique and must
        not change once anything has been serialized with it.
 @returns the same builder `reflect` returns, so fields are described in the
          same expression that registers the type — which is what stops the
          field list and the type list from drifting apart.

## `registerComponentClone`

```cpp
void registerComponentClone(const char * name, void (*)(const int &, entt::entity, int &, entt::entity) copy)
```

 Records the clone operation for a component type.
 @param name the stable type name.
 @param copy a function copying the component from one registry to another.
 @returns nothing.

## `registerCoreComponents`

```cpp
void registerCoreComponents()
```

 Registers the engine's own components: `Id`, `Tag`, `Transform`, `Hierarchy`,
 `MeshRenderer` and `Camera`.

 Called by `Scene`'s first construction, so a scene is never missing them.
 Safe to call repeatedly.
 @returns nothing.

## `add`

```cpp
Component & add(Args &&... args)
```

 Adds a component, constructing it in place, and replaces any existing one.

 Replacing rather than asserting because the alternative — a distinct
 `add` and `replace` — is a coin flip at every call site that ends in a
 runtime error nobody can act on.
 @param args arguments forwarded to the component's constructor.
 @returns a reference to the component, valid until the pool reallocates.

## `has`

```cpp
bool has() const
```

 @returns whether the entity has this component.

## `get`

```cpp
Component & get()
```

 @returns a reference to the component. Undefined if it is absent — check
          with `has`, or use `tryGet`.

## `tryGet`

```cpp
Component * tryGet()
```

 @returns a pointer to the component, or nullptr when absent.

## `remove`

```cpp
std::size_t remove()
```

 Removes the component if present.
 @returns the number of components removed: 1, or 0 when absent.

## `registerComponent`

```cpp
TypeBuilder<Component> registerComponent(const char * name)
```

 Registers a component type for reflection **and** for scene cloning.

 One call, deliberately. T0053 landed reflection with no registry to hook into
 and left a second list to keep in sync; the whole point of this function is
 that there is no second list. A type registered here is inspectable,
 serializable and clonable, and a type that is not registered is silently
 dropped by `Scene::clone` — which is the failure mode to watch for, and the
 reason the engine's own components are registered in one place at startup.

 Identity is the **name**, never `entt::type_index`: T0095 measured that index
 to be a per-module number with no meaning across the module boundary.
 @param name the stable type name, e.g. "Transform". Must be unique and must
        not change once anything has been serialized with it.
 @returns the same builder `reflect` returns, so fields are described in the
          same expression that registers the type — which is what stops the
          field list and the type list from drifting apart.
