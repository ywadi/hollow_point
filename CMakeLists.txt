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

# Precompiled headers must not embed file timestamps. Clang's PCH validation
# hard-fails when a header's mtime differs from what the .pch recorded -- even
# with identical content -- and this build's outputs legitimately outlive any
# particular set of source mtimes: CI restores the build tree from cache and
# re-stamps sources with commit-derived times (tools/ci_restore_mtimes.py), so
# a restored .pch would otherwise refuse every consumer the moment anything
# recompiles. Run 30951493282 failed exactly that way on an SDL header that
# had not changed by a byte. Ninja already rebuilds a .pch when its inputs
# really change, which is the validation that matters; the timestamp check
# adds nothing here but a way to fail on correct trees (T0131).
#
# Compile-only (add_compile_options does not reach link lines), and set at the
# top so every subdirectory -- SDL, Diligent, glslang -- inherits it.
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options("SHELL:-Xclang -fno-pch-timestamp")
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

# --- SDL3: window, input and audio backend (T0015, D16) ----------------------
#
# D16 chose SDL3 over DiligentTools/NativeApp and over GLFW. Diligent attaches to
# a window created by anything -- LinuxNativeWindow takes an X11 WindowId and a
# Display*, Win32 takes an HWND -- so using SDL costs nothing in engine
# integration, and it brings gamepad support with rumble, clipboard, DPI and
# monitor enumeration that NativeApp has none of.
#
# Static, into the engine shared library. One fewer file to ship beside every
# executable, and the engine is already the single place that owns platform
# state (D12).
#
# Deliberately minimal: no tests, no examples, no install rules, nothing
# vendored into our install tree. Same treatment as every other third-party
# project here -- consumed, never modified.
set(SDL_SHARED       OFF CACHE BOOL "" FORCE)
set(SDL_STATIC       ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS        OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL      OFF CACHE BOOL "" FORCE)

# Every backend is chosen explicitly, because SDL's detection scans the *host*.
#
# This is not caution -- it is a bug that already happened. Left to auto-detect,
# SDL's pkg-config probes found the build machine's Wayland, KMSDRM, PulseAudio,
# sndio and EGL, enabled them, and the link then failed on ten libraries the
# vendored sysroot does not carry. That is the hermetic-Linux property (D4)
# breaking silently: a build that succeeds only on a machine that happens to
# have the right -dev packages is exactly what the sysroot exists to prevent, and
# on a Windows host cross-compiling to Linux it could not work at all.
#
# So: on for what the sysroot backs, off for everything else. Turning one back
# on means vendoring its headers and stubs first (tools/mk_linux_sysroot.sh).
set(SDL_WAYLAND    OFF CACHE BOOL "" FORCE)  # T0119 owns this, with the sysroot work it needs
set(SDL_KMSDRM     OFF CACHE BOOL "" FORCE)
set(SDL_OPENGLES   OFF CACHE BOOL "" FORCE)  # EGL is not vendored; Diligent uses GLX on Linux
set(SDL_PULSEAUDIO OFF CACHE BOOL "" FORCE)
set(SDL_SNDIO      OFF CACHE BOOL "" FORCE)
set(SDL_PIPEWIRE   OFF CACHE BOOL "" FORCE)
set(SDL_JACK       OFF CACHE BOOL "" FORCE)
set(SDL_LIBUDEV    OFF CACHE BOOL "" FORCE)  # udev is not in the sysroot either (T0068 noted this)
set(SDL_DBUS       OFF CACHE BOOL "" FORCE)
set(SDL_IBUS       OFF CACHE BOOL "" FORCE)
set(SDL_LIBDECOR   OFF CACHE BOOL "" FORCE)
set(SDL_DIALOG     OFF CACHE BOOL "" FORCE)  # native file dialogs want GTK
set(SDL_FRIBIDI    OFF CACHE BOOL "" FORCE)  # bidirectional text -- T0117 owns text, not SDL
set(SDL_LIBTHAI    OFF CACHE BOOL "" FORCE)  # Thai word breaking, same reason
set(SDL_OPENVR     OFF CACHE BOOL "" FORCE)
set(SDL_OSS        OFF CACHE BOOL "" FORCE)
set(SDL_ALSA       OFF CACHE BOOL "" FORCE)  # no ALSA headers vendored; audio is T0052's problem
set(SDL_HIDAPI_LIBUSB OFF CACHE BOOL "" FORCE)
set(SDL_RPATH      OFF CACHE BOOL "" FORCE)  # the engine decides RPATH, not a dependency (T0013)

# X11 support is dlopen'd at run time rather than linked (SDL_X11_SHARED, on by
# default), which is why the vendored sysroot needs only X11 *headers* and not
# the five extension libraries GLFW would have required. That was D16's one
# recorded unknown; it is confirmed here.
add_subdirectory(third_party/SDL)

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

# Gameplay modules are declared through one function so the unload finalizer
# (T0105.1) and, later, the build-id stamp (T0104) cannot be omitted.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/HpGameplayModule.cmake")

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
