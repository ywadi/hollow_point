cmake_minimum_required (VERSION 3.20)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

project(HollowPoint CXX)

# DiligentSamples/SampleBase compiles a Win32 .rc, so the RC language has to be
# live before that subdirectory is added. The zig toolchain points CMAKE_RC_*
# at `zig rc`; see cmake/toolchains/x86_64-windows-gnu.cmake.
if (WIN32)
    enable_language(RC)
endif()

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prefer GLVND over the legacy libGL (silences FindOpenGL policy warnings).
if (POLICY CMP0072)
    cmake_policy(SET CMP0072 NEW)
endif()

# Diligent supplies the renderer. We only need SampleBase as an app shell, so skip
# building the ~40 bundled tutorials/samples: add_sample_app() is still defined.
set(DILIGENT_BUILD_SAMPLE_BASE_ONLY ON  CACHE BOOL "" FORCE)
set(DILIGENT_BUILD_TOOLS            ON  CACHE BOOL "" FORCE)
set(DILIGENT_BUILD_FX               ON  CACHE BOOL "" FORCE)
set(DILIGENT_BUILD_SAMPLES          ON  CACHE BOOL "" FORCE)
set(DILIGENT_INSTALL_SAMPLES        OFF CACHE BOOL "" FORCE)
set(DILIGENT_INSTALL_CORE           OFF CACHE BOOL "" FORCE)
set(DILIGENT_INSTALL_TOOLS          OFF CACHE BOOL "" FORCE)
set(DILIGENT_INSTALL_FX             OFF CACHE BOOL "" FORCE)

# Dear ImGui: use the upstream docking branch instead of the copy vendored as a
# submodule inside DiligentEngine, so dock panels are available.
# DILIGENT_DEAR_IMGUI_PATH is Diligent's own supported override, so the engine
# tree stays untouched and updates cleanly.
#
# third_party/imgui is ocornut/imgui `docking` (1.92.9b), newer than the 1.92.1
# Diligent bundles. Diligent's fork carried one commit on top of upstream --
# commenting out an InputScalar assert and initialising a stb_textedit local --
# and neither is needed here: upstream has since commented out that same assert
# itself, and the local was refactored away.
#
# Must be set before DiligentEngine is added: DiligentTools/ThirdParty reads it
# at configure time and only defaults it when unset.
set(DILIGENT_DEAR_IMGUI_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui"
    CACHE PATH "imgui source directory" FORCE)

# --- offline configure -------------------------------------------------------
#
# DiligentEngine pulls two dependencies over the network at *configure* time via
# FetchContent, which would otherwise make a fresh build tree require internet
# and take whatever the pinned ref resolves to on the day. Both are vendored as
# submodules at exactly the refs Diligent declares, and pointed at here.
#
# Must come before add_subdirectory(third_party/DiligentEngine), and must be
# FORCE: FetchContent_Populate declares FETCHCONTENT_SOURCE_DIR_<NAME> as a cache
# variable *without* FORCE deep inside that subdirectory, so ours has to already
# be in the cache to win.
#
# The <NAME> suffix is the name from FetchContent_Declare, uppercased -- note the
# hyphen survives in ABSEIL-CPP. A wrong name here fails silently and simply
# fetches anyway, so these are taken from the generated CMakeCache, not guessed.
set(FETCHCONTENT_SOURCE_DIR_ENTT
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/entt"
    CACHE PATH "Vendored EnTT (DiligentFX)" FORCE)
set(FETCHCONTENT_SOURCE_DIR_ABSEIL-CPP
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/abseil-cpp"
    CACHE PATH "Vendored abseil-cpp (DiligentCore/ThirdParty)" FORCE)

# rapidjson is NOT currently fetched -- DiligentTools/AssetLoader gates it behind
# DILIGENT_USE_RAPIDJSON, which defaults OFF, and TinyGLTF falls back to the
# vendored nlohmann single-header instead. It is vendored and pointed at anyway
# so that turning that option on does not silently reintroduce a network fetch.
set(FETCHCONTENT_SOURCE_DIR_RAPIDJSON
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rapidjson"
    CACHE PATH "Vendored rapidjson (inactive: DILIGENT_USE_RAPIDJSON is OFF)" FORCE)

add_subdirectory(third_party/DiligentEngine)

