# Test suites (T0012).
#
# One executable per *bucket*, not one per test. Buckets are separated at the
# binary level rather than by runtime tags, because that is what actually keeps
# the inner loop fast: a GPU or perf test in another binary costs nothing to
# compile, nothing to link and nothing to run when you are iterating on unit
# tests. Tags only ever save the last of those three.
#
#   fast          pure unit tests, no device, no subprocesses -- the default
#   integration   drives real subprocesses; seconds rather than milliseconds
#   gpu           needs a graphics device (software rasterisation is fine)
#   perf          timing-sensitive; noisy on a loaded machine
#
# Adding a test means adding one .cpp to the matching directory. Nothing else --
# no edit to this file. CONFIGURE_DEPENDS is what makes that true: without it a
# new file is invisible until someone reconfigures by hand, and the friction is
# exactly what stops people writing tests.
#
# Run them with `zig build test` (see build.zig), not by invoking these targets
# directly -- the harness knows how to execute a cross-built suite.

# doctest is header-only, so it is consumed as a header rather than by adding
# its CMake project. That skips its install rules, package-config generation and
# its own test targets, none of which this project wants, and keeps
# third_party/doctest entirely unexecuted.
add_library(hp_doctest INTERFACE)
target_include_directories(hp_doctest SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/doctest")

# hp_add_test_bucket(<name>)
#   Creates hp_tests_<name> from tests/<name>/*.cpp, if any exist. A bucket with
#   no tests produces no target at all -- an empty executable would not link,
#   and a placeholder test is worse than no test.
function(hp_add_test_bucket bucket)
    file(GLOB _sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${bucket}/*.cpp")
    if(NOT _sources)
        return()
    endif()

    set(_target "hp_tests_${bucket}")
    add_executable(${_target} ${_sources} "${CMAKE_CURRENT_SOURCE_DIR}/doctest_main.cpp")
    target_link_libraries(${_target} PRIVATE hp_doctest)
    set_target_properties(${_target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_features(${_target} PRIVATE cxx_std_20)

    # Everything a test might want to exercise. Linked to every bucket so that
    # adding a test file never requires touching this file -- which is the
    # whole point. The linker drops what a bucket does not reference.
    #
    # hp_engine is here for the same reason: a test for GUIDs or the clock
    # should be droppable into tests/fast/ without editing this file. It is a
    # shared library, so each bucket needs to find it beside itself at run time
    # -- handled below, the same way the apps do it.
    target_link_libraries(${_target} PRIVATE enkiTS meshoptimizer hp_engine)

    if (WIN32)
        add_custom_command(TARGET ${_target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "$<TARGET_RUNTIME_DLLS:${_target}>" "$<TARGET_FILE_DIR:${_target}>"
            COMMAND_EXPAND_LISTS
            VERBATIM)
    else()
        set_target_properties(${_target} PROPERTIES
            BUILD_WITH_INSTALL_RPATH ON
            INSTALL_RPATH "$ORIGIN")
        add_custom_command(TARGET ${_target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "$<TARGET_FILE:hp_engine>" "$<TARGET_FILE_DIR:${_target}>"
            VERBATIM)
    endif()

    # The engine's public headers, so tests can exercise engine-facing surfaces
    # like <hp/Profiling.hpp> without linking the library -- that header is
    # deliberately dependency-free (T0019), and a test that had to link the
    # engine to use it would not be testing that property.
    target_include_directories(${_target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include")

    # enkiTS's headers are not exported by its target in a usable form for
    # consumers outside its own tree.
    target_include_directories(${_target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/enkiTS/src")

    if(NOT WIN32)
        find_package(Threads REQUIRED)
        target_link_libraries(${_target} PRIVATE Threads::Threads)
    endif()

    message(STATUS "tests: ${_target} (${bucket})")
endfunction()

# --- module boundary fixtures (T0095, D12) -----------------------------------
#
# A miniature of the real artifact layout: a *shared* "engine" that both the
# test executable and a loadable "gameplay module" link, so engine state exists
# once per process. The module is loaded with dlopen/LoadLibrary at run time and
# is deliberately NOT linked to the test.
#
# Shared, not static, is the whole point. Statically linking the engine into
# both sides still compiles and links -- and then each side gets its own copy of
# every engine global, which is the silent failure D12 exists to prevent. If
# these ever become STATIC, tests/integration/module_boundary_test.cpp fails,
# which is exactly what it is for.
add_library(hp_abi_engine SHARED "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/abi_engine.cpp")
add_library(hp_abi_module SHARED "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/abi_module.cpp")

foreach(_fixture hp_abi_engine hp_abi_module)
    target_compile_features(${_fixture} PRIVATE cxx_std_20)
    # EnTT arrives via DiligentFX (D7); the fixtures only need its headers.
    target_include_directories(${_fixture} SYSTEM PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/entt/src")
    # Everything crossing the boundary is exported explicitly, so hide the rest.
    set_target_properties(${_fixture} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endforeach()

# The module resolves engine symbols from the engine library, on both platforms:
# ELF by SONAME, PE by import from a named DLL. Neither needs the executable to
# export anything, which is why D12 could reject the exe-exports alternative.
target_link_libraries(hp_abi_module PRIVATE hp_abi_engine)

foreach(_bucket fast integration gpu perf)
    hp_add_test_bucket(${_bucket})
endforeach()

# The integration bucket links the engine fixture and loads the module fixture.
# Guarded because a bucket with no sources produces no target at all.
if(TARGET hp_tests_integration)
    target_link_libraries(hp_tests_integration PRIVATE hp_abi_engine)
    add_dependencies(hp_tests_integration hp_abi_module)
    # The module is opened by name at run time, and the name is not the same on
    # both targets -- MinGW produces libhp_abi_module.dll, keeping the `lib`
    # prefix that MSVC would drop. Ask CMake rather than guessing: hardcoding
    # "hp_abi_module.dll" built fine and failed every Windows test at run time.
    target_compile_definitions(hp_tests_integration PRIVATE
        HP_ABI_MODULE_NAME="$<TARGET_FILE_NAME:hp_abi_module>")
    if(NOT WIN32)
        # The boundary fixture, beside the executable for the same reason the
        # engine is. RPATH is already $ORIGIN from the bucket helper above.
        add_custom_command(TARGET hp_tests_integration POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "$<TARGET_FILE:hp_abi_engine>" "$<TARGET_FILE_DIR:hp_tests_integration>"
            VERBATIM)
    endif()
endif()
