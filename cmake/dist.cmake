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

# Application payload: each app target's executable *and* the shaders and
# textures add_sample_app() copies next to it. Staged as a tree so assets keep
# their relative layout and land beside the executable -- which is where the app
# looks for them, on both platforms.
#
# Copying the tree rather than hunting for executables also sidesteps the fact
# that CMake has no portable "is this executable" test: on Linux the app binary
# has no extension and is indistinguishable from an asset by name alone.
#
# Build bookkeeping is excluded: CMakeFiles/ holds intermediate objects and
# compiler-detection artefacts (CompilerIdC/a.exe), neither of which ships.
set(_app_count 0)
file(GLOB _app_dirs LIST_DIRECTORIES true "${BUILD_DIR}/apps/*")
foreach(d IN LISTS _app_dirs)
    if(NOT IS_DIRECTORY "${d}")
        continue()
    endif()
    file(GLOB_RECURSE _payload RELATIVE "${d}" "${d}/*")
    foreach(rel IN LISTS _payload)
        if(rel MATCHES "^CMakeFiles/" OR rel MATCHES "\\.(o|obj|d|ninja)$"
           OR rel MATCHES "cmake_install\\.cmake$")
            continue()
        endif()
        get_filename_component(_sub "${rel}" DIRECTORY)
        file(COPY "${d}/${rel}" DESTINATION "${DIST_DIR}/bin/${_sub}")
        math(EXPR _app_count "${_app_count} + 1")
    endforeach()
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

message(STATUS "dist: ${DIST_DIR} -- ${_app_count} app file(s), "
               "${_shared_count} shared, ${_static_count} static/import")
