# The engine library (T0013).
#
# SHARED, not static, and that is the whole architecture rather than a build
# preference (D12). The editor, the runtime and every gameplay module link this
# one library, so engine state -- the logger, registries, asset pools -- exists
# exactly once per process. Linking it statically into both an executable and a
# gameplay module compiles and links perfectly well and then gives each side its
# own copy of every global, which is a runtime failure that looks like
# impossible bugs in gameplay code.
#
# Proven on both targets before this layout was committed; see
# tests/integration/module_boundary_test.cpp and T0095.

# --- configuration flags that the build id has to see ------------------------
#
# Declared *here*, above the build id, and that ordering is load-bearing.
#
# It used to be declared further down, next to the code it guards, which reads
# better and was wrong: the custom command below expands ${HP_PROFILING} at
# configure time, so on a fresh tree -- where nothing has put it in the cache
# yet -- it expanded to the empty string. `option()` then wrote OFF to the
# cache, and the *next* configure expanded it to "OFF" instead. The id therefore
# changed on a no-op reconfigure, with no source, no header and no argument
# different: measured 00947a49dbbb42a3 -> b88f8277d7bec25f (T0105.2).
#
# The consequence is the guard misfiring rather than failing to fire: every
# module built before that reconfigure is refused by an engine built after it,
# for a difference that does not exist. T0104's header warns about a stale id
# claiming compatibility it does not have; this is the same hazard from the
# other side, and it is the more confusing one, because the diagnostic is
# correct about the ids and wrong about the world.
#
# **Any flag that feeds the id must be declared before the command that reads
# it.** That is the rule this comment exists to keep.
option(HP_PROFILING "Compile in profiling instrumentation (Tracy, T0029)" OFF)

# --- build id (T0104) --------------------------------------------------------
#
# Generated at BUILD time with the public headers as declared inputs, so ninja
# regenerates it when a header changes. A configure-time hash would go stale
# silently -- CMake re-runs configure for a CMakeLists.txt change, not a header
# change -- and a stale id claims compatibility it does not have, which is the
# one failure this whole ticket exists to prevent.
set(HP_BUILD_ID_HEADER "${CMAKE_BINARY_DIR}/generated/hp/BuildId.h")

