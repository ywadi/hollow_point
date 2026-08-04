# The editor application (T0013).
#
# A *consumer* of the engine, which is the central architectural rule this
# ticket exists to establish: when a game is exported the editor disappears
# entirely, so anything the game needs at runtime must live in the engine
# library rather than here.
#
# Deliberately not Diligent's add_sample_app(): that is DiligentSamples'
# framework, and the app shell comes from DiligentTools/NativeApp in T0015
# instead. G8's mandatory readme.md applies to add_sample_app() and therefore
# does not bind here -- the readme is present because it is useful, not because
# CMake demands it.
add_executable(hp_editor
    src/EditorMain.cpp
)

target_link_libraries(hp_editor PRIVATE hp::engine)
target_compile_features(hp_editor PRIVATE cxx_std_20)

# The sample gameplay module this app hosts (T0048).
#
# The NAME, not a path: MinGW keeps the `lib` prefix, so it is
# libhp_sandbox.dll on Windows and libhp_sandbox.so on Linux. Ask CMake rather
# than guessing -- hardcoding one built fine and failed at run time on the other
# target, twice, in the boundary suite. The app composes it against
# hp::executableDirectory() at run time, because a configure-time path is a
# *host* path and a Windows binary cannot open it.
target_compile_definitions(hp_editor PRIVATE
    HP_SANDBOX_MODULE_NAME="$<TARGET_FILE_NAME:hp_sandbox>")
add_dependencies(hp_editor hp_sandbox)

set_target_properties(hp_editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/apps/editor")

# Where the engine library is found at run time.
#
# CMake's default is to bake the build tree's link directories into RUNPATH,
# which produces a binary that works on this machine and nowhere else. That is
# not a theoretical concern: `dist` staged an executable whose RUNPATH was
# `$ORIGIN:/media/.../build/linux-x86_64-release/engine`, and it ran happily
# from the staged folder -- and even from a copy of it -- purely because that
# absolute path still existed locally. Hiding the build tree produced
# "cannot open shared object file", which is what a user would have seen.
#
# BUILD_WITH_INSTALL_RPATH stops the build-tree paths being added at all, so no
# absolute path is ever baked in. The two entries cover both layouts the binary
# runs from: `$ORIGIN` for the build tree and for a co-located export, and
# `$ORIGIN/../lib` for `dist`, which stages executables into bin/ and shared
# libraries into lib/. T0043 recorded this hazard; this is the app-side half.
if (WIN32)
    # Windows has no RPATH. A PE executable searches its own directory and then
    # PATH, so the engine DLL has to sit beside the exe or the process dies at
    # load with no diagnostic worth reading -- which is exactly how this was
    # found: the Linux build ran, the Windows build linked, and the exe simply
    # produced nothing under wine.
    #
    # $<TARGET_RUNTIME_DLLS> resolves the transitive set rather than naming
    # libhp_engine.dll here, so this keeps working when the engine grows
    # dependencies of its own.
    add_custom_command(TARGET hp_editor POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_RUNTIME_DLLS:hp_editor>" "$<TARGET_FILE_DIR:hp_editor>"
        COMMAND_EXPAND_LISTS
        VERBATIM)
else()
    set_target_properties(hp_editor PROPERTIES
        BUILD_WITH_INSTALL_RPATH ON
        INSTALL_RPATH "$ORIGIN:$ORIGIN/../lib")

    # And put the engine beside the executable in the build tree, so `$ORIGIN`
    # resolves there too without needing LD_LIBRARY_PATH.
    add_custom_command(TARGET hp_editor POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:hp_engine>" "$<TARGET_FILE_DIR:hp_editor>"
        VERBATIM)
endif()
