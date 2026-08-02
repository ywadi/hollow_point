# Stage a built tree into dist/<target>/ -- run with `cmake -P`, not included.
#
#   cmake -DBUILD_DIR=build/linux-x86_64-release -DDIST_DIR=dist/linux-x86_64 \
#         -DTARGET_OS=linux -P cmake/dist.cmake
#
# Driven by `zig build dist`. Kept as a CMake script rather than harness code so
# it behaves identically on both hosts and still works without the harness.
#
# Layout produced:
#   bin/      executables and, on Windows, the runtime DLLs they need
#   lib/      static libraries, plus the Linux shared objects and Windows
#             import libraries an application links against

cmake_minimum_required(VERSION 3.20)

foreach(v BUILD_DIR DIST_DIR TARGET_OS)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "dist.cmake: -D${v}=... is required")
    endif()
endforeach()

if(NOT EXISTS "${BUILD_DIR}")
    message(FATAL_ERROR "dist.cmake: no build tree at ${BUILD_DIR} -- build first")
endif()

if(TARGET_OS STREQUAL "windows")
    set(_shared_glob "*.dll")
    set(_implib_glob "*.dll.a" "*.lib")
else()
    set(_shared_glob "*.so" "*.so.*")
    set(_implib_glob)
endif()

file(REMOVE_RECURSE "${DIST_DIR}")
file(MAKE_DIRECTORY "${DIST_DIR}/bin" "${DIST_DIR}/lib")

# Executables. CMake has no portable "is executable" test, so identify them by
# what they are not: on Windows by .exe, on Linux by living in a bin/ directory
# or having no extension at the top of a target directory.
set(_exe_count 0)
if(TARGET_OS STREQUAL "windows")
    file(GLOB_RECURSE _exes "${BUILD_DIR}/*.exe")
else()
    file(GLOB_RECURSE _exes "${BUILD_DIR}/apps/*")
    list(FILTER _exes EXCLUDE REGEX "\\.(o|a|so|so\\.[0-9.]+|d|cmake|txt|json|ninja)$")
    list(FILTER _exes EXCLUDE REGEX "/CMakeFiles/")
endif()
foreach(f IN LISTS _exes)
    if(NOT IS_DIRECTORY "${f}")
        file(COPY "${f}" DESTINATION "${DIST_DIR}/bin")
        math(EXPR _exe_count "${_exe_count} + 1")
    endif()
endforeach()

# Shared libraries. On Windows the DLLs must sit beside the exe to be found at
# all; on Linux they go to lib/ and are located by rpath or LD_LIBRARY_PATH.
set(_shared_dest "${DIST_DIR}/lib")
if(TARGET_OS STREQUAL "windows")
    set(_shared_dest "${DIST_DIR}/bin")
endif()

set(_shared_count 0)
foreach(g IN LISTS _shared_glob)
    file(GLOB_RECURSE _found "${BUILD_DIR}/${g}")
    foreach(f IN LISTS _found)
        if(NOT f MATCHES "/CMakeFiles/")
            file(COPY "${f}" DESTINATION "${_shared_dest}")
            math(EXPR _shared_count "${_shared_count} + 1")
        endif()
    endforeach()
endforeach()

# Static and import libraries -- what an application links against.
set(_static_count 0)
set(_lib_globs "*.a")
list(APPEND _lib_globs ${_implib_glob})
foreach(g IN LISTS _lib_globs)
    file(GLOB_RECURSE _found "${BUILD_DIR}/${g}")
    foreach(f IN LISTS _found)
        if(NOT f MATCHES "/CMakeFiles/")
            file(COPY "${f}" DESTINATION "${DIST_DIR}/lib")
            math(EXPR _static_count "${_static_count} + 1")
        endif()
    endforeach()
endforeach()

message(STATUS "dist: ${DIST_DIR} -- ${_exe_count} executable(s), "
               "${_shared_count} shared, ${_static_count} static/import")
