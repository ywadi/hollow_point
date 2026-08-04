// The "gameplay module" half of the fixture (T0095, D12).
//
// Built as a shared library, links the engine shared library, and is loaded at
// runtime by the test rather than linked to it. It declares EngineComponent
// independently -- same layout, separate declaration -- which is exactly what a
// real gameplay module does when it includes an engine header: it gets its own
// template instantiations, and cross-boundary identity has to survive that.

#include "abi_boundary.h"
#include <stdexcept>

#include "abi_polymorphic.h"

#include <hp/Log.hpp>

#include <entt/entt.hpp>
#include <typeinfo>

namespace {
struct EngineComponent {
    int value;
};

struct ModuleComponent {
    float value;
};
} // namespace

extern "C" {

HP_ABI_EXPORT void* hp_mod_global_addr(void) {
    return hp_abi_global_addr();
}

HP_ABI_EXPORT int hp_mod_bump(void) {
    return hp_abi_bump();
}

HP_ABI_EXPORT uint64_t hp_mod_engine_type_hash(void) {
    return static_cast<uint64_t>(entt::type_hash<EngineComponent>::value());
}

HP_ABI_EXPORT uint32_t hp_mod_engine_type_index(void) {
    return static_cast<uint32_t>(entt::type_index<EngineComponent>::value());
}

// The behaviour that matters: read, through the module's own instantiation, a
// component the engine wrote.
HP_ABI_EXPORT int hp_mod_count_engine_components(void* r) {
    return static_cast<int>(static_cast<entt::registry*>(r)->view<EngineComponent>().size());
}

// And write a module-defined type into an engine-owned registry.
HP_ABI_EXPORT void hp_mod_emplace_module_component(void* r, uint32_t e, float value) {
    static_cast<entt::registry*>(r)->emplace<ModuleComponent>(static_cast<entt::entity>(e),
                                                              ModuleComponent{value});
}

// Can gameplay code write to the engine's log -- and therefore to the editor
// console (T0066), the log file and every other sink -- from the far side of
// the module boundary? The category is declared *here*, in the module, which is
// the case that would break if LogCategory owned its own storage (T0054).
HP_ABI_EXPORT void hp_mod_log(const char* message) {
    const hp::LogCategory category("game.sandbox");
    category.setLevel(hp::LogLevel::Trace);
    HP_LOG_INFO(category, "{}", message);
}

// The measurement: a Derived created by the engine, cast by the module.
HP_ABI_EXPORT int hp_mod_dynamic_cast_works(void* b) {
    return dynamic_cast<hp_abi::Derived*>(static_cast<hp_abi::Base*>(b)) != nullptr ? 1 : 0;
}

HP_ABI_EXPORT const char* hp_mod_typeid_name(void* b) {
    return typeid(*static_cast<hp_abi::Base*>(b)).name();
}

HP_ABI_EXPORT int hp_mod_count_module_components(void* r) {
    return static_cast<int>(static_cast<entt::registry*>(r)->view<ModuleComponent>().size());
}

// --- exceptions across the boundary (T0127) ----------------------------------
//
// Three throws whose only difference is where the thrown type's typeinfo lives.
// The suite catches each in the host and records what the handler actually saw,
// because the answers differ by target and the difference is silent.

/// A std:: type. Its typeinfo is emitted locally into every artifact by the
/// statically linked, hidden libc++ -- so the host's copy and this one are
/// different objects.
HP_ABI_EXPORT void hp_mod_throw_std() {
    throw std::runtime_error("thrown inside the gameplay module");
}

/// An engine-owned type: default visibility, key function in the engine
/// library. One typeinfo object, referenced by both sides.
HP_ABI_EXPORT void hp_mod_throw_engine_owned() {
    throw HpAbiEngineError(127);
}

/// The same engine-owned type, thrown through a plain `throw;` rethrow, to show
/// the property belongs to the type rather than to the throw site.
HP_ABI_EXPORT void hp_mod_throw_engine_owned_rethrown() {
    try {
        throw HpAbiEngineError(128);
    } catch (...) {
        throw;
    }
}
}
