// Runtime reflection: one description of a type, consumed by everything (T0053).
//
// Four systems need to enumerate and manipulate a type's properties generically
// — serialization (T0022), the inspector (T0035), undo/redo (T0065), and
// anything that later crosses a boundary. Without one shared layer each grows
// its own switch over component types, and adding a component means editing four
// places that drift apart immediately.
//
// **This is a facade over `entt::meta`, not an implementation of reflection.**
// EnTT is already vendored and already the ECS, its meta subsystem is keyed on
// exactly the identity model this engine requires, and writing a second one
// would be the wheel-reinvention the conventions forbid. What lives here is the
// thin layer that makes an EnTT upgrade a change to one file rather than to
// every type in the engine — entt renamed meta `prop()` to `custom()` during
// the 3.14 cycle, and this will be a permanent part of how every engine type is
// declared.
//
// ## Identity is a name hash, never a type index
//
// T0095 measured that `entt::type_index` is a **per-module** sequential number:
// the same type observed from the engine and from a gameplay module gets
// different values, because the memoising static is per-module. It is a runtime
// number with no meaning outside the binary that produced it, and it must never
// be serialised or compared across the boundary.
//
// `entt::meta` keys on `type_hash` — derived from the name — which is stable
// across the boundary and identical on both targets. Everything here follows
// from that: a reflected type's identity is the name you give it.
//
// ## Every participant must adopt the engine's context
//
// This is the sharp edge, and it fails silently. `entt::locator` holds a static
// per binary, and under `-fvisibility=hidden` an executable and a module each
// get their own — so a host that never adopts the engine's context sees an
// empty one and reports every engine type as unresolvable, with no error and no
// crash. Call `hp::adoptMetaContext()` once at startup, from the executable and
// from every gameplay module, before touching anything else here.
#pragma once

#include <hp/Api.hpp>

#include <entt/locator/locator.hpp>
// Teaches entt::meta about the standard containers, so a std::vector member is
// reachable as a sequence and a std::map as an associative container without
// every type having to say so. Included here rather than left to callers
// because 53.6 requires containers to work by default -- a reflection layer
// that handles only scalars pushes a special case into serialization and into
// the inspector, which is the outcome this ticket exists to prevent.
#include <entt/meta/container.hpp>
#include <entt/meta/context.hpp>
#include <entt/meta/factory.hpp>
#include <entt/meta/resolve.hpp>

#include <cstdint>

namespace hp {

/// Editor-facing metadata attached to a reflected property.
///
/// One struct rather than a bag of loose properties: entt keys custom data on
/// its type, so a single struct is one lookup and cannot half-exist.
struct PropertyMeta {
    /// Inclusive range for numeric editors. Equal values mean "unbounded".
    double min = 0.0;
    double max = 0.0;
    /// Shown on hover in the inspector. Never null; empty means none.
    const char* tooltip = "";
    /// Present in serialization but not offered for editing.
    bool read_only = false;
    /// Neither shown nor edited. Still serialised — hiding is a UI decision.
    bool hidden = false;
};

/// The engine's meta context handle.
///
/// Exported so every binary in the process can point its own `entt::locator` at
/// the one the engine owns.
///
/// @returns a handle to the engine's context; never empty.
[[nodiscard]] HP_API entt::locator<entt::meta_ctx>::node_type metaContextHandle() noexcept;

/// Point this binary's reflection at the engine's context.
///
/// **Deliberately header-only, and that is load-bearing.** `entt::locator`'s
/// storage is a static per binary; an exported function would run inside the
/// engine and reset the *engine's* locator, leaving the caller's untouched — the
/// exact silent failure this exists to prevent. Being inline, it is compiled
/// into the caller and resets the caller's.
///
/// Call once at startup, from the executable and from every gameplay module,
/// before any other reflection call. Calling it twice is harmless.
inline void adoptMetaContext() noexcept {
    entt::locator<entt::meta_ctx>::reset(metaContextHandle());
}

/// Registration handle for one type. Obtained from `hp::reflect<T>()`.
///
/// Wraps `entt::meta_factory` rather than exposing it, so the churn-prone half
/// of the entt meta API — the half that renamed `prop` to `custom` — is behind
/// one door. Query-side consumers still use entt's `meta_type`/`meta_data`
/// directly; wrapping those too would be reimplementing entt::meta, which is
/// the opposite of the point.
template <typename Type>
class TypeBuilder {
public:
    /// Wraps an entt factory. Obtained from `hp::reflect<Type>()` rather than
    /// constructed directly, so the type is always named before properties are
    /// attached to it.
    ///
    /// @param factory the underlying entt factory, already given its type name.
    explicit TypeBuilder(entt::meta_factory<Type> factory) noexcept
        : factory_(factory) {}

