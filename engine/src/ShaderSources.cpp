// The engine's shader source factory (T0141, D26). See `hp/ShaderSources.hpp`.

#include <hp/ShaderSources.hpp>

#include "SurfacePipeline.hpp"

#include <hp/Log.hpp>

#include <cstring>
#include <string>
#include <vector>

#include <DiligentFXShaderSourceStreamFactory.hpp>
#include <GraphicsTypes.h>
#include <RenderDevice.h>
#include <Shader.h>
#include <GLTFLoader.hpp>
#include <GraphicsTypesX.hpp>
#include <RefCntAutoPtr.hpp>
#include <ShaderSourceFactoryUtils.h>

namespace hp {
namespace {

const LogCategory kLog("render.shaders");

// The generated table of embedded sources: one `MemoryShaderSourceFileInfo` per
// file in `engine/shaders/`. Written by `cmake/hp_embed_shaders.cmake` and
// regenerated whenever a shader changes, because the sources are declared as
// dependencies of the command that runs it.
#include <hp/EngineShaders.h>

} // namespace

void createEngineShaderFactory(Diligent::IShaderSourceInputStreamFactory** factory) {
    if (factory == nullptr) {
        return;
    }
    *factory = nullptr;

    Diligent::RefCntAutoPtr<Diligent::IShaderSourceInputStreamFactory> ours;
    const Diligent::MemoryShaderSourceFactoryCreateInfo info{
        g_hpShaders, static_cast<Diligent::Uint32>(g_hpShaderCount),
        // **Copy the strings, false.** They are string literals in the binary's
        // read-only data and outlive every factory built from them, so copying
        // would duplicate the whole shader set per factory for nothing. This is
        // the one place that is safe, precisely because they are embedded rather
        // than read.
        /*CopySources = */ false};
    Diligent::CreateMemoryShaderSourceFactory(info, &ours);
    if (!ours) {
        HP_LOG_ERROR(kLog, "could not create the engine shader source factory");
        return;
    }

    // Ours first. See the header: an engine shader may include any DiligentFX
    // public header, and a name collision resolves to ours deliberately.
    Diligent::IShaderSourceInputStreamFactory* sources[] = {
        ours, &Diligent::DiligentFXShaderSourceStreamFactory::GetInstance()};

    Diligent::RefCntAutoPtr<Diligent::IShaderSourceInputStreamFactory> compound;
    Diligent::CreateCompoundShaderSourceFactory({sources, _countof(sources)}, &compound);
    if (!compound) {
        HP_LOG_ERROR(kLog, "could not combine the engine and DiligentFX shader sources");
        return;
    }

    *factory = compound.Detach();
}

int embeddedShaderCount() {
    return g_hpShaderCount;
}

bool compileEngineShader(Diligent::IRenderDevice* device, std::string_view name,
                         ShaderStage stage) {
    if (device == nullptr) {
        return false;
    }

    Diligent::RefCntAutoPtr<Diligent::IShaderSourceInputStreamFactory> factory;
    createEngineShaderFactory(&factory);
    if (!factory) {
        return false;
    }

    const std::string path{name};

    Diligent::ShaderCreateInfo info;
    info.Desc = {path.c_str(),
                 stage == ShaderStage::Vertex ? Diligent::SHADER_TYPE_VERTEX
                                              : Diligent::SHADER_TYPE_PIXEL,
                 // Combined texture samplers: what the GL backend needs, and
                 // what `PBR_Renderer` passes for the same reason.
                 device->GetDeviceInfo().IsGLDevice()};
    // **HLSL, and that is D2's consequence rather than a preference.** OpenGL is
    // the only fallback on Windows, and Diligent's portable path is HLSL --
    // through glslang for Vulkan and converted for GL. GLSL written directly
    // does not reach both backends through one pipeline.
    info.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
    info.EntryPoint = "main";
    // By name: it resolves to a string embedded in this binary, and any
    // `#include` inside it resolves through the compound factory -- ours first,
    // then DiligentFX's public headers. Nothing is opened.
    info.FilePath = path.c_str();
    info.pShaderSourceStreamFactory = factory;

    Diligent::RefCntAutoPtr<Diligent::IShader> shader;
    device->CreateShader(info, &shader);
    if (!shader) {
        // **Logged here, on the attempt.** A compile is a transition, so this is
        // the one place a shader failure belongs; the draw path that substitutes
        // a fallback must stay silent or it produces thousands of lines a minute
        // (T0141).
        HP_LOG_ERROR(kLog, "shader '{}' did not compile", path);
        return false;
    }
    return true;
}

bool probePrecompiledSpirvPipeline(Diligent::IRenderDevice* device,
                                   Diligent::IDeviceContext* context, const void* vertexSpirv,
                                   std::size_t vertexSize, const void* pixelSpirv,
                                   std::size_t pixelSize, std::vector<std::uint8_t>& outRgba) {
    if (device == nullptr || context == nullptr || vertexSpirv == nullptr || pixelSpirv == nullptr) {
        return false;
    }
    // Diligent asserts on this rather than failing gracefully, so it is checked
    // here where the caller can be told why.
    if (vertexSize == 0 || pixelSize == 0 || vertexSize % 4 != 0 || pixelSize % 4 != 0) {
        HP_LOG_ERROR(kLog, "SPIR-V size must be a non-zero multiple of 4 ({} / {})", vertexSize,
                     pixelSize);
        return false;
    }

    // **`ByteCode`, not `Source`** — the two are mutually exclusive and their
    // size fields are a union, so setting the wrong one sends HLSL text down the
    // bytecode path or vice versa.
    const auto makeShader = [&](const void* code, std::size_t size, Diligent::SHADER_TYPE type,
                                const char* name) {
        Diligent::ShaderCreateInfo info;
        info.ByteCode = code;
        info.ByteCodeSize = size;
        info.Desc = {name, type, /*UseCombinedTextureSamplers = */ false};
        info.EntryPoint = "main";
        Diligent::RefCntAutoPtr<Diligent::IShader> shader;
        device->CreateShader(info, &shader);
        return shader;
    };

    Diligent::RefCntAutoPtr<Diligent::IShader> vertexShader =
        makeShader(vertexSpirv, vertexSize, Diligent::SHADER_TYPE_VERTEX, "slang probe VS");
    Diligent::RefCntAutoPtr<Diligent::IShader> pixelShader =
        makeShader(pixelSpirv, pixelSize, Diligent::SHADER_TYPE_PIXEL, "slang probe PS");
    if (!vertexShader || !pixelShader) {
        HP_LOG_ERROR(kLog, "the device refused the precompiled SPIR-V ({} / {})",
                     vertexShader ? "vs ok" : "vs failed", pixelShader ? "ps ok" : "ps failed");
        return false;
    }

    constexpr Diligent::Uint32 kSize = 8;
    // **UNORM, not sRGB.** The probe asserts an exact colour, and an sRGB target
    // would re-encode it — turning a correctness check into a gamma quiz.
    constexpr Diligent::TEXTURE_FORMAT kFormat = Diligent::TEX_FORMAT_RGBA8_UNORM;

    Diligent::GraphicsPipelineStateCreateInfoX psoInfo{"slang probe"};
    psoInfo.GraphicsPipeline.NumRenderTargets = 1;
    psoInfo.GraphicsPipeline.RTVFormats[0] = kFormat;
    psoInfo.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    psoInfo.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    psoInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
    psoInfo.AddShader(vertexShader);
    psoInfo.AddShader(pixelShader);

    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso;
    device->CreateGraphicsPipelineState(psoInfo, &pso);
    if (!pso) {
        HP_LOG_ERROR(kLog, "the device refused a pipeline built from precompiled SPIR-V");
        return false;
    }

    Diligent::TextureDesc targetDesc;
    targetDesc.Name = "slang probe target";
    targetDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    targetDesc.Width = kSize;
    targetDesc.Height = kSize;
    targetDesc.Format = kFormat;
    targetDesc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
    Diligent::RefCntAutoPtr<Diligent::ITexture> target;
    device->CreateTexture(targetDesc, nullptr, &target);

    Diligent::TextureDesc stagingDesc = targetDesc;
    stagingDesc.Name = "slang probe staging";
    stagingDesc.Usage = Diligent::USAGE_STAGING;
    stagingDesc.BindFlags = Diligent::BIND_NONE;
    stagingDesc.CPUAccessFlags = Diligent::CPU_ACCESS_READ;
    Diligent::RefCntAutoPtr<Diligent::ITexture> staging;
    device->CreateTexture(stagingDesc, nullptr, &staging);
    if (!target || !staging) {
        return false;
    }

    Diligent::ITextureView* rtv = target->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
    context->SetRenderTargets(1, &rtv, nullptr,
                              Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    const float clear[] = {0.0F, 0.0F, 1.0F, 1.0F};
    context->ClearRenderTarget(rtv, clear, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->SetPipelineState(pso);
    context->Draw(Diligent::DrawAttribs{3, Diligent::DRAW_FLAG_VERIFY_ALL});

    Diligent::CopyTextureAttribs copy{target, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                      staging,
                                      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION};
    context->CopyTexture(copy);
    context->SetRenderTargets(0, nullptr, nullptr,
                              Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->WaitForIdle();

    Diligent::MappedTextureSubresource mapped;
    context->MapTextureSubresource(staging, 0, 0, Diligent::MAP_READ, Diligent::MAP_FLAG_DO_NOT_WAIT,
                                   nullptr, mapped);
    if (mapped.pData == nullptr) {
        return false;
    }
    outRgba.resize(static_cast<std::size_t>(kSize) * kSize * 4);
    for (Diligent::Uint32 y = 0; y < kSize; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.Stride;
        std::memcpy(outRgba.data() + static_cast<std::size_t>(y) * kSize * 4, row, kSize * 4);
    }
    context->UnmapTextureSubresource(staging, 0, 0);
    return true;
}

bool buildEngineSurfacePipeline(Diligent::IRenderDevice* device,
                                Diligent::IDeviceContext* context) {
    if (device == nullptr || context == nullptr) {
        return false;
    }

    // **The engine's real settings, via the same function `SceneRenderer` calls.**
    // Restating them here is what let this test pass against a `CreateInfo` that
    // differed from the shipping one -- and the field the two disagreed about
    // was the one whose unset value silently disables three subsystems. A test
    // that configures its own renderer is testing its own renderer.
    Diligent::PBR_Renderer::CreateInfo info;
    SurfacePipeline::configure(info);

    const Diligent::InputLayoutDescX inputLayout = Diligent::GLTF::VertexAttributesToInputLayout(
        Diligent::GLTF::DefaultVertexAttributes.data(),
        static_cast<Diligent::Uint32>(Diligent::GLTF::DefaultVertexAttributes.size()));
    info.InputLayout = inputLayout;

    SurfacePipeline pipeline{device, nullptr, context, info};

    Diligent::GraphicsPipelineDesc graphics;
    graphics.NumRenderTargets = 1;
    graphics.RTVFormats[0] = Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
    graphics.DSVFormat = Diligent::TEX_FORMAT_D32_FLOAT;
    graphics.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    graphics.DepthStencilDesc.DepthEnable = true;
    graphics.DepthStencilDesc.DepthWriteEnable = true;
    // Reverse-Z, per T0130 — the engine's convention, not Diligent's default.
    graphics.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_GREATER_EQUAL;

    const Diligent::PBR_Renderer::PSOKey key{
        Diligent::PBR_Renderer::RenderPassType::Main,
        static_cast<Diligent::PBR_Renderer::PSO_FLAGS>(
            Diligent::PBR_Renderer::PSO_FLAG_VERTEX_ATTRIBS |
            Diligent::PBR_Renderer::PSO_FLAG_USE_VERTEX_NORMALS |
            Diligent::PBR_Renderer::PSO_FLAG_USE_TEXCOORD0 |
            Diligent::PBR_Renderer::PSO_FLAG_DEFAULT_TEXTURES |
            Diligent::PBR_Renderer::PSO_FLAG_USE_LIGHTS),
        Diligent::CULL_MODE_BACK};

    return pipeline.pipeline(graphics, key) != nullptr;
}

} // namespace hp
