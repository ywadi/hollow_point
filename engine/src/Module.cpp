// Build id and module compatibility (T0104).

#include <hp/Module.hpp>

#include <hp/BuildId.h> // generated; see cmake/hp_build_id.cmake

#include <cstdio>
#include <cstring>

namespace hp {

const char* engineBuildId() noexcept {
    return HP_BUILD_ID;
}

ModuleCompatibility checkModuleBuildId(const char* module_id) noexcept {
    ModuleCompatibility result;
    result.engine_id = HP_BUILD_ID;
    // A module with no stamp is not "probably fine" -- it is a module built by
    // something that did not go through hp_add_gameplay_module(), which is
    // exactly the case with no evidence either way. Refuse it.
    result.module_id = (module_id != nullptr) ? module_id : "<unstamped>";
    result.compatible = (module_id != nullptr) && std::strcmp(module_id, HP_BUILD_ID) == 0;
    return result;
}

const char* describeIncompatibility(const char* module_name,
                                    ModuleCompatibility result) noexcept {
    // Thread-local so two loader threads cannot scribble over each other's
    // message. Fixed size: this runs on a failure path where allocating would be
    // one more thing that can go wrong.
    static thread_local char buffer[512];

    if (result.compatible) {
        std::snprintf(buffer, sizeof buffer, "%s: compatible (build id %s)",
                      module_name ? module_name : "<module>", result.engine_id);
        return buffer;
    }

    // Says what is wrong, what each side is, and what to do -- in that order,
    // because the reader is someone whose game just refused to start.
    std::snprintf(buffer, sizeof buffer,
                  "%s was built against a different engine and will not be loaded.\n"
                  "  engine build id : %s\n"
                  "  module build id : %s\n"
                  "  target/config   : %s, %s, profiling=%s\n"
                  "Rebuild the gameplay module against this engine "
                  "(`zig build`), or check out the engine revision the module was built from. "
                  "Loading it anyway would read fields at the wrong offsets and corrupt memory "
                  "somewhere unrelated -- see D12.",
                  module_name ? module_name : "<module>", result.engine_id, result.module_id,
                  HP_BUILD_ID_TARGET, HP_BUILD_ID_CONFIG, HP_BUILD_ID_PROFILING);
    return buffer;
}

} // namespace hp
