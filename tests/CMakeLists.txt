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

    # --- the engine copy that has to be fresh on EVERY build ------------------
    #
    # **This was a POST_BUILD on the test binary, and it silently ran the wrong
    # engine.** POST_BUILD fires only when the target is *rebuilt*. On Windows a
    # test executable links against the **import library**, which does not change
    # when an engine `.cpp` is recompiled without altering the exported symbol
    # set -- so the `.exe` is up to date, nothing relinks, the copy never runs,
    # and the suite executes against the **previous** `libhp_engine.dll` beside
    # it.
    #
    # The failure mode is the worst kind: the suite passes or fails on stale code
    # with no diagnostic. Measured on 2026-08-05, twice in one session and in
    # both directions -- once a green Windows suite while the same source
    # segfaulted on Linux, and once two Windows failures for a bug already fixed.
    # Both were the harness, not the code.
    #
    # **An `ALL` custom target does not fix it**, which was the first attempt and
    # was measured not to work: `build.zig` builds named targets
    # (`cmake --build --target hp_tests_fast`) so that the fast suite does not
    # drag in ~1100 engine targets, and a target in `all` is simply not reached.
    # The dependency has to run the other way -- the test binary depends on the
    # copy -- so that asking for the binary asks for a fresh engine beside it.
    #
    # One rule for the whole directory, because every bucket shares this output
    # directory and two custom commands writing one path is a configure error.
    # A **stamp** is the output, not the copied library itself: `OUTPUT` accepts
    # only a restricted set of generator expressions and `TARGET_FILE_NAME` is
    # not among them, so naming the destination there fails at generate time
    # with "No target hp_engine" -- measured, and the reason this indirection
    # exists rather than the obvious spelling. `COMMAND` has no such limit.
    if (NOT TARGET hp_tests_engine_copy)
        set(_engine_stamp "${CMAKE_CURRENT_BINARY_DIR}/hp_engine_copy.stamp")
        add_custom_command(
            OUTPUT "${_engine_stamp}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "$<TARGET_FILE:hp_engine>" "${CMAKE_CURRENT_BINARY_DIR}"
            # The slang runtime, beside the binaries for the same reason the
            # engine is (T0142): the engine loads it at run time by name, and
            # the executable's own directory is the one place both platforms'
            # loaders look without help -- under wine especially, where no
            # POSIX path is usable. The pinned files never change between
            # builds, so riding this stamp is fresh enough.
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    ${HP_SLANG_RUNTIME_LIBRARIES} "${CMAKE_CURRENT_BINARY_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E touch "${_engine_stamp}"
            DEPENDS hp_engine
            COMMENT "Refreshing hp_engine beside the test binaries"
            VERBATIM)
        add_custom_target(hp_tests_engine_copy DEPENDS "${_engine_stamp}")
    endif()
    add_dependencies(${_target} hp_tests_engine_copy)

    if (WIN32)
        # The **transitive** set -- SDL3 and anything else the engine grows --
        # still rides on the relink, which is correct for it: a new dependency
        # changes the import library, so the exe does relink and this fires. It
        # is only `hp_engine` itself that changes on almost every build without
        # touching its exports, which is why that one gets the rule above.
        add_custom_command(TARGET ${_target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "$<TARGET_RUNTIME_DLLS:${_target}>" "$<TARGET_FILE_DIR:${_target}>"
            COMMAND_EXPAND_LISTS
            VERBATIM)
    else()
        set_target_properties(${_target} PROPERTIES
            BUILD_WITH_INSTALL_RPATH ON
            INSTALL_RPATH "$ORIGIN")
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
# hp_engine too: a real gameplay module links the engine, and the boundary
# suite asserts that a module can log to the engine's sinks (T0054, T0066).
target_link_libraries(hp_abi_module PRIVATE hp_abi_engine hp_engine)

# --- unload fixtures (T0105.1) ------------------------------------------------
#
# Two modules from ONE source, differing only by whether the unload finalizer is
# compiled in, plus a probe process that performs a real unload (no
# RTLD_NODELETE). This is what turns "the fix works" from an observation into a
# test: the control module must still crash, or the test is asserting nothing.
#
# If hp_unload_module_broken ever stops crashing on Linux, the toolchain bug
# (ziglang/zig#17908) has been fixed upstream and this whole mechanism can be
# revisited -- the test says so when it fails that way.
add_library(hp_unload_module_fixed  MODULE
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/unload_module.cpp"
    "${HP_MODULE_FINALIZE_SOURCE}")
add_library(hp_unload_module_broken MODULE
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/unload_module.cpp")

foreach(_um hp_unload_module_fixed hp_unload_module_broken)
    target_compile_features(${_um} PRIVATE cxx_std_20)
    set_target_properties(${_um} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endforeach()

# --- build-id fixtures (T0104.5) ---------------------------------------------
#
# Two modules: one stamped by hp_add_gameplay_module() with this engine's real
# id, and one hand-rolled carrying an id belonging to no build of anything. The
# stale one is deliberately NOT built through the helper, because the helper's
# entire purpose is to make staleness impossible -- producing a stale module
# means going around it.
hp_add_gameplay_module(hp_stamped_module
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/unload_module.cpp"
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")

add_library(hp_stale_module MODULE "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/stale_module.cpp")
target_compile_features(hp_stale_module PRIVATE cxx_std_20)
set_target_properties(hp_stale_module PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(hp_unload_probe "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/unload_probe.cpp")
target_compile_features(hp_unload_probe PRIVATE cxx_std_20)
set_target_properties(hp_unload_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
if(NOT WIN32)
    target_link_libraries(hp_unload_probe PRIVATE ${CMAKE_DL_LIBS})
endif()

# T0048/T0127: a gameplay module whose entry point throws. Declared through
# hp_add_gameplay_module() like any other module -- the point of the fixture is
# that the guard HP_GAMEPLAY_MODULE installs is real, so it must be built the
# way a real module is.
hp_add_gameplay_module(hp_throwing_module
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/throwing_module.cpp"
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")

foreach(_bucket fast integration gpu perf)
    hp_add_test_bucket(${_bucket})
endforeach()

# The gpu bucket sees Diligent's interface headers (T0161.1). **Include
# directories only, never the libraries** -- the same trick `engine/CMakeLists`
# uses for `Diligent-Common`: every call the tests make goes through a COM-style
# vtable on objects the engine created, so no Diligent code is linked twice and
# D12's one-copy rule holds. The platform defines (`PLATFORM_LINUX`, ...) arrive
# transitively through hp_engine's PUBLIC `Diligent-PublicBuildSettings`.
#
# This exists for the T0161.1 measurement, which must build a second
# `PipelineResourceSignature` against the raw device -- the engine deliberately
# has no public API for that, and giving it one for a benchmark would be
# backwards.
if(TARGET hp_tests_gpu)
    target_include_directories(hp_tests_gpu SYSTEM PRIVATE
        $<TARGET_PROPERTY:Diligent-GraphicsEngine,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:Diligent-GraphicsAccessories,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:Diligent-Common,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:Diligent-Primitives,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:Diligent-PlatformInterface,INTERFACE_INCLUDE_DIRECTORIES>)
endif()

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
    # T0105.1's unload regression. Paths rather than names: the probe is spawned
    # with an absolute path so the test does not depend on the working directory.
    add_dependencies(hp_tests_integration
        hp_unload_probe hp_unload_module_fixed hp_unload_module_broken)
    # NAMES, not paths, for the same reason HP_ABI_MODULE_NAME is a name: these
    # definitions are baked at configure time on the *host*, so a Windows binary
    # would receive a POSIX path it cannot open. The test composes them against
    # exe_dir() at run time, which is correct on both targets and under interop.
    add_dependencies(hp_tests_integration hp_stamped_module hp_stale_module hp_sandbox)
    # The rock cube sample (T0157): the module that actually moves something.
    add_dependencies(hp_tests_integration hp_rockcube)
    add_dependencies(hp_tests_integration hp_throwing_module)
    # The real sample gameplay module is loaded from where it is built, by a
    # path relative to this binary, rather than copied next to it.
    #
    # A POST_BUILD copy was the obvious approach and is quietly wrong: it only
    # runs when *this* target relinks, so editing only the module leaves a stale
    # copy beside the test. That produced a mutation test which "passed" while
    # exercising the previous build -- i.e. a guard that was guarding nothing.
    # A relative path cannot go stale.
    target_compile_definitions(hp_tests_integration PRIVATE
        HP_SANDBOX_MODULE_NAME="$<TARGET_FILE_NAME:hp_sandbox>"
        HP_ROCKCUBE_MODULE_NAME="$<TARGET_FILE_NAME:hp_rockcube>"
        HP_STAMPED_MODULE_NAME="$<TARGET_FILE_NAME:hp_stamped_module>"
        HP_STALE_MODULE_NAME="$<TARGET_FILE_NAME:hp_stale_module>"
        HP_UNLOAD_PROBE_NAME="$<TARGET_FILE_NAME:hp_unload_probe>"
        HP_UNLOAD_MODULE_FIXED_NAME="$<TARGET_FILE_NAME:hp_unload_module_fixed>"
        HP_UNLOAD_MODULE_BROKEN_NAME="$<TARGET_FILE_NAME:hp_unload_module_broken>"
        HP_THROWING_MODULE_NAME="$<TARGET_FILE_NAME:hp_throwing_module>")
    if(NOT WIN32)
        # The boundary fixture, beside the executable for the same reason the
        # engine is. RPATH is already $ORIGIN from the bucket helper above.
        add_custom_command(TARGET hp_tests_integration POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "$<TARGET_FILE:hp_abi_engine>" "$<TARGET_FILE_DIR:hp_tests_integration>"
            VERBATIM)
    endif()
endif()
