// Where the engine's own shader source comes from (T0141, D26).
//
// **D26 in one file.** The engine writes its own vertex and pixel shaders and
// calls DiligentFX's public shading library from them, rather than patching
// DiligentFX's private material shader. For that to work, a shader in *our* tree
// has to be able to `#include "PBR_Shading.fxh"` from *theirs* — which is what
// this produces: one source factory that serves both.
//
// **No shader is ever read from disk.** They are embedded into the binary at
// build time by `cmake/hp_embed_shaders.cmake`, the same way DiligentFX embeds
// its own. So D13's "every read goes through the VFS" stays true by there being
// no read at all, `dist` has nothing extra to install, and a missing shader is a
// build error rather than a black screen.
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
    /// Runs per vertex. Ours under D26, which is what makes vertex displacement
    /// reachable (T0141.7).
    Vertex,

    /// Runs per pixel. Ours under D26, and where the surface stage lives.
    Pixel,
};

/// Creates the source factory the engine compiles its shaders through.
///
/// **Ours first, then DiligentFX's.** A compound factory resolves an include by
/// asking each source in order, so this order means an engine shader may include
/// any DiligentFX public header — `PBR_Structures.fxh`, `PBR_Shading.fxh`,
/// `Shadows.fxh`, `PCF.fxh` — while a name collision resolves to ours. That is
/// the right way round: if the engine ever ships a file that shadows one of
/// theirs it will be on purpose, and the reverse would be a silent surprise
/// after an upgrade.
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

/// Compiles one embedded shader and reports whether it succeeded.
///
/// **A validation pass, not the render path.** Nothing keeps the result: the
/// shader is released before returning, and a renderer that wants one holds it
/// itself. What this is for is answering "does this build's shader set actually
/// compile on this device" without creating a pipeline, which is the question
/// worth asking at startup and the one a test can ask cheaply.
///
/// This is the seed of T0141.3's warm-up: once shaders are cached
/// (`RenderStateCache`, `BytecodeCache`), the same walk over the embedded set is
/// what fills the cache before the first frame instead of hitching on it.
///
/// **The compiler's own message goes to the log on failure** and nowhere else —
/// see T0141's "log on the transition, never per draw". A compile is a
/// transition; this is the right place for it.
///
/// @param device the device to compile on.
/// @param name the shader's file name as it appears in `engine/shaders/`, e.g.
///        `"HpSurface.psh"`.
/// @param stage which pipeline stage to compile it for.
/// @returns whether it compiled. **False is not fatal** — the caller decides,
///          and for a material shader that decision is the checkerboard
///          (T0141.4).
[[nodiscard]] HP_API bool compileEngineShader(Diligent::IRenderDevice* device,
                                              std::string_view name, ShaderStage stage);

/// Builds one pipeline state from the engine's own shaders and reports whether
/// the device accepted it (T0141.10).
///
/// **Compiling a shader and building a pipeline are different questions**, and
/// only the second one is the claim D26 rests on. A shader can compile in
/// isolation and still be refused by the device once it has to agree with a
/// vertex layout, a resource signature and a render-pass format — which is
/// exactly the seam between our pixel shader and DiligentFX's vertex shader.
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
