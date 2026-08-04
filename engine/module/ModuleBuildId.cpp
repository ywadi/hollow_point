// Stamps a gameplay module with the engine build id it was compiled against
// (T0104.3).
//
// Compiled into every module by `hp_add_gameplay_module()`, for the same reason
// the unload finalizer is: a stamp that an author can forget is a stamp that is
// missing exactly when it matters. A module without one is refused rather than
// assumed compatible — see hp::checkModuleBuildId.
//
// The value is baked at *this module's* compile time. That is the entire
// mechanism: rebuild the engine's public headers and the engine's own id moves,
// while a module compiled earlier still carries the old one, and the two no
// longer match. Nothing needs to detect the change; the mismatch is the
// detection.
//
// `extern "C"` and exported, because the loader resolves it by name
// (hp::kModuleBuildIdSymbol) before calling anything else in the module — the
// check has to happen before any of its code runs, so it cannot depend on the
// module having been initialised.

#include <hp/BuildId.h> // generated; see cmake/hp_build_id.cmake

#if defined(_WIN32)
#define HP_MODULE_STAMP_EXPORT __declspec(dllexport)
#else
#define HP_MODULE_STAMP_EXPORT __attribute__((visibility("default")))
#endif

extern "C" HP_MODULE_STAMP_EXPORT const char* hp_module_build_id() {
    return HP_BUILD_ID;
}
