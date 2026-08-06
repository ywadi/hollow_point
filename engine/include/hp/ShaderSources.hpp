// Where the engine's own shader source comes from (T0141, D26).
//
// **D26 in one file.** The engine writes its own vertex and pixel shaders and
// calls DiligentFX's public shading library from them, rather than patching
// DiligentFX's private material shader. For that to work, a shader in *our* tree
// has to be able to `#include "PBR_Shading.fxh"` from *theirs* — which is what
// this produces: one source factory that serves both.
//
// **No *engine* shader is ever read from disk.** They are embedded into the
// binary at build time by `cmake/hp_embed_shaders.cmake`, the same way
// DiligentFX embeds its own. So D13's "every read goes through the VFS" stays
// true by there being no read at all, `dist` has nothing extra to install, and
// a missing shader is a build error rather than a black screen. A *game's*
// shader is content and does come through the VFS (T0142.14) — that is the
// second source in the chain below.
//
// **Cooking did not change that** (T0142.7, D34): the cooked-shader key is a
// content hash over the resolved source text, so a shipped game still reads
// every one of these strings before it finds the bytecode. Embedding is not
// something cooked output replaces.
#pragma once

#include <hp/Api.hpp>

#include <cstdint>
#include <string_view>

namespace Diligent {
struct IShaderSourceInputStreamFactory;
struct IRenderDevice;
struct IDeviceContext;
} // namespace Diligent

namespace hp {

/// Which pipeline stage a shader is compiled for.
enum class ShaderStage : std::uint8_t {
    /// Runs per vertex. **The engine's own since T0146** — `HpSurface.slang`'s
    /// `vsMain`, which is what makes `IHpMaterial.vertex()` reachable at all
    /// (D26, D36). It was DiligentFX's `RenderPBR.vsh` until then.
    Vertex,

    /// Runs per pixel. Ours under D26, and where the surface stage lives.
    Pixel,
};

/// Creates the source factory the engine compiles its shaders through.
///
/// **Engine, then the game's content, then DiligentFX** (T0142.14, D13). A
/// compound factory resolves an include by asking each source in order:
///
/// - the engine's embedded shaders come first, so a project cannot shadow
///   `HpMaterial.slang` or `HpSurface.slang` and quietly redefine the contract;
/// - the **virtual filesystem** comes second, which is what makes a game's
///   `.slang` ordinary content — mounted directories and packs resolve here,
///   and an engine with no mounts behaves exactly as if this source did not
///   exist;
/// - DiligentFX's tree comes last, so an engine shader may include any of its
///   public headers — `PBR_Structures.fxh`, `PBR_Shading.fxh`, `Shadows.fxh`,
///   `PCF.fxh`.
///
/// The consequence worth stating: **engine and DiligentFX header names are
/// reserved.** Engine names are *enforced*: the VFS source refuses to serve
/// any path whose basename matches an embedded engine shader, with a warning
/// — merely ordering the sources was not enough, because a compiler's
/// include-relative candidate (`shaders/HpMaterial.slang`) would resolve from
/// the mount before the bare name ever reached the engine's copy. DiligentFX
/// names stay a documented constraint (D27): their factory cannot be
/// enumerated to enforce against.
///
/// The slang bridge (`FactoryFileSystem`) consumes this same factory, so both
/// compilers see one resolution order.
///
/// The caller owns the returned factory and must release it; use
/// `RefCntAutoPtr` at the call site rather than a raw pointer.
///
/// @param factory receives the factory. Set to nullptr if one cannot be created.
/// @returns nothing.
HP_API void createEngineShaderFactory(Diligent::IShaderSourceInputStreamFactory** factory);

/// @returns how many shader sources are embedded in this build.
///
/// An empty set fails at *pipeline creation*, which reads like a device fault
/// rather than a build one, and by then the useful diagnostic is far away.
[[nodiscard]] HP_API int embeddedShaderCount();

/// Compiles one shader through the engine's compiler and reports whether the
/// device accepted the result.
///
/// **A validation pass, not the render path.** Nothing keeps the result: the
/// shader is released before returning, and a renderer that wants one holds it
/// itself. What this is for is answering "does this name resolve, do its
/// includes resolve, and does the device take what comes out" without creating
/// a pipeline — which is the question worth asking at startup and the one a
/// test can ask cheaply.
///
/// **It goes through slang, like everything else** (T0142.13, D28). It used to
/// hand the source to Diligent's own HLSL front end, which made this a second
/// compiler reachable from engine code; two paths that both work is exactly
/// what T0142.13 removes. So this is subject to the same rules as any other
/// compile: it needs the slang runtime, and in a cooked-only build
/// (`hp/ShaderCook.hpp`) it can only succeed for a variant that was cooked.
///
/// **The compiler's own message goes to the log on failure** and nowhere else —
/// see T0141's "log on the transition, never per draw". A compile is a
/// transition; this is the right place for it.
///
/// @param device the device to hand the bytecode to.
/// @param name the shader's name as the source factory knows it — a virtual
///        path into a mount, or an engine shader. **`HpSurface.slang` no longer
///        succeeds here**: since T0146 its entry points are `vsMain` and
///        `psMain`, and this compiles `main`. What it proves is that the
///        compound factory resolves a name and its includes, which is exactly
///        what a game's module needs, so that is what it is pointed at.
/// @param stage which pipeline stage to compile it for.
/// @returns whether it compiled **and** the device accepted it. **False is not
///          fatal** — the caller decides, and for a material shader that
///          decision is the checkerboard (T0141.4).
[[nodiscard]] HP_API bool compileEngineShader(Diligent::IRenderDevice* device,
                                              std::string_view name, ShaderStage stage);

/// Builds one pipeline state from the engine's own shaders and reports whether
/// the device accepted it (T0141.10).
///
/// **Compiling a shader and building a pipeline are different questions**, and
/// only the second one is the claim D26 rests on. A shader can compile in
/// isolation and still be refused by the device once it has to agree with a
/// vertex layout, a resource signature and a render-pass format — which is
/// exactly the seam between `HpSurface.slang`'s two halves — since T0146 both
/// stages are the engine's, compiled from one file in one request, and the
/// device is what reconciles them.
///
/// Nothing is kept: the pipeline is released before returning. This is the same
/// kind of validation `compileEngineShader` is, one level up, and the same seed
/// of T0141.3's warm-up.
///
/// @param device the device to build on.
/// @param context the immediate context, which `PBR_Renderer` needs to
///        initialise its buffers and default textures.
/// @returns whether the device accepted the pipeline.
[[nodiscard]] HP_API bool buildEngineSurfacePipeline(Diligent::IRenderDevice* device,
                                                     Diligent::IDeviceContext* context);

} // namespace hp
