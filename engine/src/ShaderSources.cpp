// The engine's shader source factory (T0141, D26). See `hp/ShaderSources.hpp`.

#include <hp/ShaderSources.hpp>

#include "SlangCompiler.hpp"
#include "SurfacePipeline.hpp"

#include <hp/Log.hpp>
#include <hp/Vfs.hpp>

#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <DataBlobImpl.hpp>
#include <DiligentFXShaderSourceStreamFactory.hpp>
#include <GraphicsTypes.h>
#include <MemoryFileStream.hpp>
#include <ObjectBase.hpp>
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

/// Whether @p path 's basename matches one of the engine's embedded shaders.
///
/// Those names are **reserved** (D27): the contract must have exactly one
/// definition, and this is the check that enforces it against mounted content.
bool isReservedEngineShaderName(const char* path) {
    const char* base = path;
    for (const char* c = path; *c != '\0'; ++c) {
        if (*c == '/' || *c == '\\') {
            base = c + 1;
        }
    }
    for (int i = 0; i < g_hpShaderCount; ++i) {
        if (std::strcmp(base, g_hpShaders[i].Name) == 0) {
            return true;
        }
    }
    return false;
}

/// Says so once per offending path — a mounted file that silently never
/// resolves is a confusing afternoon, and per-compile repetition would be
/// noise. Compiles are transitions, but one line per pipeline permutation is
/// still several lines too many for the same file.
void warnAboutReservedName(const char* path) {
    static std::mutex mutex;
    static std::unordered_set<std::string> warned;
    std::lock_guard<std::mutex> lock(mutex);
    if (warned.insert(path).second) {
        HP_LOG_WARN(kLog,
                    "mounted shader '{}' shadows a reserved engine shader name and is ignored; "
                    "engine contract headers have exactly one definition (D27)",
                    path);
    }
}