# ImGui renamed the modifier keys in 1.89 (ImGuiKey_ModCtrl -> ImGuiMod_Ctrl)
# and kept obsolete aliases. Those aliases were still live in the 1.92.1 that
# DiligentEngine bundles but have been removed by 1.92.9, and Diligent's Linux
# and Emscripten ImGui impls still use the old spellings. The names are plain
# enum renames, so map them back with compile definitions instead of patching
# the engine tree. Only Diligent-Imgui needs them -- the Win32 impl comes from
# ImGui's own up-to-date backend.
#
# Drop this once DiligentEngine updates those three files.
if (TARGET Diligent-Imgui)
    target_compile_definitions(Diligent-Imgui PRIVATE
        ImGuiKey_ModCtrl=ImGuiMod_Ctrl
        ImGuiKey_ModShift=ImGuiMod_Shift
        ImGuiKey_ModAlt=ImGuiMod_Alt
        ImGuiKey_ModSuper=ImGuiMod_Super
    )
endif()

# --- additional third-party libraries ----------------------------------------
#
# Dear ImGui and EnTT are deliberately absent here: DiligentEngine already
# supplies both, and adding a second copy would collide on the target names.
#   ImGui  1.92.1 -- DiligentTools/ThirdParty/imgui, wrapped by Diligent-Imgui,
#                    which is what to link (it carries the renderer backends).
#   EnTT   3.16.0 -- fetched by DiligentFX; link EnTT::EnTT.
# To pin EnTT to a vendored checkout, set FETCHCONTENT_SOURCE_DIR_ENTT rather
# than adding it here.

# enkiTS -- task scheduler. Target: enkiTS
set(ENKITS_BUILD_EXAMPLES    OFF CACHE BOOL "" FORCE)
set(ENKITS_BUILD_SHARED      OFF CACHE BOOL "" FORCE)
set(ENKITS_INSTALL           OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/enkiTS)

# meshoptimizer -- mesh simplification/optimisation. Target: meshoptimizer
set(MESHOPT_BUILD_DEMO       OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_GLTFPACK   OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(MESHOPT_INSTALL          OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/meshoptimizer)

# ozz-animation -- runtime skeletal animation.
# Targets: ozz_base, ozz_animation, ozz_geometry (and ozz_animation_offline).
# Everything else is off: the samples need GLFW/OpenGL and the FBX pipeline
# needs the proprietary FBX SDK, neither of which survives a cross-compile.
set(ozz_build_tools          OFF CACHE BOOL "" FORCE)
set(ozz_build_fbx            OFF CACHE BOOL "" FORCE)
set(ozz_build_gltf           OFF CACHE BOOL "" FORCE)
set(ozz_build_samples        OFF CACHE BOOL "" FORCE)
set(ozz_build_howtos         OFF CACHE BOOL "" FORCE)
set(ozz_build_tests          OFF CACHE BOOL "" FORCE)
set(ozz_build_data           OFF CACHE BOOL "" FORCE)
# Without this, library files get a per-config suffix (ozz_base_r.a).
set(ozz_build_postfix        OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/ozz-animation)

# The engine library, and the four-artifact layout it anchors (T0013).
#
# engine/  a SHARED library -- the editor, the runtime and the gameplay module
#          all link this one copy, so engine state exists once per process (D12)
# samples/ the engine's own sample gameplay module -- a MODULE library loaded at
#          runtime and reloadable by T0048. NOT "the game": HollowPoint powers
#          several games, and a game is a separate project built against an
#          installed engine. This one exists so the module boundary is exercised
#          by the engine's own CI rather than only by whichever game is open
# apps/*   consumers, globbed below
#
# engine/ must come before apps/ and game/, which link it.
add_subdirectory(engine)
add_subdirectory(samples/sandbox)

# Apps are globbed, so adding one needs no edit here. They deliberately do NOT
# use Diligent's add_sample_app(): that is DiligentSamples' framework, and the
# app shell comes from DiligentTools/NativeApp in T0015.
file(GLOB _hp_apps LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/apps/*")
foreach(_app IN LISTS _hp_apps)
    if (IS_DIRECTORY "${_app}" AND EXISTS "${_app}/CMakeLists.txt")
        add_subdirectory("${_app}")
    endif()
endforeach()

# Test suites (T0012). Added unconditionally: the bucket executables are small
# next to ~1100 engine targets, and `zig build test` builds only the bucket it
# needs by name, so a normal build is not slowed by tests it will not run.
add_subdirectory(tests)