    /// Register a data member under a stable name.
    ///
    /// The name is the identity: it is hashed to the id that gets serialised and
    /// compared across the module boundary, so renaming a property is a
    /// data-format change, not a refactor.
    ///
    /// @param name stable identity for this property, hashed to its id.
    /// @returns this builder, for chaining.
    template <auto Member>
    TypeBuilder& property(const char* name) {
        factory_ = factory_.template data<Member>(entt::hashed_string{name});
        return *this;
    }

    /// Attach editor metadata to the most recently registered property.
    ///
    /// Applies to the preceding `property()` call, mirroring how entt's factory
    /// scopes custom data — so ordering is significant and metadata written
    /// before any property has been registered attaches to the type itself.
    ///
    /// @param info ranges, tooltip and visibility flags for the inspector.
    /// @returns this builder, for chaining.
    TypeBuilder& meta(const PropertyMeta& info) {
        factory_ = factory_.template custom<PropertyMeta>(info);
        return *this;
    }

    /// Register a read-only property backed by a getter.
    ///
    /// Most engine types will not expose public data members — `hp::Guid` keeps
    /// its value private behind `value()`, and that is the house style. Such a
    /// property is still fully readable through reflection, so serialization and
    /// the inspector can show it; writing goes through whatever API the type
    /// actually offers rather than being forged past its invariants.
    ///
    /// @param name stable identity for this property, hashed to its id.
    /// @returns this builder, for chaining.
    template <auto Getter>
    TypeBuilder& readOnlyProperty(const char* name) {
        factory_ = factory_.template data<nullptr, Getter>(entt::hashed_string{name});
        return *this;
    }

    /// Register a named constant — an enumerator, or a static constant.
    ///
    /// Enum values must be named individually: entt has no way to enumerate an
    /// enum's enumerators, and neither does C++20. That is a real cost of the
    /// mechanism and it is the reason 53.6 lists enums separately — a forgotten
    /// enumerator is a value the inspector cannot display and the serializer
    /// writes as a bare integer.
    ///
    /// @param name the enumerator's stable name, used in saved data and in the UI.
    /// @returns this builder, for chaining.
    template <auto Value>
    TypeBuilder& value(const char* name) {
        factory_ = factory_.template data<Value>(entt::hashed_string{name});
        return *this;
    }

    /// Register a base class, so inherited properties resolve.
    ///
    /// @returns this builder, for chaining.
    template <typename Base>
    TypeBuilder& base() {
        factory_ = factory_.template base<Base>();
        return *this;
    }

private:
    entt::meta_factory<Type> factory_;
};

/// Begin describing a type.
///
/// @param name the type's stable identity. Hashed to the id used everywhere
///        else, so it must not change once data referencing it exists.
/// @returns a builder for chaining property registrations.
template <typename Type>
[[nodiscard]] TypeBuilder<Type> reflect(const char* name) {
    return TypeBuilder<Type>{entt::meta_factory<Type>{}.type(entt::hashed_string{name})};
}

/// Look a reflected type up by the name it was registered under.
///
/// @param name the same string passed to `reflect`.
/// @returns the type, or an invalid `meta_type` when nothing is registered —
///          test it with `operator bool` rather than assuming.
[[nodiscard]] HP_API entt::meta_type resolveType(const char* name) noexcept;

/// Forget every type registered from a gameplay module.
///
/// Mandatory on module unload. `entt::meta` stores raw function pointers and
/// unowned `const char*` names that live in the module's image, so a module that
/// registers and then unloads leaves the shared context holding pointers into
/// nothing — the same shape of bug as T0105.1's destructor registrations.
///
/// @param name the type to forget, as passed to `reflect`.
HP_API void forgetType(const char* name) noexcept;

} // namespace hp
