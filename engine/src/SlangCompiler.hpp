// Compiles the engine's `.slang` shaders to SPIR-V at pipeline-build time
// (T0142, D28).
//
// **Internal to the engine, like `SurfacePipeline`.** It names a Diligent type
// in its interface, and more importantly it is the seam D28 draws: Slang is the
// authoring language, HLSL/SPIR-V is the interchange, and *Diligent never
// learns Slang exists* -- it receives bytecode. Nothing here may leak into a
// public header, because a shipped game reads cooked shaders (142.7) and must
// not inherit so much as a slang typename.
//
// ## Why the library is loaded at run time rather than linked
//
// Three separate problems disappear at once:
//
//   * **The shipped-runtime rule.** D28's boundary table says a shipped game
//     links no Slang. The engine is one shared library serving the editor and
//     the runtime alike (D12), so a link edge here would be inherited by every
//     consumer forever. A `dlopen` happens only when a `.slang` shader actually
//     needs compiling, which the cooked path never does.
//   * **The cross-compile.** The Windows package is MSVC-built; its import
//     library is not something the MinGW link should be fed (see the atexit
//     collision that already rules out Diligent's own import libraries). The
//     slang API is COM-shaped -- one C entry point, then vtables -- precisely
//     so that the compiler ABI does not matter.
//   * **Distribution.** The library sits beside the executables (staged by the
//     build, T0142.1); a machine without it degrades to "shader does not
//     compile", which is loud, logged, and already the failure mode T0141
//     designed for -- never a crash.
#pragma once

#include <hp/ShaderParams.hpp>
#include <hp/ShaderSources.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Diligent {
struct IShaderSourceInputStreamFactory;
} // namespace Diligent

namespace hp {

/// One preprocessor definition handed to the compile. Mirrors
/// `Diligent::ShaderMacro` so `SurfacePipeline` can forward `DefineMacros`'
/// output without this header including Diligent's.
struct SlangMacro {
    const char* name;
    const char* definition;
};

/// One resource-typed global the compiled program declares, as slang sees it
/// (T0161.3).
///
/// **This exists for the one fact only slang can report: the resource's
/// shape.** Diligent's `ShaderResourceDesc` reflects name, type and array
/// size out of the SPIR-V but not dimension, so a `Texture2D` declared where
/// the engine binds its one-slice `Texture2DArray` views would sail through
/// signature creation and surface as a raw Vulkan validation error in a
/// cooked build. Dev-time reflection sees the shape here, and
/// `buildModuleSignatureDesc` refuses the mismatch by name.
///
/// The list covers **every** resource-typed global in the program, engine
/// resources included — this file cannot know which names are the module's,
/// and the consumer already subtracts the engine's set for the signature walk.
struct SlangReflectedResource {
    /// The global's declared name.
    std::string name;

    /// Whether the declared type is a texture at all.
    bool isTexture = false;

    /// Whether it is specifically `Texture2DArray` — the one sampled shape
    /// the engine's binder can feed. Meaningful only when `isTexture`.
    bool isTexture2DArray = false;
};

/// What a compile may be asked to report about the module it compiled (T0160.2).
///
/// **Filled from the compile that already happens, never from a second one.**
/// The alternative was reflecting a module on its own, and that is not a thing
/// that exists: a module names `IHpMaterial`, `VSOutput` and — since D27's
/// amendment — anything else `HpSurface.slang` can see, so a standalone
/// translation unit containing it does not type-check, and slang emits no
/// reflection at all for a failed compile (measured on the pinned
/// `slangc 2026.14.1`: `-reflection-json` writes no file). Reflecting the real
/// compile is therefore not a compromise, it is the only place the module is a
/// well-formed program.
struct SlangReflectionRequest {
    /// Receives the module's declared parameters. Left empty when the shader
    /// declares none, which is the standard material's case.
    ///
    /// The layout's `textures` list is **not** filled here — it comes from
    /// the same walk that builds the module signature
    /// (`buildModuleSignatureDesc`), so the list a `.hpmat` binds against and
    /// the signature the SRB binds into can never disagree, on the dev path
    /// or the cooked one.
    ShaderParamLayout* layout = nullptr;

    /// Receives every resource-typed global with its shape, or null when the
    /// caller does not need the dimension check.
    std::vector<SlangReflectedResource>* resources = nullptr;
};

/// Bump when the way slang is *driven* changes -- target, matrix layout, the
/// prelude arrangement -- anything that alters the output without altering any
/// hashed input.
///
/// It lives beside the code it describes and is consumed by `ShaderStore.hpp`,
/// which folds it into every variant key. Forgetting to bump it means a cooked
/// archive or a developer cache serves bytecode from a compile that would no
/// longer be made the same way, which is the one staleness this key cannot
/// detect on its own.
inline constexpr int kSlangDrivingSchema = 1;

/// @returns whether the slang runtime library could be loaded on this machine.
///
/// The first call attempts the load and logs the outcome once; later calls are
/// free. False means every `.slang` compile will fail -- loudly, per compile
/// attempt -- which is deliberate: a quiet fallback to a second shader path is
/// exactly the divergence T0142.13 exists to remove.
[[nodiscard]] bool slangCompilerAvailable();

/// Compiles one shader source to SPIR-V through slang.
///
/// @param filePath the shader's file name as the source factory knows it, e.g.
///        `"HpSurface.slang"`. Also the name diagnostics carry.
/// @param stage which pipeline stage to compile for.
/// @param macros the permutation's preprocessor definitions -- exactly what
///        `PBR_Renderer::DefineMacros` produced (142.5).
/// @param macroCount how many.
/// @param sources resolves the file itself and every `#include` in it. This is
///        142.4: the same compound factory Diligent's path uses -- generated
///        structs first, then the engine's embedded set, then DiligentFX's --
///        bridged to slang's `ISlangFileSystem`, so the two compilers can never
///        disagree about what a name resolves to.
/// @param outSpirv receives the bytecode on success.
/// @param outDiagnostics receives the compiler's message text, on success
///        (warnings) and failure (errors) alike. The caller owns logging it on
///        the attempt, once -- never from a draw path (T0141).
/// @param reflect what to report about the module, or null for nothing.
///        **Asking for reflection defeats the bytecode cache for this call**:
///        a cached hit has no `ProgramLayout` behind it, so a caller that needs
///        the layout compiles. That is deliberate and cheap -- it happens once
///        per module per process, and the alternative is caching a second
///        artefact whose staleness nothing could detect.
/// @returns whether compilation succeeded.
[[nodiscard]] bool compileSlangToSpirv(const char* filePath, ShaderStage stage,
                                       const SlangMacro* macros, std::size_t macroCount,
                                       Diligent::IShaderSourceInputStreamFactory* sources,
                                       std::vector<std::uint8_t>& outSpirv,
                                       std::string& outDiagnostics,
                                       const SlangReflectionRequest* reflect = nullptr);

} // namespace hp
