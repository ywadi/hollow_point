// The engine's shader source factory (T0141, D26). See `hp/ShaderSources.hpp`.

#include <hp/ShaderSources.hpp>

#include <hp/Log.hpp>

#include <string>

#include <DiligentFXShaderSourceStreamFactory.hpp>
#include <GraphicsTypes.h>
#include <RenderDevice.h>
#include <Shader.h>
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

} // namespace hp