/// A shader source that reads through the virtual filesystem (T0142.14, D13).
///
/// **This is what makes a game's `.slang` content.** The compound factory
/// consults it after the engine's embedded set and before DiligentFX's tree,
/// so a shader in a mounted directory or pack resolves like any other asset —
/// and the same factory feeds both compilers through `FactoryFileSystem`
/// (`SlangCompiler.cpp`), so slang and Diligent cannot disagree about what a
/// name means.
///
/// A VFS that is not initialised, or a path that does not resolve, returns
/// no stream and the compound moves on — which is exactly what makes this
/// safe to keep in the chain unconditionally: an engine with no mounts
/// behaves as it did before this existed.
class VfsShaderSourceFactory final
    : public Diligent::ObjectBase<Diligent::IShaderSourceInputStreamFactory> {
public:
    using TBase = Diligent::ObjectBase<Diligent::IShaderSourceInputStreamFactory>;

    explicit VfsShaderSourceFactory(Diligent::IReferenceCounters* refCounters)
        : TBase{refCounters} {}

    // Diligent's `IMPLEMENT_QUERY_INTERFACE_IN_PLACE` assumes it expands
    // inside `namespace Diligent`; written out because this class does not.
    void DILIGENT_CALL_TYPE QueryInterface(const Diligent::INTERFACE_ID& iid,
                                           Diligent::IObject** iface) override {
        if (iface == nullptr) {
            return;
        }
        if (iid == Diligent::IID_IShaderSourceInputStreamFactory) {
            *iface = this;
            (*iface)->AddRef();
        } else {
            TBase::QueryInterface(iid, iface);
        }
    }

    Diligent::Bool DILIGENT_CALL_TYPE CreateInputStream(const Diligent::Char* name,
                                                        Diligent::IFileStream** stream) override {
        return CreateInputStream2(name, Diligent::CREATE_SHADER_SOURCE_INPUT_STREAM_FLAG_NONE,
                                  stream);
    }

    Diligent::Bool DILIGENT_CALL_TYPE
    CreateInputStream2(const Diligent::Char* name,
                       Diligent::CREATE_SHADER_SOURCE_INPUT_STREAM_FLAGS flags,
                       Diligent::IFileStream** stream) override {
        if (stream == nullptr) {
            return false;
        }
        *stream = nullptr;
        if (name == nullptr || !Vfs::ready()) {
            return false;
        }
        // Compilers resolve an include relative to the including file, which
        // for content at the mount root arrives `./`-prefixed. The VFS knows
        // only clean virtual paths.
        while (name[0] == '.' && (name[1] == '/' || name[1] == '\\')) {
            name += 2;
        }

        // **Engine shader names are reserved under every path, not just at
        // the root** (D27, T0142.14) — and the test that added this line is
        // why. A `#include "HpMaterial.slang"` inside a mounted shader makes
        // Diligent try the include-relative candidate first
        // (`shaders/HpMaterial.slang`), which the engine's embedded set does not
        // carry — so a file named like the contract, sitting *beside* a game
        // shader, would win the relative round and redefine the contract
        // after all. Refusing to serve any path whose basename matches an
        // embedded engine shader closes that hole: the relative candidate
        // misses, the bare-name candidate resolves engine-first, and the
        // contract has exactly one definition.
        if (isReservedEngineShaderName(name)) {
            warnAboutReservedName(name);
            return false;
        }

        const std::optional<std::vector<std::byte>> bytes = Vfs::read(name);
        if (!bytes) {
            // Silent by design, whatever @p flags says: inside a compound
            // factory "not here" is the normal answer, and the compound logs
            // once if *nobody* had the file.
            return false;
        }

        auto blob = Diligent::DataBlobImpl::Create(bytes->size(), bytes->data());
        Diligent::RefCntAutoPtr<Diligent::MemoryFileStream> memory{
            Diligent::MakeNewRCObj<Diligent::MemoryFileStream>()(blob)};
        *stream = memory.Detach();
        return true;
    }
};

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

    // A game's shaders, through the VFS (T0142.14, D13). Content, so it sits
    // **after** the engine's embedded set — a project must not be able to
    // shadow `HpMaterial.slang` or `HpSurface.slang` and quietly redefine the
    // contract — and **before** DiligentFX's tree. Engine and DiligentFX
    // header names are therefore reserved: a game file named like either is
    // shadowed by ours or shadows theirs, and both are documented constraints
    // rather than resolvable ones, because DiligentFX's factory cannot be
    // enumerated to warn.
    Diligent::RefCntAutoPtr<VfsShaderSourceFactory> vfs{
        Diligent::MakeNewRCObj<VfsShaderSourceFactory>()()};

    // Ours first. See the header: an engine shader may include any DiligentFX
    // public header, and a name collision resolves to ours deliberately.
    Diligent::IShaderSourceInputStreamFactory* sources[] = {
        ours, vfs, &Diligent::DiligentFXShaderSourceStreamFactory::GetInstance()};

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

    // **Through slang, like every other shader in this engine** (T0142.13).
    // This used to hand the source to Diligent's own HLSL front end, which made
    // it a *second* compiler reachable from engine code -- and "two paths that
    // both work" is the outcome T0142.13 exists to remove, whatever the second
    // path is only used by. What this function proves is that the compound
    // factory resolves a name and its includes; routing it through the real
    // compiler makes that proof stronger, not weaker.
    std::vector<std::uint8_t> spirv;
    std::string diagnostics;
    if (!compileSlangToSpirv(path.c_str(), stage, nullptr, 0, factory, spirv, diagnostics)) {
        // **Logged here, on the attempt.** A compile is a transition, so this is
        // the one place a shader failure belongs; the draw path that substitutes
        // a fallback must stay silent or it produces thousands of lines a minute
        // (T0141).
        HP_LOG_ERROR(kLog, "shader '{}' did not compile: {}", path,
                     diagnostics.empty() ? "(no diagnostics)" : diagnostics);
        return false;
    }

    // **And then the device has to accept it.** Compiling is half the claim; a
    // module the driver refuses is not a shader, and returning true on bytecode
    // nobody validated would make this weaker than what it replaced.
    Diligent::ShaderCreateInfo info;
    info.Desc = {path.c_str(),
                 stage == ShaderStage::Vertex ? Diligent::SHADER_TYPE_VERTEX
                                              : Diligent::SHADER_TYPE_PIXEL,
                 // Separate samplers, which is what Vulkan wants. Combined
                 // samplers existed for the GL backend, which D29 removed.
                 /*UseCombinedTextureSamplers = */ false};
    info.EntryPoint = "main";
    // **`ByteCode`, not `Source`** -- the two are mutually exclusive and their
    // size fields are a union, so setting the wrong one sends SPIR-V down the
    // text path.
    info.ByteCode = spirv.data();
    info.ByteCodeSize = spirv.size();

    Diligent::RefCntAutoPtr<Diligent::IShader> shader;
    device->CreateShader(info, &shader);
    if (!shader) {
        HP_LOG_ERROR(kLog, "the device refused slang-compiled SPIR-V for '{}'", path);
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
