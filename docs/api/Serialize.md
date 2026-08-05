# `<hp/Serialize.hpp>`

*Generated from `engine/include/hp/Serialize.hpp` — do not edit.*

```cpp
#include <hp/Serialize.hpp>
```

6 public declaration(s), 6 documented.

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

## `cookProperties`

```cpp
bool cookProperties(const entt::meta_any & value, std::vector<std::byte> & out)
```

 Cooks a reflected object into binary payload bytes (T0020.4).

 **Every property is length-prefixed**, and that is the whole compatibility
 story. A reader that meets a property it does not recognise — because the
 type lost that field, or gained others since — skips exactly its bytes and
 carries on, rather than losing sync and misreading everything after it. The
 YAML path gets that for free from the format; binary has to build it in.

 Properties are identified by the **hash of their name**, not by position, so
 reordering a type's registrations does not invalidate cooked data.

 @param value the object, from `entt::forward_as_meta`.
 @param out receives the payload, appended to whatever is already there.
 @returns false when the type has no registered properties, or a property has
          a type this layer cannot write.

## `readCookedProperties`

```cpp
bool readCookedProperties(const std::vector<std::byte> & bytes, std::size_t & cursor, entt::meta_any value)
```

 Reads a cooked payload back into a reflected object.

 Same leniency as the YAML path: an unknown property is skipped, and a
 property the payload does not mention keeps its current value.
 @param bytes the payload, as produced by `cookProperties`.
 @param cursor the read position, advanced past the object.
 @param value a reference to the target, from `entt::forward_as_meta`.
 @returns false when the payload is truncated or structurally invalid — which
          means "re-cook", exactly as every other cook failure does.
