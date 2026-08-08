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

# --- debug information (T0128.6) ---------------------------------------------
#
# **`zig c++` emits full DWARF by default.** Plain clang emits none, and nothing
# in this repository or in Diligent asks for `-g` -- a census of every
# translation unit in both `compile_commands.json` (1416 Linux, 1410 Windows)
# found not one passing it. So the default was costing:
#
#   libhp_engine.so   203.0 MiB, of which 92% debug info (16.7 MiB stripped)
#   libhp_engine.pdb  131.4 MiB on Windows, beside a 16.5 MiB DLL
#   dist              1.4 GB staged on Linux, 92.2% debug info and symtab
#   build tree        ~4.3 GB
#
# and about 16% of compile time on top. Our own objects were 54.5% debug info by
# size, so this was never only a third-party problem.
#
# **`-g0` rather than stripping at link, and the difference is `.symtab`.** Under
# LLD, `-Wl,--strip-debug` also removes the symbol table -- it produces a
# byte-identical file to `--strip-all` -- and with `CXX_VISIBILITY_PRESET hidden`
# the remaining `.dynsym` is ~133 KB, so stripped backtraces name almost nothing.
# `-g0` keeps `.symtab`, so a crash still reports function names, and it shrinks
# the *objects* too rather than only the final image.
#
# **What this gives up**, stated plainly: full source-level symbolication.
# Turning this option ON restores it, and that is the build to make when symbols
# are wanted -- for a release whose crashes must be symbolicated, keep that
# unstripped artifact as the symbol archive (Breakpad's `dump_syms` and
# `sentry-cli` both accept an unstripped binary directly).
#
# The alternative that looks obvious and is unavailable: splitting debug info
# into sidecar files. `zig objcopy` answers `error: unimplemented` for every ELF
# option including `--only-keep-debug` and `--add-gnu-debuglink` -- and truncates
# its output to zero bytes *before* exiting non-zero, so a naive staging check
# would ship an empty library. LLD has no `--separate-debug-file`. Requiring host
# `objcopy` would break D3, since a Windows host cross-compiling to Linux has
# none. See T0128 for the measurements behind each of these.
option(HP_DEBUG_INFO "Emit debug information (large: see T0128.6)" OFF)
if (NOT HP_DEBUG_INFO AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-g0)
endif()

# A build id, so a stripped or `-g0` binary can still be matched to its symbols
# by a symbol server or `debuginfod`. **Absent from every binary before this**,
# and it is the one piece here that cannot be added retroactively: a build that
# ships without one can never be symbolicated afterwards. ~180 bytes.
#
# ELF only. PE carries its own identity in the CodeView entry that already points
# at the `.pdb`, and `--build-id` is not a thing lld-link accepts.
if (NOT WIN32)
    add_link_options(-Wl,--build-id=sha1)
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

# Vulkan is the only backend (D29, T0144). Disabling OpenGL here removes its
# ~100 targets from every build rather than merely leaving them unlinked --
# nothing can quietly depend on a backend that does not build. Direct3D needs
# no equivalent: DiligentCore's own probes disable it under MinGW (D2).
set(DILIGENT_NO_OPENGL ON CACHE BOOL "" FORCE)

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

# --- draco (T0168.3) ---------------------------------------------------------
#
# Draco decompression for glTF: `KHR_draco_mesh_compression` is the default
# compression on a Sketchfab download, so without this the engine's answer to
# a large class of real models is "cannot open" -- the loader is fully wired
# upstream (`TINYGLTF_ENABLE_DRACO`) and merely gated on the target existing.
# Vendored at exactly the 1.5.7 ref DiligentTools' FetchContent declares, and
# redirected the same way as EnTT and abseil above, so offline configure holds.
# The submodule cost and the pin-bump cold CI build were weighed on T0168.3;
# the decision and the measurements are on that ticket.
set(FETCHCONTENT_SOURCE_DIR_DRACO
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/draco"
    CACHE PATH "Vendored draco (DiligentTools GLTF loader)" FORCE)
set(DILIGENT_ENABLE_DRACO ON CACHE BOOL "Draco decode in the glTF loader" FORCE)
# Draco's own extras stay off: the engine needs the decoder library, not
# command-line tools in every cross-compiled build tree.
set(DRACO_TESTS OFF CACHE BOOL "" FORCE)
set(DRACO_TOOLS OFF CACHE BOOL "" FORCE)

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

