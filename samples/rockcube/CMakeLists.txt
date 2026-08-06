# The rock cube sample (T0157).
#
# The second gameplay module in the tree, and 157.1 asked whether a second one
# needs anything the first did not. It needs exactly two things: this file, and
# one `add_subdirectory` line beside `samples/sandbox` in the root
# `CMakeLists.txt`. Everything else — MODULE, hidden visibility, the unload
# finalizer, the build-id stamp — comes from `hp_add_gameplay_module()`.
#
# What it needs that `sandbox` does not is **content**, and staging it is the
# part with a trap in it. See below.

hp_add_gameplay_module(hp_rockcube
    SOURCES src/RockCube.cpp
    OUTPUT_DIR "${CMAKE_BINARY_DIR}/samples/rockcube")

# --- content staging (157.5) -------------------------------------------------
#
# **The sample reads a staged content root beside its module, never the source
# tree.** A sample that walks up to find `samples/rockcube/content` works on the
# machine that built it and cannot ship, which makes it a worse test of the
# authoring path than one that behaves like a game: mount a directory, read
# everything through the VFS (D13).
#
# The rock textures are **copied from `test_assets/derived` at build time** and
# are not duplicated into `samples/`. That directory's name says it is for
# tests, and this is the coupling 157.5 asked to decide rather than default
# into. The reasoning, in full, is in the ticket; the short version is that the
# alternatives are worse — a second committed copy of 1 MB of PNG that must be
# kept in step by hand, or a run-time read of a path named `test_assets` from a
# shipped sample. What is here is a single declared build-time dependency, in
# one place, that T0024's project layout can repoint in one line.
set(_rockcube_content "${CMAKE_BINARY_DIR}/samples/rockcube/content")
set(_rockcube_textures "${CMAKE_SOURCE_DIR}/test_assets/derived")

# The rock set only. `metal_*` exists for the gpu suite's metallic cases and
# would be 900 KB of copy for a sample that never names it.
set(_rockcube_texture_files rock_basecolour.png rock_orm.png rock_height.png
                            rock_normal.png)

set(_rockcube_sources)
file(GLOB_RECURSE _rockcube_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/content/*")

set(_rockcube_stamp "${CMAKE_CURRENT_BINARY_DIR}/rockcube_content.stamp")
set(_rockcube_texture_inputs)
foreach(_texture IN LISTS _rockcube_texture_files)
    list(APPEND _rockcube_texture_inputs "${_rockcube_textures}/${_texture}")
endforeach()

# A stamp file rather than the copied files as OUTPUT: `OUTPUT` accepts only a
# restricted set of generator expressions and a directory copy has no single
# output to name. The stamp is what makes this re-run when a content file
# changes and not otherwise.
add_custom_command(
    OUTPUT "${_rockcube_stamp}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/content" "${_rockcube_content}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_rockcube_content}/textures"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_rockcube_texture_inputs} "${_rockcube_content}/textures"
    COMMAND ${CMAKE_COMMAND} -E touch "${_rockcube_stamp}"
    DEPENDS ${_rockcube_sources} ${_rockcube_texture_inputs}
    COMMENT "Staging the rock cube sample's content"
    VERBATIM)

add_custom_target(hp_rockcube_content DEPENDS "${_rockcube_stamp}")

# **This dependency points the way it does on purpose.** `build.zig` builds
# *named* targets and never `all`, so an `add_custom_target(... ALL ...)` never
# runs and `add_dependencies(hp_rockcube_content hp_rockcube)` would be
# backwards — nothing would ever ask for the content. Asking for the module has
# to be what asks for it.
add_dependencies(hp_rockcube hp_rockcube_content)
