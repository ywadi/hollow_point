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
    src/EditorCamera.cpp
    src/EditorLayer.cpp
    src/EditorPanels.cpp
)

target_link_libraries(hp_editor PRIVATE hp::engine)

# Dear ImGui, so the editor can *call* it (T0032.2).
#
# `Diligent-Imgui` rather than raw ImGui, which is D6's instruction and is right
# for a second reason here: it carries `IMGUI_USER_CONFIG` as a PUBLIC compile
# definition, and this binary's ImGui must be configured **identically** to the
# engine's or the two disagree about `ImGuiIO`'s layout while sharing one
# context. Linking the same target is what makes that true by construction
# rather than by two CMake files agreeing.
#
# This does give the process a second copy of ImGui's code -- see `hp::UiBinding`
# for why that is the arrangement rather than a defect, and note it is *code*
# that is duplicated, not state: there is one context, and `EditorLayer::onAttach`
# points this copy at it.
target_link_libraries(hp_editor PRIVATE Diligent-Imgui)
target_compile_features(hp_editor PRIVATE cxx_std_20)

# The viewport camera, which DiligentEngine already wrote (T0172, **D40**).
#
# `FirstPersonCamera` and `TrackballCamera` live in `DiligentSamples/SampleBase`,
# and this app compiles the two source files it needs **directly** rather than
# linking `Diligent-SampleBase`. That is not squeamishness about a sample: the
# target link-depends PUBLIC on `${ENGINE_LIBRARIES}` and `Diligent-NativeAppBase`,
# so linking it would pull a *second* graphics-engine flavour into a process that
# already has one through `hp::engine` -- the duplicate-Diligent hazard
# `engine/CMakeLists.txt` describes at length, and the one D12 exists to prevent.
# Two translation units is the whole cost of avoiding it.
#
# `InputController<Platform>.cpp` is here for exactly one symbol: the platform
# controller's out-of-line constructor/destructor, which `EditorInputController`
# needs because it derives from the `InputController` typedef -- the parameter
# type `FirstPersonCamera::Update` takes. None of its event handling is ever
# called; this app's events come from SDL3 (T0015).
set(HP_SAMPLE_BASE "${CMAKE_SOURCE_DIR}/third_party/DiligentEngine/DiligentSamples/SampleBase")

target_sources(hp_editor PRIVATE "${HP_SAMPLE_BASE}/src/FirstPersonCamera.cpp")

# Include directories taken from the targets that own them rather than spelled
# out, so a Diligent reorganisation does not silently break this -- the same
# pattern `engine/CMakeLists.txt` uses for `Diligent-Common`.
target_include_directories(hp_editor SYSTEM PRIVATE
    "${HP_SAMPLE_BASE}/include"
    $<TARGET_PROPERTY:Diligent-GraphicsEngine,INTERFACE_INCLUDE_DIRECTORIES>)

# `FirstPersonCamera::SetReferenceAxes` calls `LOG_WARNING_MESSAGE`, which
# resolves to `Diligent::DebugMessageCallback` -- a global defined in the
# platform library. The engine links the platform layer PRIVATE and builds with
# `-fvisibility=hidden`, so its copy is not exported and this app needs its own.
#
# **A second copy of a few hundred lines, and it is contained.** The variable is
# a message sink: this app's Diligent asserts print through this app's copy, the
# engine's through the engine's, and nothing correlates the two. That is not the
# duplicate-state hazard D12 is about -- which is engine state a game observes --
# and it is the same arrangement `Diligent-SampleBase` itself uses. The
# alternative is exporting Diligent's symbols from `libhp_engine.so`, which
# would put a vendored library's ABI on this engine's public surface.
target_link_libraries(hp_editor PRIVATE Diligent-TargetPlatform)

if (WIN32)
    target_sources(hp_editor PRIVATE "${HP_SAMPLE_BASE}/src/Win32/InputControllerWin32.cpp")
else()
    target_sources(hp_editor PRIVATE "${HP_SAMPLE_BASE}/src/Linux/InputControllerLinux.cpp")
    # X11 and xcb resolve from `third_party/sysroot/`, not from /usr -- the build
    # is hermetic (see the trap list in CLAUDE.md). `XCBKeySyms` is Diligent's own
    # vendored copy under DiligentTools/NativeApp/Linux, already a target here.
    find_package(X11 REQUIRED)
    target_link_libraries(hp_editor PRIVATE XCBKeySyms X11::X11)
endif()

# The sample gameplay module this app hosts (T0048).
#
# The NAME, not a path: MinGW keeps the `lib` prefix, so it is
# libhp_sandbox.dll on Windows and libhp_sandbox.so on Linux. Ask CMake rather
# than guessing -- hardcoding one built fine and failed at run time on the other
# target, twice, in the boundary suite. The app composes it against
# hp::executableDirectory() at run time, because a configure-time path is a
# *host* path and a Windows binary cannot open it.
target_compile_definitions(hp_editor PRIVATE
    HP_SANDBOX_MODULE_NAME="$<TARGET_FILE_NAME:hp_sandbox>"
    # The second sample (T0157), named the same way and for the same reason.
    # The editor knowing two module names by macro is the shape T0024's project
    # replaces — a project says which modules it loads — and until it exists,
    # naming them here is honest about the fact that nothing else can.
    HP_ROCKCUBE_MODULE_NAME="$<TARGET_FILE_NAME:hp_rockcube>")
add_dependencies(hp_editor hp_sandbox hp_rockcube)

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
    # **The engine copy must be fresh even when the exe does not relink.**
    # A Windows exe links against the import library, which does not change when
    # an engine `.cpp` is recompiled without altering the exported symbols -- so
    # POST_BUILD never fires and the app runs the previous DLL with no
    # diagnostic. Measured against the test harness on 2026-08-05; the full
    # account is in tests/CMakeLists.txt.
    #
    # The dependency runs exe -> copy, not copy -> exe: `build.zig` builds named
    # targets, so a custom target in `all` is never reached.
    # A stamp is the output: `OUTPUT` accepts only a restricted set of generator
    # expressions and `TARGET_FILE_NAME` is not one of them. `COMMAND` is fine.
    set(_engine_stamp "${CMAKE_CURRENT_BINARY_DIR}/hp_engine_copy.stamp")
    add_custom_command(
        OUTPUT "${_engine_stamp}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:hp_engine>" "$<TARGET_FILE_DIR:hp_editor>"
        # The slang runtime, beside the exe for the same reason the engine is
        # (T0142): loaded at run time by name from the exe's own directory.
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                ${HP_SLANG_RUNTIME_LIBRARIES} "$<TARGET_FILE_DIR:hp_editor>"
        COMMAND "${CMAKE_COMMAND}" -E touch "${_engine_stamp}"
        DEPENDS hp_engine
        COMMENT "Refreshing hp_engine beside hp_editor"
        VERBATIM)
    add_custom_target(hp_editor_engine_copy DEPENDS "${_engine_stamp}")
    add_dependencies(hp_editor hp_editor_engine_copy)

    # The transitive set still rides on the relink, which is correct for it: a
    # new dependency changes the import library, so the exe does relink.
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
    # resolves there too without needing LD_LIBRARY_PATH. The slang runtime
    # rides along (T0142): the engine loads it by name from the exe directory.
    add_custom_command(TARGET hp_editor POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:hp_engine>" "$<TARGET_FILE_DIR:hp_editor>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                ${HP_SLANG_RUNTIME_LIBRARIES} "$<TARGET_FILE_DIR:hp_editor>"
        VERBATIM)
endif()