# PhysicsFS -- the virtual filesystem every asset read goes through (T0103, D13).
# Target: physfs-static
#
# Static and PRIVATE, like SDL. PhysicsFS has global init state, and the engine
# is a shared library that the editor, the runtime and every gameplay module all
# link (D12) -- so there must be exactly one copy of that state per process. A
# shared physfs beside every executable would be a second file to ship for no
# benefit; a static one linked into a second artifact would be the statics-per-
# artifact hazard that produced T0105.1 and T0127. One static copy, inside
# hp_engine, reached only through <hp/Vfs.hpp>.
#
# Everything else is off. The stdio test program is a console app nobody runs
# here, the docs need doxygen, and the install rules would put a library we
# consume as a target into the staging tree.
set(PHYSFS_BUILD_STATIC      ON  CACHE BOOL "" FORCE)
set(PHYSFS_BUILD_SHARED      OFF CACHE BOOL "" FORCE)
set(PHYSFS_BUILD_TEST        OFF CACHE BOOL "" FORCE)
set(PHYSFS_BUILD_DOCS        OFF CACHE BOOL "" FORCE)
set(PHYSFS_DISABLE_INSTALL   ON  CACHE BOOL "" FORCE)
# 103.6: ZIP is the shipping format and the only archive reader we keep. The
# rest are readers for other people's games -- Quake PAK, Descent HOG, Gothic
# VDF -- and each is attack surface and compile time for a format this engine
# will never open. Turning them off is reversible in one line if that is ever
# wrong; shipping a parser for Resident Evil 3 archives is not a default anyone
# chose.
set(PHYSFS_ARCHIVE_ZIP       ON  CACHE BOOL "" FORCE)
set(PHYSFS_ARCHIVE_7Z        OFF CACHE BOOL "" FORCE)
set(PHYSFS_ARCHIVE_GRP       OFF CACHE BOOL "" FORCE)
set(PHYSFS_ARCHIVE_WAD       OFF CACHE BOOL "" FORCE)
set(PHYSFS_ARCHIVE_CSM       OFF CACHE BOOL "" FORCE)
set(PHYSFS_ARCHIVE_HOG       OFF CACHE BOOL "" FORCE)
set(PHYSFS_ARCHIVE_MVL       OFF CACHE BOOL "" FORCE)
set(PHYSFS_ARCHIVE_QPAK      OFF CACHE BOOL "" FORCE)
set(PHYSFS_ARCHIVE_ROFS      OFF CACHE BOOL "" FORCE)
set(PHYSFS_ARCHIVE_SLB       OFF CACHE BOOL "" FORCE)
set(PHYSFS_ARCHIVE_ISO9660   OFF CACHE BOOL "" FORCE)
set(PHYSFS_ARCHIVE_VDF       OFF CACHE BOOL "" FORCE)
set(PHYSFS_ARCHIVE_LECARCHIVES OFF CACHE BOOL "" FORCE)
set(PHYSFS_ARCHIVE_POD       OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/physfs)

# Undo PhysicsFS's global RPATH sabotage.
#
# Its CMakeLists does `set(CMAKE_SKIP_RPATH ON CACHE BOOL "" FORCE)` under any
# GCC or Clang -- not scoped to its own targets, not optional, and FORCE, so it
# lands in *our* cache and strips RPATH from **every target in this build**. It
# is reasonable for a library that expects to be installed to a system prefix
# and wrong for a subdirectory dependency.
#
# The symptom is worth writing down because it looks nothing like a CMake
# option: every executable links fine and then dies at startup with
# `libhp_engine.so: cannot open shared object file`, which reads as a missing
# file rather than a missing RPATH -- and the file is right there beside the
# binary. Diligent's backends fail the same way one step later. It survives a
# clean rebuild, because the cause is in the source, not the tree.
#
# Reset immediately after the subdirectory so nothing else inherits it. Both
# the cache entry and the directory-scope variable, because the FORCE wrote the
# cache and a plain `set` here would be shadowed by it.
set(CMAKE_SKIP_RPATH OFF CACHE BOOL "Use RPATH" FORCE)
set(CMAKE_SKIP_RPATH OFF)

# rapidyaml -- YAML parsing and emitting (T0020). Target: ryml
#
# YAML is the source of truth for every serialized thing here; the binary form
# is a derived cache (T0020's model). rapidyaml over yaml-cpp for speed and
# maintenance, with <hp/Serialization.hpp> as the seam that makes that
# reversible -- no ryml:: or c4:: type appears in an engine header.
#
# It brings c4core, which brings three more submodules of its own (cmake,
# debugbreak, fast_float). That is more transitive vendoring than anything else
# here, and it is the price of the parser; all four are pinned by rapidyaml's
# own tags rather than floating.
set(RYML_BUILD_TESTS         OFF CACHE BOOL "" FORCE)
set(RYML_BUILD_BENCHMARKS    OFF CACHE BOOL "" FORCE)
set(RYML_BUILD_TOOLS         OFF CACHE BOOL "" FORCE)
set(RYML_BUILD_API           OFF CACHE BOOL "" FORCE)
set(RYML_INSTALL             OFF CACHE BOOL "" FORCE)
set(C4CORE_INSTALL           OFF CACHE BOOL "" FORCE)
set(C4CORE_BUILD_TESTS       OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/rapidyaml)

