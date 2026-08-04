# `<hp/Serialize.hpp>`

*Generated from `engine/include/hp/Serialize.hpp` — do not edit.*

```cpp
#include <hp/Serialize.hpp>
```

4 public declaration(s), 4 documented.

## `writeReflected`

```cpp
bool writeReflected(YamlNode parent, std::string_view key, const entt::meta_any & value)
```

 Writes a reflected value into a mapping, under `key`.

 Dispatches on the value's type: a leaf becomes a scalar or a short sequence,
 a sequence container becomes a YAML sequence, and anything else with
 registered properties becomes a nested map, recursively.

 @param parent the mapping to write into.
 @param key the key to write under.
 @param value the value, usually from `entt::forward_as_meta` or a
        `meta_data::get`.
 @returns false when the value's type is neither a known leaf, a sequence,
          nor a type with registered properties — in which case **nothing is
          written**, rather than a key with an empty or misleading value.
          The type is named in the log so the missing registration is
          actionable.

## `readReflected`

```cpp
bool readReflected(YamlNode node, entt::meta_any value)
```

 Reads a YAML node into an existing reflected value.

 @param node the node to read from. An invalid node is a no-op that returns
        true — an absent field leaves the target at whatever it already held,
        which is what lets a component gain a property without invalidating
        every file written before it.
 @param value a reference to the target, from `entt::forward_as_meta`. Passed
        by value because `meta_any` *is* the handle; it must refer to the real
        object rather than a copy, or the read silently updates a temporary.
 @returns false when the node is present but cannot be read into this type.

## `writeProperties`

```cpp
bool writeProperties(YamlNode parent, const entt::meta_any & value)
```

 Writes every registered property of a reflected type into a mapping.

 @param parent the mapping to write the properties into.
 @param value the object, from `entt::forward_as_meta`.
 @returns false when the type has no registered properties, which almost
          always means it was never passed to `hp::reflect`.

## `readProperties`

```cpp
bool readProperties(YamlNode node, entt::meta_any value)
```

 Reads a mapping into every registered property it names.

 **Properties absent from the document are left alone**, and properties in the
 document that the type does not have are ignored — the first is forward
 compatibility, the second is backward compatibility, and a save format needs
 both.
 @param node the mapping to read.
 @param value a reference to the target, from `entt::forward_as_meta`.
 @returns false when the type has no registered properties.
