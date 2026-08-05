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
/// @returns whether compilation succeeded.
[[nodiscard]] bool compileSlangToSpirv(const char* filePath, ShaderStage stage,
                                       const SlangMacro* macros, std::size_t macroCount,
                                       Diligent::IShaderSourceInputStreamFactory* sources,
                                       std::vector<std::uint8_t>& outSpirv,
                                       std::string& outDiagnostics);

} // namespace hp
