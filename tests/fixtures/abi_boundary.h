// Shared C declarations for the module-boundary fixtures (T0095).
//
// Deliberately a C interface *for the fixture's own plumbing* -- not because
// the engine boundary is C (D12 says it is C++), but because the test needs to
// resolve these symbols by name with dlsym/GetProcAddress, and a C name is the
// only kind you can spell in a string literal without guessing a mangling.
// The C++ that actually crosses the boundary is the entt registry these
// functions pass around.
#ifndef HP_ABI_BOUNDARY_H
#define HP_ABI_BOUNDARY_H

#include <stdint.h>

#if defined(_WIN32)
#define HP_ABI_EXPORT __declspec(dllexport)
#define HP_ABI_IMPORT __declspec(dllimport)
#else
#define HP_ABI_EXPORT __attribute__((visibility("default")))
#define HP_ABI_IMPORT
#endif

#if defined(HP_ABI_ENGINE_BUILD)
#define HP_ABI_ENGINE_API HP_ABI_EXPORT
#else
#define HP_ABI_ENGINE_API HP_ABI_IMPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

// --- provided by the "engine" shared library ---------------------------------

// Address and value of a single engine-owned global. If the engine is ever
// linked into both the executable and the module, these diverge -- which is the
// failure D12 exists to prevent.
HP_ABI_ENGINE_API void* hp_abi_global_addr(void);
HP_ABI_ENGINE_API int hp_abi_bump(void);

// Identity of an engine-defined component type, as the engine sees it.
HP_ABI_ENGINE_API uint64_t hp_abi_engine_type_hash(void);
HP_ABI_ENGINE_API uint32_t hp_abi_engine_type_index(void);

// An entt::registry, owned by the engine, passed across the boundary as C++.
HP_ABI_ENGINE_API void* hp_abi_make_registry(void);
HP_ABI_ENGINE_API void hp_abi_destroy_registry(void* registry);
HP_ABI_ENGINE_API uint32_t hp_abi_create_entity(void* registry);
HP_ABI_ENGINE_API void hp_abi_emplace_engine_component(void* registry, uint32_t entity, int value);
HP_ABI_ENGINE_API int hp_abi_count_engine_components(void* registry);

// A polymorphic type created by the engine and handed to the module, so the
// module can try dynamic_cast/typeid on it. Whether that works decides a
// convention in T0055: RTTI across the boundary is either supported or banned,
// and the answer must be measured rather than assumed (T0095, 95.4).
HP_ABI_ENGINE_API void* hp_abi_make_derived(void);
HP_ABI_ENGINE_API void hp_abi_destroy_base(void* base);
HP_ABI_ENGINE_API int hp_abi_engine_dynamic_cast_works(void* base);
HP_ABI_ENGINE_API const char* hp_abi_engine_typeid_name(void* base);

// --- provided by the loadable "gameplay module" ------------------------------
// Resolved by name at runtime, never linked. Signatures the test casts to.

typedef void* (*hp_mod_global_addr_fn)(void);
typedef int (*hp_mod_bump_fn)(void);
typedef uint64_t (*hp_mod_engine_type_hash_fn)(void);
typedef uint32_t (*hp_mod_engine_type_index_fn)(void);
typedef int (*hp_mod_count_engine_components_fn)(void* registry);
typedef void (*hp_mod_emplace_module_component_fn)(void* registry, uint32_t entity, float value);
typedef int (*hp_mod_count_module_components_fn)(void* registry);
typedef void (*hp_mod_log_fn)(const char* message);
typedef int (*hp_mod_dynamic_cast_works_fn)(void* base);
typedef const char* (*hp_mod_typeid_name_fn)(void* base);

#ifdef __cplusplus
}
#endif

#endif
