# hp_add_gameplay_module() — the only supported way to declare a gameplay module.
#
# It exists so that two things cannot be forgotten, both of which fail silently
# or confusingly when they are:
#
#   * the unload finalizer (T0105.1). Without it a module that is dlclose'd
#     kills the process at exit, far from the cause.
#   * MODULE rather than SHARED, hidden visibility, and the output layout the
#     loader expects.
#
# T0104 will add build-id stamping here for the same reason: "so a module cannot
# accidentally be built without one" is its 104.3, and this is where that lands.
#
# Usage:
#   hp_add_gameplay_module(hp_sandbox
#       SOURCES src/Sandbox.cpp
#       OUTPUT_DIR "${CMAKE_BINARY_DIR}/samples/sandbox")

set(HP_MODULE_FINALIZE_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../engine/module/ModuleFinalize.cpp"
    CACHE INTERNAL "Unload finalizer compiled into every gameplay module (T0105.1)")

set(HP_MODULE_BUILD_ID_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../engine/module/ModuleBuildId.cpp"
    CACHE INTERNAL "Build-id stamp compiled into every gameplay module (T0104.3)")

function(hp_add_gameplay_module target)
    cmake_parse_arguments(ARG "" "OUTPUT_DIR" "SOURCES" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "hp_add_gameplay_module(${target}): SOURCES is required")
    endif()

    # MODULE rather than SHARED: loaded with dlopen/LoadLibrary at run time and
    # never linked against. On ELF the distinction is cosmetic; it matters
    # because it states the intent, and because nothing should be tempted to put
    # it on a target_link_libraries() line.
    #
    # The finalizer is appended to the author's sources rather than injected via
    # a linked object, because it must be compiled *into this DSO* — it
    # finalizes this module's own __dso_handle, and one carried in from a static
    # library would finalize the wrong thing or be dropped as unreferenced.
    add_library(${target} MODULE
        ${ARG_SOURCES}
        "${HP_MODULE_FINALIZE_SOURCE}"
        "${HP_MODULE_BUILD_ID_SOURCE}")

    target_link_libraries(${target} PRIVATE hp::engine)
    # The stamp includes the generated <hp/BuildId.h>, so the module cannot be
    # compiled before it exists.
    add_dependencies(${target} hp_build_id)
    target_compile_features(${target} PRIVATE cxx_std_20)

    set_target_properties(${target} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)

    if(ARG_OUTPUT_DIR)
        set_target_properties(${target} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${ARG_OUTPUT_DIR}"
            RUNTIME_OUTPUT_DIRECTORY "${ARG_OUTPUT_DIR}")
    endif()
endfunction()
