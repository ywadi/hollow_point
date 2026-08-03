# The engine library (T0013).
#
# SHARED, not static, and that is the whole architecture rather than a build
# preference (D12). The editor, the runtime and every gameplay module link this
# one library, so engine state -- the logger, registries, asset pools -- exists
# exactly once per process. Linking it statically into both an executable and a
# gameplay module compiles and links perfectly well and then gives each side its
# own copy of every global, which is a runtime failure that looks like
# impossible bugs in gameplay code.
#
# Proven on both targets before this layout was committed; see
# tests/integration/module_boundary_test.cpp and T0095.
add_library(hp_engine SHARED
    src/Engine.cpp
)
add_library(hp::engine ALIAS hp_engine)

target_include_directories(hp_engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

# PRIVATE: consumers must see the *import* side of HP_API, never the export
# side. Making this PUBLIC would have every app and module claim to define the
# engine's symbols, which fails at link on Windows and silently duplicates them
# elsewhere.
target_compile_definitions(hp_engine PRIVATE HP_ENGINE_BUILD)

# C++20 for our own code while third-party stays at the root's CMAKE_CXX_STANDARD
# 17 (see documentation/06-engine-conventions.md). Verified working on both
# targets rather than assumed.
target_compile_features(hp_engine PUBLIC cxx_std_20)

# Hidden by default so the exported surface is exactly what HP_API marks. This
# is also what keeps RTTI across the module boundary honest: types that cross it
# are annotated deliberately rather than exported by accident.
set_target_properties(hp_engine PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

# Deliberately links nothing from Diligent yet.
#
# 13.3 asks for PUBLIC vs PRIVATE to be chosen deliberately, and the deliberate
# choice today is "nothing", because nothing here needs a device. When T0015
# brings the window and T0025 the render layer, the rule to apply is: link
# Diligent targets PRIVATE unless a *public engine header* names their types.
# D6 records that DiligentFX links Diligent-Imgui PUBLIC, so a careless PUBLIC
# here would hand ImGui to every gameplay module whether it wants it or not.
