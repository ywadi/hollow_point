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
    src/Application.cpp
    src/Engine.cpp
    src/Guid.cpp
    src/Log.cpp
    src/Time.cpp
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

# --- logging (T0054) ---------------------------------------------------------
#
# The compile-time floor. Levels below this are removed by the preprocessor
# rather than filtered at run time, so verbose logging is absent from a shipped
# build instead of merely skipped -- and, as with the profiling macros, its
# arguments are not evaluated.
#
# PUBLIC for the same reason HP_PROFILING is: the macros live in a public header
# and consumers must compile against the same floor as the engine.
set(HP_LOG_MIN_LEVEL "0" CACHE STRING
    "Compile-time minimum log level: 0=trace 1=debug 2=info 3=warning (error/fatal always on)")
target_compile_definitions(hp_engine PUBLIC HP_LOG_MIN_LEVEL=${HP_LOG_MIN_LEVEL})

# --- profiling (T0019) -------------------------------------------------------
#
# One switch, off by default, and it is a *compile-time* switch: off means the
# instrumentation does not exist rather than being skipped at run time. That is
# what preserves shipped-build performance, and it is why the macros must not so
# much as name their arguments when disabled.
#
# PUBLIC, not PRIVATE, and this is the part that matters. The macros live in a
# public header, so every consumer -- the editor, the runtime, every gameplay
# module -- must compile against the *same* setting as the engine. A module
# built with profiling on against an engine built without it is exactly the
# mismatch T0104 exists to refuse at load, because enabling profiling changes
# the engine's symbol surface. Making this PRIVATE would let that happen
# silently and is the single easiest way to get this wrong.
option(HP_PROFILING "Compile in profiling instrumentation (Tracy, T0029)" OFF)
if (HP_PROFILING)
    target_compile_definitions(hp_engine PUBLIC HP_PROFILING)
    message(STATUS "hp_engine: profiling instrumentation ENABLED")
endif()

# Deliberately links nothing from Diligent yet.
#
# 13.3 asks for PUBLIC vs PRIVATE to be chosen deliberately, and the deliberate
# choice today is "nothing", because nothing here needs a device. When T0015
# brings the window and T0025 the render layer, the rule to apply is: link
# Diligent targets PRIVATE unless a *public engine header* names their types.
# D6 records that DiligentFX links Diligent-Imgui PUBLIC, so a careless PUBLIC
# here would hand ImGui to every gameplay module whether it wants it or not.