file(GLOB _hp_public_headers CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/include/hp/*.hpp")

add_custom_command(
    OUTPUT "${HP_BUILD_ID_HEADER}"
    COMMAND "${CMAKE_COMMAND}"
            -DHP_ID_INCLUDE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/include
            -DHP_ID_TARGET=${HP_TARGET}
            -DHP_ID_CONFIG=${CMAKE_BUILD_TYPE}
            -DHP_ID_PROFILING=${HP_PROFILING}
            -DHP_ID_OUTPUT=${HP_BUILD_ID_HEADER}
            -P "${CMAKE_SOURCE_DIR}/cmake/hp_build_id.cmake"
    DEPENDS ${_hp_public_headers} "${CMAKE_SOURCE_DIR}/cmake/hp_build_id.cmake"
    COMMENT "Computing engine build id (T0104)"
    VERBATIM)

add_custom_target(hp_build_id DEPENDS "${HP_BUILD_ID_HEADER}")

# --- embedded shaders (T0141, D26) -------------------------------------------
#
# The engine's own shaders are compiled into the binary rather than read from
# disk -- see cmake/hp_embed_shaders.cmake for why, and why Diligent's own
# generator is not reused. Regenerated when a shader changes, because the
# sources are declared inputs here.
set(HP_ENGINE_SHADERS_HEADER "${CMAKE_BINARY_DIR}/generated/hp/EngineShaders.h")

# **Wider than what is embedded, deliberately** (T0142.13). Only `.slang` is
# embedded; the HLSL extensions stay in this dependency glob so that adding one
# re-runs the embed step, which then *fails* naming the decision. Narrowing this
# to `.slang` would make a stray `.psh` invisible to the build instead of
# refused by it.
file(GLOB _hp_shader_sources CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vsh"
     "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.psh"
     "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.fxh"
     "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.slang")

# Embedded alongside our own shaders (T0142): Diligent *prepends* this file to
# HLSL sources itself, but slang resolves it as an ordinary `#include`, so the
# engine factory must serve it. Read from the pinned submodule at build time --
# same bytes, regenerated when the submodule moves, so it cannot drift. The
# file carries its own include guard, so double inclusion on Diligent's paths
# is harmless.
set(_hp_shader_extra_files
    "${CMAKE_SOURCE_DIR}/third_party/DiligentEngine/DiligentCore/Graphics/ShaderTools/include/HLSLDefinitions.fxh")

add_custom_command(
    OUTPUT "${HP_ENGINE_SHADERS_HEADER}"
    COMMAND "${CMAKE_COMMAND}"
            -DHP_SHADER_DIR=${CMAKE_CURRENT_SOURCE_DIR}/shaders
            -DHP_SHADER_OUTPUT=${HP_ENGINE_SHADERS_HEADER}
            "-DHP_SHADER_EXTRA_FILES=${_hp_shader_extra_files}"
            -P "${CMAKE_SOURCE_DIR}/cmake/hp_embed_shaders.cmake"
    DEPENDS ${_hp_shader_sources} ${_hp_shader_extra_files}
            "${CMAKE_SOURCE_DIR}/cmake/hp_embed_shaders.cmake"
    COMMENT "Embedding engine shaders (T0141)"
    VERBATIM)

add_custom_target(hp_engine_shaders DEPENDS "${HP_ENGINE_SHADERS_HEADER}")

add_library(hp_engine SHARED
    src/Module.cpp
    src/ModuleHost.cpp
    src/Reflect.cpp
    src/Application.cpp
    src/AssetImport.cpp
    src/Assets.cpp
    src/Camera.cpp
    src/Cook.cpp
    src/CameraSystem.cpp
    src/DrawSubmission.cpp
    src/Engine.cpp
    src/Guid.cpp
    src/Input.cpp
    src/InputBindings.cpp
    src/Paths.cpp
    src/FrameTargets.cpp
    src/Render.cpp
    src/RenderStack.cpp
    src/Scene.cpp
    src/ShaderCook.cpp
    src/ShaderSources.cpp
    src/SlangCompiler.cpp
    src/SurfacePipeline.cpp
    src/SceneSerialize.cpp
    src/SceneRenderer.cpp
    src/SceneRenderLayer.cpp
    src/SceneView.cpp
    src/Serialize.cpp
    src/Settings.cpp
    src/Light.cpp
    src/Material.cpp
    src/Layer.cpp
    src/Log.cpp
    src/Time.cpp
    src/Vfs.cpp
    src/Yaml.cpp
    src/Window.cpp
)
add_library(hp::engine ALIAS hp_engine)

target_include_directories(hp_engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    # PUBLIC because gameplay modules include <hp/BuildId.h> to stamp
    # themselves, and they must get the *same* generated header as the engine --
    # that identity is what makes a mismatch mean something.
    "${CMAKE_BINARY_DIR}/generated")

# The generated header is an input to compiling the engine, so say so rather
# than relying on target ordering.
add_dependencies(hp_engine hp_build_id hp_engine_shaders)

# PRIVATE: consumers must see the *import* side of HP_API, never the export
# side. Making this PUBLIC would have every app and module claim to define the
# engine's symbols, which fails at link on Windows and silently duplicates them
# elsewhere.
target_compile_definitions(hp_engine PRIVATE HP_ENGINE_BUILD)

# C++20 for our own code while third-party stays at the root's CMAKE_CXX_STANDARD
# 17 (see documentation/06-engine-conventions.md). Verified working on both
# targets rather than assumed.
target_compile_features(hp_engine PUBLIC cxx_std_20)

# Hidden by default so the exported surface is exactly what HP_API marks. This
# is also what keeps RTTI across the module boundary honest: types that cross it
# are annotated deliberately rather than exported by accident.
set_target_properties(hp_engine PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

# --- logging (T0054) ---------------------------------------------------------
#
# The compile-time floor. Levels below this are removed by the preprocessor
# rather than filtered at run time, so verbose logging is absent from a shipped
# build instead of merely skipped -- and, as with the profiling macros, its
# arguments are not evaluated.
#
# PUBLIC for the same reason HP_PROFILING is: the macros live in a public header
# and consumers must compile against the same floor as the engine.
set(HP_LOG_MIN_LEVEL "0" CACHE STRING
    "Compile-time minimum log level: 0=trace 1=debug 2=info 3=warning (error/fatal always on)")
target_compile_definitions(hp_engine PUBLIC HP_LOG_MIN_LEVEL=${HP_LOG_MIN_LEVEL})

# --- profiling (T0019) -------------------------------------------------------
#
# One switch, off by default, and it is a *compile-time* switch: off means the
# instrumentation does not exist rather than being skipped at run time. That is
# what preserves shipped-build performance, and it is why the macros must not so
# much as name their arguments when disabled.
#
# PUBLIC, not PRIVATE, and this is the part that matters. The macros live in a
# public header, so every consumer -- the editor, the runtime, every gameplay
# module -- must compile against the *same* setting as the engine. A module
# built with profiling on against an engine built without it is exactly the
# mismatch T0104 exists to refuse at load, because enabling profiling changes
# the engine's symbol surface. Making this PRIVATE would let that happen
# silently and is the single easiest way to get this wrong.
#
# The option itself is declared at the top of this file, above the build id that
# has to hash it -- see the comment there for why moving it back down breaks the
# id.
if (HP_PROFILING)
    target_compile_definitions(hp_engine PUBLIC HP_PROFILING)
    message(STATUS "hp_engine: profiling instrumentation ENABLED")
endif()

# SDL3, PRIVATE and static (D16). PRIVATE because no public engine header names
# an SDL type -- <hp/Window.hpp> exposes native handles as void*/uint64_t
# precisely so consumers never see SDL3/SDL.h, and gameplay cannot reach SDL
# behind the engine's back. Static so there is one fewer file beside every
# executable.
# PUBLIC, not PRIVATE. Gameplay modules register their own types into the
# engine's meta context, so they compile against entt's headers too -- and they
# must get the *same* headers, because the context's layout is shared across the
# boundary. D12's lockstep is what makes that safe.
target_link_libraries(hp_engine PUBLIC EnTT::EnTT)

target_link_libraries(hp_engine PRIVATE SDL3::SDL3-static)

# PhysicsFS, PRIVATE and static (T0103, D13). Same argument as SDL: no public
# engine header names a PHYSFS_ type -- <hp/Vfs.hpp> is deliberately shaped in
# std:: types so consumers never see physfs.h, which is what keeps the library
# replaceable behind that seam and stops PHYSFS_* calls spreading through the
# codebase.
#
# It matters more here than for SDL, because PhysicsFS keeps *global init
# state*. The engine is a shared library that the editor, the runtime and every
# gameplay module all link (D12), so exactly one copy of that state must exist
# per process. Linking it into a second artifact would give that artifact its
# own uninitialised filesystem -- the statics-per-artifact hazard behind T0105.1
# and T0127, in a form where the symptom is "the file is missing" rather than a
# crash.
target_link_libraries(hp_engine PRIVATE physfs-static)

# rapidyaml, PRIVATE and static (T0020). Same seam argument as SDL and
# PhysicsFS: <hp/Yaml.hpp> names no ryml:: or c4:: type, so the parser is
# replaceable and its span-lifetime rules never reach a caller.
target_link_libraries(hp_engine PRIVATE ryml)

# Diligent, PRIVATE (T0025).
#
# 13.3 asked for PUBLIC vs PRIVATE to be chosen deliberately, and this is that
# choice being made: **PRIVATE unless a public engine header names a Diligent
# type**, and none does. `hp/Render.hpp` is a pimpl precisely so that stays
# true, and `src/Render.cpp` is the only translation unit in the engine that
# includes Diligent at all.
#
# PUBLIC here is not a build error, which is what makes it dangerous: it hands
# Diligent's include path to every app and every gameplay module. D6 records
# that DiligentFX links Diligent-Imgui PUBLIC, so a careless PUBLIC would give
# ImGui to every module whether it wants it or not.
#
# The `-shared` variants rather than `-static`: they are the .so/.dll that
# `dist` already stages, and linking statically would pull both backends into
# libhp_engine, which removes any possibility of one being absent.
#
# The link is per-platform, and that is forced rather than chosen.
#
# On Windows, linking the engine DLLs' *import* libraries fails outright:
# GraphicsEngineVk_64r.dll exports `atexit`, zig's MinGW `dllcrt2.obj` defines
# it, and lld-link refuses the duplicate. Diligent's own answer is explicit
# loading -- link only the interface (headers, no import library) and
# LoadLibrary the DLL at run time. `LoadEngineDll.h` includes <Windows.h>, so
# that path exists on Windows and nowhere else, which is exactly why the split
# is here rather than being avoidable.
#
# On Linux there is no such collision, so the shared targets link directly and
# `GetEngineFactoryVk()` resolves at load time. Measured working on an RTX 2080.

# The one PUBLIC piece of Diligent: the math headers (T0021, D21).
#
# `hp/Math.hpp` is a public header and it includes `BasicMath.hpp`, so every
# consumer needs to be able to find it. That is a deliberate amendment to the
# rule directly above, argued in D21 and in `hp/Math.hpp`'s own comment: the
# conventions document requires gameplay to use Diligent's math types, and
# `BasicMath.hpp` drags in 74 headers including the RHI because Diligent hashes
# its descriptors in the same file it hashes vectors.
#
# **Include directories only, never the library.** `Diligent-Common` is a STATIC
# library; linking it PUBLIC would compile its code into every gameplay module,
# and a second copy of anything stateful is exactly the failure T0105.1 and
# T0127 were. Using the target's INTERFACE_INCLUDE_DIRECTORIES rather than a
# hard-coded path keeps this correct if Diligent moves the directory, without
# creating a link edge.
#
# `Diligent-PublicBuildSettings` *is* linked PUBLIC, and safely: it is an
# INTERFACE target carrying only `PLATFORM_LINUX=1` / `PLATFORM_WIN32=1`. Those
# are not optional — `PlatformDefinitions.h` is a hard `#error` without one, so
# omitting it turns every gameplay TU that includes a transform into a compile
# failure with a misleading message.
#
# Measured: `Common/interface` alone is sufficient. Everything below it resolves
# through relative includes, so this is one directory rather than the six the
# chain actually reaches into.
target_link_libraries(hp_engine PUBLIC Diligent-PublicBuildSettings)
target_include_directories(hp_engine PUBLIC
    $<TARGET_PROPERTY:Diligent-Common,INTERFACE_INCLUDE_DIRECTORIES>)

if (WIN32)
    # Static on Windows, and both alternatives were tried and measured.
    #
    # Linking the engine DLLs' import libraries fails at link time:
    # GraphicsEngineVk_64r.dll exports `atexit`, zig's MinGW dllcrt2.obj defines
    # it, and lld-link refuses the duplicate.
    #
    # Diligent's own explicit-load path fails at *run* time, which is worse
    # because it compiles. The DLL loads, and then GetProcAddress returns null:
    # under MinGW the factory is exported as `Diligent_GetEngineFactoryVk`, while
    # LoadEngineDll asks for `GetEngineFactoryVk`. Measured with a probe --
    # "loaded, GetEngineFactoryVk=0000000000000000".
    #
    # Static linking sidesteps both: no import library, so no atexit collision,
    # and no name lookup, so no prefix mismatch. The cost is that the backend is
    # baked into libhp_engine.dll rather than being a separate file.
    #
    # Vulkan only (D29): the OpenGL backend is removed, and the root
    # CMakeLists.txt sets DILIGENT_NO_OPENGL so its targets do not even build.
    target_link_libraries(hp_engine PRIVATE
        Diligent-GraphicsEngineVk-static
        Diligent-Common)
else()
    target_link_libraries(hp_engine PRIVATE
        Diligent-GraphicsEngineVk-shared
        Diligent-Common)
endif()

# Diligent's asset parsers (T0023.3). STATIC and PRIVATE, so exactly one copy
# lives inside libhp_engine and no consumer sees their headers.
#
# **These do the file parsing and we do none of it**, which is 23.3's
# instruction. They are reached only from `src/AssetImport.cpp`, which feeds
# them bytes from the VFS rather than paths -- `CreateTextureLoaderFromMemory`
# for images, and glTF's read-whole-file callback for models -- so D13's rule
# that every read goes through `hp::Vfs` survives reusing them.
target_link_libraries(hp_engine PRIVATE
    Diligent-TextureLoader
    Diligent-AssetLoader)

# DiligentFX, for `GLTF_PBR_Renderer` (T0028, decided on T0134). STATIC and
# PRIVATE, so it lives inside libhp_engine and no consumer sees its headers.
#
# **This is what makes the renderer swappable, and it is enforced by the linker
# rather than by review.** The rule above -- PRIVATE unless a public engine
# header names a Diligent type -- means the renderer appears in exactly one
# translation unit, behind a pimpl, so replacing it touches that file and
# nothing else. That is why no `IRenderBackend` interface was written: **D22**
# says an abstraction over an abstraction buys nothing until there is a concrete
# second target, and one implementation behind an interface is that abstraction.
#
# **It drags in Diligent-Imgui**, which DiligentFX links PUBLIC and calls from
# ScreenSpaceReflection, Bloom, ToneMapping and friends. That is a cost already
# accounted for: **D6** records it explicitly, and corrects the assumption it
# would otherwise break -- ImGui is linked into the *runtime* binary too, not
# only the editor. So this spends a decision already made rather than a new one.
target_link_libraries(hp_engine PRIVATE DiligentFX)

# --- shader sources (T0141, D26) ---------------------------------------------
#
# `Diligent-GraphicsTools` supplies `CreateMemoryShaderSourceFactory` and
# `CreateCompoundShaderSourceFactory`, which are what let an engine shader
# `#include` a DiligentFX public header without either side being modified. It
# is a separate target from `DiligentFX` and from the graphics engines, so
# linking it is deliberate rather than inherited -- and it is also where
# `RenderStateCache` lives, which T0141.3 will want for PSO caching.
target_link_libraries(hp_engine PRIVATE Diligent-GraphicsTools)

# DiligentFX exports its root as PUBLIC, which reaches `PBR/interface` and the
# shader tree but **not** `Utilities/interface`, where
# `DiligentFXShaderSourceStreamFactory` lives. Named explicitly rather than
# reaching through the root with a longer relative path, so a move upstream is a
# build error here rather than a silently wrong include.
target_include_directories(hp_engine PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/DiligentEngine/DiligentFX/Utilities/interface")

# --- slang (T0142, D28) ------------------------------------------------------
#
# Headers only, PRIVATE, SYSTEM. The library itself is loaded at RUN time by
# `src/SlangCompiler.cpp` -- deliberately no link edge, so nothing a consumer
# links inherits slang, the MinGW cross-link never sees an MSVC import library,
# and a shipped game that reads cooked shaders (142.7) simply never loads it.
# The name is a compile definition because it differs per target and the code
# must ask for exactly the file the build staged.
target_include_directories(hp_engine SYSTEM PRIVATE "${HP_SLANG_ROOT}/include")
target_compile_definitions(hp_engine PRIVATE
    HP_SLANG_LIBRARY_NAME="${HP_SLANG_LIBRARY_NAME}"
    # Baked into the SPIR-V cache's file name (T0141.3): bytecode compiled by
    # one slang is not evidence about another, so a pin bump must read as a
    # cold cache rather than as stale hits.
    HP_SLANG_VERSION="${HP_SLANG_VERSION}")
