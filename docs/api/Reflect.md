# `<hp/Reflect.hpp>`

*Generated from `engine/include/hp/Reflect.hpp` — do not edit.*

```cpp
#include <hp/Reflect.hpp>
```

13 public declaration(s), 13 documented.

## `PropertyMeta`

```cpp
struct PropertyMeta
```

 Editor-facing metadata attached to a reflected property.

 One struct rather than a bag of loose properties: entt keys custom data on
 its type, so a single struct is one lookup and cannot half-exist.

## `metaContextHandle`

```cpp
entt::locator<entt::meta_ctx>::node_type metaContextHandle()
```

 The engine's meta context handle.

 Exported so every binary in the process can point its own `entt::locator` at
 the one the engine owns.

 @returns a handle to the engine's context; never empty.

## `adoptMetaContext`

```cpp
void adoptMetaContext()
```

 Point this binary's reflection at the engine's context.

 **Deliberately header-only, and that is load-bearing.** `entt::locator`'s
 storage is a static per binary; an exported function would run inside the
 engine and reset the *engine's* locator, leaving the caller's untouched — the
 exact silent failure this exists to prevent. Being inline, it is compiled
 into the caller and resets the caller's.

 Call once at startup, from the executable and from every gameplay module,
 before any other reflection call. Calling it twice is harmless.

## `TypeBuilder`

```cpp
class TypeBuilder
```

 Registration handle for one type. Obtained from `hp::reflect<T>()`.

 Wraps `entt::meta_factory` rather than exposing it, so the churn-prone half
 of the entt meta API — the half that renamed `prop` to `custom` — is behind
 one door. Query-side consumers still use entt's `meta_type`/`meta_data`
 directly; wrapping those too would be reimplementing entt::meta, which is
 the opposite of the point.

## `TypeBuilder::TypeBuilder<Type>`

```cpp
TypeBuilder<Type>(entt::meta_factory<Type> factory)
```

 Wraps an entt factory. Obtained from `hp::reflect<Type>()` rather than
 constructed directly, so the type is always named before properties are
 attached to it.

 @param factory the underlying entt factory, already given its type name.

## `TypeBuilder::property`

```cpp
TypeBuilder<Type> & property(const char * name)
```

 Register a data member under a stable name.

 The name is the identity: it is hashed to the id that gets serialised and
 compared across the module boundary, so renaming a property is a
 data-format change, not a refactor.

 @param name stable identity for this property, hashed to its id.
 @returns this builder, for chaining.

## `TypeBuilder::meta`

```cpp
TypeBuilder<Type> & meta(const PropertyMeta & info)
```

 Attach editor metadata to the most recently registered property.

 Applies to the preceding `property()` call, mirroring how entt's factory
 scopes custom data — so ordering is significant and metadata written
 before any property has been registered attaches to the type itself.

 @param info ranges, tooltip and visibility flags for the inspector.
 @returns this builder, for chaining.

## `TypeBuilder::readOnlyProperty`

```cpp
TypeBuilder<Type> & readOnlyProperty(const char * name)
```

 Register a read-only property backed by a getter.

 Most engine types will not expose public data members — `hp::Guid` keeps
 its value private behind `value()`, and that is the house style. Such a
 property is still fully readable through reflection, so serialization and
 the inspector can show it; writing goes through whatever API the type
 actually offers rather than being forged past its invariants.

 @param name stable identity for this property, hashed to its id.
 @returns this builder, for chaining.

## `TypeBuilder::value`

```cpp
TypeBuilder<Type> & value(const char * name)
```

 Register a named constant — an enumerator, or a static constant.

 Enum values must be named individually: entt has no way to enumerate an
 enum's enumerators, and neither does C++20. That is a real cost of the
 mechanism and it is the reason 53.6 lists enums separately — a forgotten
 enumerator is a value the inspector cannot display and the serializer
 writes as a bare integer.

 @param name the enumerator's stable name, used in saved data and in the UI.
 @returns this builder, for chaining.

## `TypeBuilder::base`

```cpp
TypeBuilder<Type> & base()
```

 Register a base class, so inherited properties resolve.

 @returns this builder, for chaining.

## `reflect`

```cpp
TypeBuilder<Type> reflect(const char * name)
```

 Begin describing a type.

 @param name the type's stable identity. Hashed to the id used everywhere
        else, so it must not change once data referencing it exists.
 @returns a builder for chaining property registrations.

## `resolveType`

```cpp
entt::meta_type resolveType(const char * name)
```

 Look a reflected type up by the name it was registered under.

 @param name the same string passed to `reflect`.
 @returns the type, or an invalid `meta_type` when nothing is registered —
          test it with `operator bool` rather than assuming.

## `forgetType`

```cpp
void forgetType(const char * name)
```

 Forget every type registered from a gameplay module.

 Mandatory on module unload. `entt::meta` stores raw function pointers and
 unowned `const char*` names that live in the module's image, so a module that
 registers and then unloads leaves the shared context holding pointers into
 nothing — the same shape of bug as T0105.1's destructor registrations.

 @param name the type to forget, as passed to `reflect`.
