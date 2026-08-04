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

# --- configuration flags that the build id has to see ------------------------
#
# Declared *here*, above the build id, and that ordering is load-bearing.
#
# It used to be declared further down, next to the code it guards, which reads
# better and was wrong: the custom command below expands ${HP_PROFILING} at
# configure time, so on a fresh tree -- where nothing has put it in the cache
# yet -- it expanded to the empty string. `option()` then wrote OFF to the
# cache, and the *next* configure expanded it to "OFF" instead. The id therefore
# changed on a no-op reconfigure, with no source, no header and no argument
# different: measured 00947a49dbbb42a3 -> b88f8277d7bec25f (T0105.2).
#
# The consequence is the guard misfiring rather than failing to fire: every
# module built before that reconfigure is refused by an engine built after it,
# for a difference that does not exist. T0104's header warns about a stale id
# claiming compatibility it does not have; this is the same hazard from the
# other side, and it is the more confusing one, because the diagnostic is
# correct about the ids and wrong about the world.
#
# **Any flag that feeds the id must be declared before the command that reads
# it.** That is the rule this comment exists to keep.
option(HP_PROFILING "Compile in profiling instrumentation (Tracy, T0029)" OFF)

# --- build id (T0104) --------------------------------------------------------
#
# Generated at BUILD time with the public headers as declared inputs, so ninja
# regenerates it when a header changes. A configure-time hash would go stale
# silently -- CMake re-runs configure for a CMakeLists.txt change, not a header
# change -- and a stale id claims compatibility it does not have, which is the
# one failure this whole ticket exists to prevent.
set(HP_BUILD_ID_HEADER "${CMAKE_BINARY_DIR}/generated/hp/BuildId.h")

file(GLOB _hp_public_headers CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/include/hp/*.hpp")

add_custom_command(
    OUTPUT "${HP_BUILD_ID_HEADER}"
    COMMAND "${CMAKE_COMMAND}"
            -DHP_ID_INCLUDE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/include
            -DHP_ID_TARGET=${HP_TARGET}
            -DHP_ID_CONFIG=${CMAKE_BUILD_TYPE}
            -DHP_ID_PROFILING=${HP_PROFILING}
            -DHP_ID_OUTPUT=${HP_BUILD_ID_HEADER}
            -P "${CMAKE_SOURCE_DIR}/cmake/hp_build_id.cmake"
    DEPENDS ${_hp_public_headers} "${CMAKE_SOURCE_DIR}/cmake/hp_build_id.cmake"
    COMMENT "Computing engine build id (T0104)"
    VERBATIM)

add_custom_target(hp_build_id DEPENDS "${HP_BUILD_ID_HEADER}")

add_library(hp_engine SHARED
    src/Module.cpp
    src/ModuleHost.cpp
    src/Reflect.cpp
    src/Application.cpp
    src/Engine.cpp
    src/Guid.cpp
    src/Input.cpp
    src/Paths.cpp
    src/Render.cpp
    src/Layer.cpp
    src/Log.cpp
    src/Time.cpp
    src/Window.cpp
)
add_library(hp::engine ALIAS hp_engine)

target_include_directories(hp_engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    # PUBLIC because gameplay modules include <hp/BuildId.h> to stamp
    # themselves, and they must get the *same* generated header as the engine --
    # that identity is what makes a mismatch mean something.
    "${CMAKE_BINARY_DIR}/generated")

# The generated header is an input to compiling the engine, so say so rather
# than relying on target ordering.
add_dependencies(hp_engine hp_build_id)

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
#
# The option itself is declared at the top of this file, above the build id that
# has to hash it -- see the comment there for why moving it back down breaks the
# id.
if (HP_PROFILING)
    target_compile_definitions(hp_engine PUBLIC HP_PROFILING)
    message(STATUS "hp_engine: profiling instrumentation ENABLED")
endif()

# SDL3, PRIVATE and static (D16). PRIVATE because no public engine header names
# an SDL type -- <hp/Window.hpp> exposes native handles as void*/uint64_t
# precisely so consumers never see SDL3/SDL.h, and gameplay cannot reach SDL
# behind the engine's back. Static so there is one fewer file beside every
# executable.
# PUBLIC, not PRIVATE. Gameplay modules register their own types into the
# engine's meta context, so they compile against entt's headers too -- and they
# must get the *same* headers, because the context's layout is shared across the
# boundary. D12's lockstep is what makes that safe.
target_link_libraries(hp_engine PUBLIC EnTT::EnTT)

target_link_libraries(hp_engine PRIVATE SDL3::SDL3-static)

# Diligent, PRIVATE (T0025).
#
# 13.3 asked for PUBLIC vs PRIVATE to be chosen deliberately, and this is that
# choice being made: **PRIVATE unless a public engine header names a Diligent
# type**, and none does. `hp/Render.hpp` is a pimpl precisely so that stays
# true, and `src/Render.cpp` is the only translation unit in the engine that
# includes Diligent at all.
#
# PUBLIC here is not a build error, which is what makes it dangerous: it hands
# Diligent's include path to every app and every gameplay module. D6 records
# that DiligentFX links Diligent-Imgui PUBLIC, so a careless PUBLIC would give
# ImGui to every module whether it wants it or not.
#
# The `-shared` variants rather than `-static`: they are the .so/.dll that
# `dist` already stages, and linking statically would pull both backends into
# libhp_engine, which removes any possibility of one being absent.
#
# The link is per-platform, and that is forced rather than chosen.
#
# On Windows, linking the engine DLLs' *import* libraries fails outright:
# GraphicsEngineVk_64r.dll exports `atexit`, zig's MinGW `dllcrt2.obj` defines
# it, and lld-link refuses the duplicate. Diligent's own answer is explicit
# loading -- link only the interface (headers, no import library) and
# LoadLibrary the DLL at run time. `LoadEngineDll.h` includes <Windows.h>, so
# that path exists on Windows and nowhere else, which is exactly why the split
# is here rather than being avoidable.
#
# On Linux there is no such collision, so the shared targets link directly and
# `GetEngineFactoryVk()` resolves at load time. Measured working on an RTX 2080.
if (WIN32)
    target_link_libraries(hp_engine PRIVATE
        Diligent-GraphicsEngineVkInterface
        Diligent-GraphicsEngineOpenGLInterface
        Diligent-Common)
    target_compile_definitions(hp_engine PRIVATE
        DILIGENT_VK_EXPLICIT_LOAD=1
        DILIGENT_OPENGL_EXPLICIT_LOAD=1)
    # The DLLs are loaded by name at run time, so they must exist beside the
    # executable -- which `dist` already guarantees, and the build tree gets via
    # the apps' POST_BUILD copy of the runtime DLL set.
    add_dependencies(hp_engine
        Diligent-GraphicsEngineVk-shared
        Diligent-GraphicsEngineOpenGL-shared)
else()
    target_link_libraries(hp_engine PRIVATE
        Diligent-GraphicsEngineVk-shared
        Diligent-GraphicsEngineOpenGL-shared
        Diligent-Common)
endif()
