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
    target_compile_features(${_target} PRIVATE cxx_std_17)

    # Everything a test might want to exercise. Linked to every bucket so that
    # adding a test file never requires touching this file -- which is the
    # whole point. The linker drops what a bucket does not reference.
    target_link_libraries(${_target} PRIVATE enkiTS meshoptimizer)

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

foreach(_bucket fast integration gpu perf)
    hp_add_test_bucket(${_bucket})
endforeach()
