// Reflected values to and from YAML (T0020.3).
//
// **Serialization is derived from reflection, not written per type.** T0053
// exists so that four systems — this one, the inspector, undo/redo, and anything
// crossing a boundary — enumerate a type once instead of each growing a switch
// that drifts. A component that registers its properties is serializable by
// that fact alone; there is no second mechanism to keep in step, which is the
// "four switches" failure T0053 was created to prevent.
//
// **Hand-written handling exists only for the leaf types reflection bottoms out
// in** — the numeric types, `bool`, `std::string`, `hp::Guid`, and the maths
// types. That is 20.3 read the way the architecture review settled it: define
// the leaf layer reflection sits on, not a parallel per-component mechanism.
//
// ---
//
// **Reading is lenient and writing is exact.** A field absent from the document
// keeps whatever the target already holds, so a component gains a property and
// old files still load with the new field at its default. A field present but
// malformed is reported. That asymmetry is deliberate: files outlive the code
// that wrote them, and refusing to load a scene because one field is missing is
// a worse outcome than loading it with a default.
//
// **Leaf shapes**, chosen for a person reading a diff:
//
// ```yaml
// position: [1, 2, 3]          # float3, float4, Quaternion
// rotation: [0, 0, 0, 1]
// guid: 0123456789abcdef       # as its canonical string
// name: Player                 # std::string
// ```
#pragma once

#include <hp/Api.hpp>
#include <hp/Reflect.hpp>
#include <hp/Yaml.hpp>

#include <entt/meta/meta.hpp>

#include <string_view>

namespace hp {

/// Writes a reflected value into a mapping, under `key`.
///
/// Dispatches on the value's type: a leaf becomes a scalar or a short sequence,
/// a sequence container becomes a YAML sequence, and anything else with
/// registered properties becomes a nested map, recursively.
///
/// @param parent the mapping to write into.
/// @param key the key to write under.
/// @param value the value, usually from `entt::forward_as_meta` or a
///        `meta_data::get`.
/// @returns false when the value's type is neither a known leaf, a sequence,
///          nor a type with registered properties — in which case **nothing is
///          written**, rather than a key with an empty or misleading value.
///          The type is named in the log so the missing registration is
///          actionable.
[[nodiscard]] HP_API bool writeReflected(YamlNode parent, std::string_view key,
                                         const entt::meta_any& value);

/// Reads a YAML node into an existing reflected value.
///
/// @param node the node to read from. An invalid node is a no-op that returns
///        true — an absent field leaves the target at whatever it already held,
///        which is what lets a component gain a property without invalidating
///        every file written before it.
/// @param value a reference to the target, from `entt::forward_as_meta`. Passed
///        by value because `meta_any` *is* the handle; it must refer to the real
///        object rather than a copy, or the read silently updates a temporary.
/// @returns false when the node is present but cannot be read into this type.
[[nodiscard]] HP_API bool readReflected(YamlNode node, entt::meta_any value);

/// Writes every registered property of a reflected type into a mapping.
///
/// @param parent the mapping to write the properties into.
/// @param value the object, from `entt::forward_as_meta`.
/// @returns false when the type has no registered properties, which almost
///          always means it was never passed to `hp::reflect`.
[[nodiscard]] HP_API bool writeProperties(YamlNode parent, const entt::meta_any& value);

/// Reads a mapping into every registered property it names.
///
/// **Properties absent from the document are left alone**, and properties in the
/// document that the type does not have are ignored — the first is forward
/// compatibility, the second is backward compatibility, and a save format needs
/// both.
/// @param node the mapping to read.
/// @param value a reference to the target, from `entt::forward_as_meta`.
/// @returns false when the type has no registered properties.
[[nodiscard]] HP_API bool readProperties(YamlNode node, entt::meta_any value);

} // namespace hp
