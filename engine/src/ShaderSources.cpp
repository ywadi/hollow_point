// The engine's shader source factory (T0141, D26). See `hp/ShaderSources.hpp`.

#include <hp/ShaderSources.hpp>

#include "SurfacePipeline.hpp"

#include <hp/Log.hpp>

#include <string>

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
                 // Separate samplers, which is what Vulkan wants. Combined
                 // samplers existed for the GL backend, which D29 removed.
                 /*UseCombinedTextureSamplers = */ false};
    // HLSL: this function exercises *Diligent's* compile path over the embedded
    // sources -- what it proves is that the factory resolves names and includes
    // correctly, and Diligent's HLSL front end (glslang, for Vulkan) is the one
    // it ships. The engine's real pipelines compile the same bytes through
    // slang instead (D28, `SurfacePipeline::build`).
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
