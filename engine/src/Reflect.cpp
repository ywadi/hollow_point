// Reflection context ownership (T0053).
//
// Only the parts that must live in the engine are here: the context itself, and
// lookups that need it. Registration is header-side, because it is templated on
// the type being registered and there is nothing to hide.

#include <hp/Reflect.hpp>

#include <entt/meta/meta.hpp>

namespace hp {

entt::locator<entt::meta_ctx>::node_type metaContextHandle() noexcept {
    // `handle()` returns the locator's current node, constructing the service on
    // first use. Because this function is compiled into the engine shared
    // library, the node it returns is the *engine's* — which is the whole point:
    // every other binary in the process adopts this one rather than minting its
    // own. See hp::adoptMetaContext, which is deliberately not exported.
    entt::locator<entt::meta_ctx>::value_or();
    return entt::locator<entt::meta_ctx>::handle();
}

entt::meta_type resolveType(const char* name) noexcept {
    if (name == nullptr) {
        return {};
    }
    // Resolved by name hash rather than by type index, which is the rule this
    // whole subsystem is built on: type_index is a per-module number with no
    // meaning across the boundary (T0095), while type_hash is stable and is what
    // entt keys its own pools on.
    return entt::resolve(entt::hashed_string{name});
}

void forgetType(const char* name) noexcept {
    if (name == nullptr) {
        return;
    }
    // meta_reset takes the same id the type was registered under. Leaving a
    // module's types registered after it unloads means the shared context holds
    // function pointers and name literals that live in an unmapped image --
    // structurally the same failure T0105.1 fixed for static destructors, and
    // just as invisible until something walks the list.
    entt::meta_reset(entt::hashed_string{name}.value());
}

} // namespace hp