# c4core cannot detect MinGW when the compiler is clang, and 20.1's "expected
# fine" was wrong because of it.
#
# c4core/src/c4/compiler.hpp nests the MinGW test *inside* its GCC branch:
#
#     #elif defined(__clang__)          <- zig cc lands here
#     #    define C4_CLANG
#     #elif defined(__GNUC__)
#     #    ifdef __MINGW32__
#     #        define C4_MINGW          <- never reached
#
# zig cc targeting x86_64-windows-gnu defines __clang__, __MINGW32__ and
# __MINGW64__ all at once -- verified with `zig cc -target x86_64-windows-gnu
# -dM -E`. It therefore takes the clang branch, C4_MINGW is never defined, and
# rapidyaml's common.hpp falls through to `#include <alloca.h>`, which MinGW
# does not have -- it puts alloca in <malloc.h>. Every translation unit that
# touches ryml fails to compile, on Windows only.
#
# Defining it here rather than patching the tree: the condition is genuinely
# true (this *is* MinGW), the symbol has exactly one other use in the whole of
# rapidyaml and c4core -- the malloc.h/alloca.h switch it exists for -- and a
# vendored tree that is never edited updates cleanly. PUBLIC so our own
# translation units that include ryml headers agree with the library about it;
# disagreeing would be an ODR problem rather than a compile error.
if (WIN32)
    target_compile_definitions(ryml PUBLIC C4_MINGW)
endif()

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

# --- slang (T0142, D28) ------------------------------------------------------
#
# The engine's shader language. `.slang` sources are compiled to SPIR-V at
# pipeline-build time by the slang library, which the engine loads at RUN time
# (dlopen/LoadLibrary) -- there is deliberately no link edge, so a shipped game
# that reads cooked shaders never touches it and the cross-compile never has to
# link an MSVC-built import library.
#
# What the build needs from the pinned package is therefore only:
#   * the headers, to compile the engine's slang-driving code against;
#   * the target platform's runtime library, staged beside every executable
#     that renders (tests and apps), because that is the one place both
#     platforms' loaders look without help -- Windows has no RPATH, and under
#     wine an absolute POSIX path is meaningless.
#
# Keyed by the TARGET platform: a Linux host cross-compiling the Windows suite
# stages slang-compiler.dll for wine, from the windows-x86_64 package.
# bootstrap installs both packages on every host for exactly this reason.
set(HP_SLANG_VERSION "2026.14.1")
if (WIN32)
    set(HP_SLANG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.harness/slang/windows-x86_64/${HP_SLANG_VERSION}")
    # slang-compiler.dll is the real library; slang.dll is a forwarder shim
    # over it, so loading the real one directly saves staging both.
    # slang-glslang.dll is loaded on demand by slang itself (SPIR-V validation
    # and optimisation passes) from its own directory; staged so the passes
    # that want it do not silently degrade.
    set(HP_SLANG_LIBRARY_NAME "slang-compiler.dll")
    set(HP_SLANG_RUNTIME_LIBRARIES
        "${HP_SLANG_ROOT}/bin/slang-compiler.dll"
        "${HP_SLANG_ROOT}/bin/slang-glslang.dll")
else()
    set(HP_SLANG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.harness/slang/linux-x86_64/${HP_SLANG_VERSION}")
    set(HP_SLANG_LIBRARY_NAME "libslang-compiler.so")
    # The first entry is a symlink in the package; copy_if_different
    # dereferences it, so what lands beside the binaries is the real library
    # under the unversioned name the engine asks for.
    set(HP_SLANG_RUNTIME_LIBRARIES
        "${HP_SLANG_ROOT}/lib/libslang-compiler.so"
        "${HP_SLANG_ROOT}/lib/libslang-glslang-${HP_SLANG_VERSION}.so")
endif()
if (NOT EXISTS "${HP_SLANG_ROOT}/include/slang.h")
    message(FATAL_ERROR
            "slang ${HP_SLANG_VERSION} is not installed in .harness/ -- run ./bootstrap.sh "
            "(or .\\bootstrap.ps1 on Windows) once, then reconfigure. The build is offline "
            "afterwards (T0142).")
endif()

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
# The second sample, and the evidence for 157.1's question: a second gameplay
# module costs exactly this line plus its own CMakeLists.txt.
add_subdirectory(samples/rockcube)

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
