#include "SurfacePipeline.hpp"

#include <hp/Log.hpp>
#include <hp/Profiling.hpp>
#include <hp/ShaderSources.hpp>

#include <GLTFLoader.hpp>
#include <GraphicsTypesX.hpp>
#include <RenderDevice.h>
#include <ShaderSourceFactoryUtils.h>

#include <string>

namespace hp {
namespace {

const LogCategory kLog("render.pipeline");

/// The engine's vertex shader.
///
/// **DiligentFX's, for now, and that is a deliberate half-step.** The surface
/// stage is a pixel-shader concept: parallax, triplanar and texture blending all
/// happen per fragment, so owning the pixel shader is what D26 actually needed.
/// `RenderPBR.vsh` already produces exactly the `VSOutput` our pixel shader
/// consumes, and reimplementing it now would be work with no capability behind
/// it.
///
/// **141.7's vertex displacement is what changes this**, and it is the only
/// thing that should: at that point the engine writes its own and this constant
/// moves to `HpSurface.vsh`. Naming it here rather than inlining the string is
/// what makes that a one-line change.
constexpr const char* kVertexShader = "RenderPBR.vsh";

/// The engine's pixel shader — ours, and the whole point of D26.
constexpr const char* kPixelShader = "HpSurface.psh";

/// Folds the render-target formats into the PSO key's hash.
///
/// Two targets with different formats need different pipelines for the same
/// shader. A cache keyed on the shader alone hands back one built for the wrong
/// render pass, which Vulkan reports as an incompatibility rather than drawing
/// something slightly wrong -- loud, but a long way from the cause.
std::size_t cacheKey(const Diligent::GraphicsPipelineDesc& graphics,
                     const Diligent::PBR_Renderer::PSOKey& key) {
    std::size_t hash = Diligent::PBR_Renderer::PSOKey::Hasher{}(key);
    const auto mix = [&hash](std::size_t value) {
        hash ^= value + 0x9E3779B97F4A7C15ULL + (hash << 6U) + (hash >> 2U);
    };
    mix(static_cast<std::size_t>(graphics.NumRenderTargets));
    for (Diligent::Uint8 i = 0; i < graphics.NumRenderTargets; ++i) {
        mix(static_cast<std::size_t>(graphics.RTVFormats[i]));
    }
    mix(static_cast<std::size_t>(graphics.DSVFormat));
    mix(static_cast<std::size_t>(graphics.PrimitiveTopology));
    mix(static_cast<std::size_t>(graphics.DepthStencilDesc.DepthFunc));
    mix(graphics.DepthStencilDesc.DepthEnable ? 1U : 0U);
    return hash;
}

} // namespace

SurfacePipeline::SurfacePipeline(Diligent::IRenderDevice* device,
                                 Diligent::IRenderStateCache* cache,
                                 Diligent::IDeviceContext* context, const CreateInfo& info)
    : Diligent::PBR_Renderer(device, cache, context, info) {}

Diligent::IPipelineState* SurfacePipeline::pipeline(const Diligent::GraphicsPipelineDesc& graphics,
                                                    const PSOKey& key) {
    HP_PROFILE_ZONE();

    const std::size_t hash = cacheKey(graphics, key);
    if (const auto found = pipelines_.find(hash); found != pipelines_.end()) {
        return found->second;
    }

    Diligent::RefCntAutoPtr<Diligent::IPipelineState> built = build(graphics, key);
    // **Cached even when null.** A shader that will not compile does not compile
    // any better on the next frame, and retrying would turn one logged failure
    // into one per frame -- which is the trap T0141 records against the *draw*
    // path and which applies just as well here.
    pipelines_[hash] = built;
    return built;
}

Diligent::RefCntAutoPtr<Diligent::IPipelineState>
SurfacePipeline::build(const Diligent::GraphicsPipelineDesc& graphics, const PSOKey& key) {
    HP_PROFILE_ZONE();

    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso;

    // The macro set for this key. **Without it `PBR_Shading.fxh` does not
    // compile at all**: every optional feature in it is behind a macro that
    // `DefineMacros` defines, so this is the single reason the subclass exists
    // rather than a free function building pipelines beside the renderer.
    Diligent::ShaderMacroHelper macros = DefineMacros(key);

    // The generated interface structs, which the shaders include by name --
    // exactly as `RenderPBR.psh` does. Reusing the base class's generators is
    // what keeps our pixel shader and their vertex shader agreeing about
    // `VSOutput` without either of us restating it.
    Diligent::InputLayoutDescX inputLayout;
    std::string vsInputStruct;
    GetVSInputStructAndLayout(key.GetFlags(), vsInputStruct, inputLayout);
    const std::string vsOutputStruct =
        GetVSOutputStruct(key.GetFlags(), /*UseVkPointSize = */ false,
                          /*UsePrimitiveId = */ false);
    const std::string psOutputStruct = GetPSOutputStruct(key.GetFlags());

    const Diligent::MemoryShaderSourceFileInfo generatedSources[] = {
        {"VSInputStruct.generated", vsInputStruct},
        {"VSOutputStruct.generated", vsOutputStruct},
        {"PSOutputStruct.generated", psOutputStruct},
        // Empty, and it has to exist: `RenderPBR.vsh` includes it
        // unconditionally, and a missing include is a compile error rather than
        // an empty expansion.
        {"PSMainFooter.generated", ""},
    };
    Diligent::RefCntAutoPtr<Diligent::IShaderSourceInputStreamFactory> generated;
    Diligent::CreateMemoryShaderSourceFactory(
        // **Copied.** The strings above are locals and the factory outlives this
        // function -- the engine's own factory can pass `false` because its
        // sources are string literals in the binary, and this one cannot.
        {generatedSources, static_cast<Diligent::Uint32>(_countof(generatedSources)),
         /*CopySources = */ true},
        &generated);
    if (!generated) {
        HP_LOG_ERROR(kLog, "could not create the generated shader struct factory");
        return pso;
    }

    Diligent::RefCntAutoPtr<Diligent::IShaderSourceInputStreamFactory> engine;
    createEngineShaderFactory(&engine);
    if (!engine) {
        return pso;
    }

    // Generated first, then the engine's own (which is itself ours-then-
    // DiligentFX). The generated structs must win: they are per-PSO and there is
    // no other copy of them to fall back to.
    Diligent::IShaderSourceInputStreamFactory* sources[] = {generated, engine};
    Diligent::RefCntAutoPtr<Diligent::IShaderSourceInputStreamFactory> factory;
    Diligent::CreateCompoundShaderSourceFactory({sources, _countof(sources)}, &factory);
    if (!factory) {
        HP_LOG_ERROR(kLog, "could not combine the shader sources for this pipeline");
        return pso;
    }

    const bool combinedSamplers = GetDevice()->GetDeviceInfo().IsGLDevice();

    Diligent::ShaderCreateInfo shaderInfo;
    shaderInfo.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
    shaderInfo.pShaderSourceStreamFactory = factory;
    shaderInfo.Macros = macros;
    shaderInfo.EntryPoint = "main";
    // Row-major, matching `CreateInfo::PackMatrixRowMajor`. **Getting this wrong
    // is invisible**: every matrix is read transposed, the geometry lands off
    // screen, and the frame comes back clear-coloured with a draw counted.
    // T0028 paid an afternoon for it once already.
    shaderInfo.CompileFlags = GetSettings().PackMatrixRowMajor
                                  ? Diligent::SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR
                                  : Diligent::SHADER_COMPILE_FLAG_NONE;

    Diligent::RefCntAutoPtr<Diligent::IShader> vertexShader;
    shaderInfo.Desc = {"hp surface VS", Diligent::SHADER_TYPE_VERTEX, combinedSamplers};
    shaderInfo.FilePath = kVertexShader;
    GetDevice()->CreateShader(shaderInfo, &vertexShader);

    Diligent::RefCntAutoPtr<Diligent::IShader> pixelShader;
    shaderInfo.Desc = {"hp surface PS", Diligent::SHADER_TYPE_PIXEL, combinedSamplers};
    shaderInfo.FilePath = kPixelShader;
    GetDevice()->CreateShader(shaderInfo, &pixelShader);

    if (!vertexShader || !pixelShader) {
        // **Logged here, on the compile attempt.** One line per failed pipeline,
        // never per draw -- see T0141. The result is cached so this does not
        // repeat next frame.
        HP_LOG_ERROR(kLog, "the engine's shaders did not compile ({} / {})",
                     vertexShader ? "vs ok" : "vs failed", pixelShader ? "ps ok" : "ps failed");
        return pso;
    }

    Diligent::GraphicsPipelineStateCreateInfoX psoInfo{"hp surface"};
    psoInfo.GraphicsPipeline = graphics;
    psoInfo.GraphicsPipeline.InputLayout = inputLayout;

    // **The key drives pipeline state, not just shader macros**, and forgetting
    // that is invisible without a pixel test. `PBR_Renderer::CreatePSO` reads
    // `Key.GetCullMode()` into the rasterizer; the first version of this did
    // not, so every draw got the `GraphicsPipelineDesc` default of
    // `CULL_MODE_BACK` and a **double-sided** quad was back-face culled. The
    // pipeline built, the draw was submitted, statistics reported one
    // submission, and the target came back pure clear colour -- which reads
    // exactly like a transform bug and is not one.
    psoInfo.GraphicsPipeline.RasterizerDesc.CullMode = key.GetCullMode();

    // Blend state follows the alpha mode, the same way. Opaque and mask do not
    // blend; `Blend` is premultiplied alpha, matching what T0106.4 will want for
    // particles. **Depth writes stay on for blended geometry here** because
    // there is no OIT path in this engine yet -- T0045 owns the sorted
    // transparent queue, and turning writes off before that exists would trade
    // one wrong image for another.
    Diligent::RenderTargetBlendDesc& blend = psoInfo.GraphicsPipeline.BlendDesc.RenderTargets[0];
    if (key.GetAlphaMode() == Diligent::GLTF::Material::ALPHA_MODE_BLEND) {
        blend.BlendEnable = true;
        blend.SrcBlend = Diligent::BLEND_FACTOR_ONE;
        blend.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
        blend.BlendOp = Diligent::BLEND_OPERATION_ADD;
        blend.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
        blend.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
        blend.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
    } else {
        blend.BlendEnable = false;
    }
    psoInfo.AddShader(vertexShader);
    psoInfo.AddShader(pixelShader);
    // **The base class's signature, not one of ours.** It already describes the
    // frame, primitive and material buffers and every texture slot, and building
    // a second one would mean `CreateResourceBinding` handing back bindings this
    // pipeline could not accept.
    for (auto& signature : m_ResourceSignatures) {
        psoInfo.AddSignature(signature);
    }

    GetDevice()->CreateGraphicsPipelineState(psoInfo, &pso);
    if (!pso) {
        HP_LOG_ERROR(kLog, "the device refused the engine's surface pipeline");
    }
    return pso;
}

} // namespace hp
