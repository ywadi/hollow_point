// The "engine" half of the module-boundary fixture (T0095, D12).
//
// Built as a *shared* library. The executable and the loadable module both link
// it, so engine state exists exactly once in the process -- that is the property
// under test, not an implementation detail.

#define HP_ABI_ENGINE_BUILD
#include "abi_boundary.h"

#include <entt/entt.hpp>

namespace {
// The kind of engine global D12 is about: a logger, an autoload registry, an
// asset pool. Duplicate it and each side silently mutates its own copy.
int g_counter = 0;

struct EngineComponent {
    int value;
};
} // namespace

extern "C" {

void* hp_abi_global_addr(void) { return &g_counter; }
int   hp_abi_bump(void)        { return ++g_counter; }

uint64_t hp_abi_engine_type_hash(void) {
    return static_cast<uint64_t>(entt::type_hash<EngineComponent>::value());
}
uint32_t hp_abi_engine_type_index(void) {
    return static_cast<uint32_t>(entt::type_index<EngineComponent>::value());
}

void* hp_abi_make_registry(void) { return new entt::registry(); }
void  hp_abi_destroy_registry(void* r) { delete static_cast<entt::registry*>(r); }

uint32_t hp_abi_create_entity(void* r) {
    return static_cast<uint32_t>(static_cast<entt::registry*>(r)->create());
}

void hp_abi_emplace_engine_component(void* r, uint32_t e, int value) {
    static_cast<entt::registry*>(r)->emplace<EngineComponent>(
        static_cast<entt::entity>(e), EngineComponent{value});
}

int hp_abi_count_engine_components(void* r) {
    return static_cast<int>(static_cast<entt::registry*>(r)->view<EngineComponent>().size());
}
}
