#include "SurfacePipeline.hpp"

#include "ModuleResourceSignature.hpp"
#include "SlangCompiler.hpp"

#include <hp/Light.hpp>
#include <hp/Log.hpp>
#include <hp/Profiling.hpp>
#include <hp/ShaderSources.hpp>
#include <hp/WindingConvention.hpp>

#include <CommonlyUsedStates.h>
#include <GLTFLoader.hpp>
#include <GraphicsTypesX.hpp>
#include <RenderDevice.h>
#include <ShaderSourceFactoryUtils.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace hp {
namespace {

const LogCategory kLog("render.pipeline");

/// The engine's surface shader — **both stages of it** (T0146, D26).
///
/// One `.slang` file (D28) carrying `vsMain` and `psMain`, compiled by slang to
/// SPIR-V at pipeline-build time. Until T0146 the vertex half was
/// `RenderPBR.vsh`, DiligentFX's, compiled by a second request; the constant
/// that named it lived here so that replacing it would be a one-line change,
/// and this is that change.
constexpr const char* kSurfaceShader = "HpSurface.slang";

/// The vertex entry point in `kSurfaceShader`.
constexpr const char* kVertexEntryPoint = "vsMain";

/// The pixel entry point in `kSurfaceShader`.
constexpr const char* kPixelEntryPoint = "psMain";

/// The custom interpolators a module's vertex hook writes (T0146.4).
///
/// **Appended to `PBR_Renderer`'s generated `VSOutput` for custom-material
/// permutations only**, which is what makes them free for the standard
/// material. Four `float4`s: `HpVertexOutput` documents why the count is fixed
/// (reflection rides the compile, so what a module wants is known only after
/// the compile that would have to be told), and 16 floats was sized against
/// the 2026-08-06 capability audit.
///
/// The semantic names are the engine's and never collide with theirs — their
/// generator emits `SV_Position`, `WORLD_POS`, `COLOR`, `NORMAL`, `UV0`, `UV1`,
/// `TANGENT`, `PREV_CLIP_POS`, `PSIZE` and `PRIM_ID`.
constexpr const char* kCustomInterpolators = R"(    float4 HpCustom0 : HP_CUSTOM0;
    float4 HpCustom1 : HP_CUSTOM1;
    float4 HpCustom2 : HP_CUSTOM2;
    float4 HpCustom3 : HP_CUSTOM3;
)";

/// Folds the render-target formats into the PSO key's hash.
///
/// Two targets with different formats need different pipelines for the same
/// shader. A cache keyed on the shader alone hands back one built for the wrong
/// render pass, which Vulkan reports as an incompatibility rather than drawing
/// something slightly wrong -- loud, but a long way from the cause.
std::size_t cacheKey(const Diligent::GraphicsPipelineDesc& graphics,
                     const Diligent::PBR_Renderer::PSOKey& key, const char* customModule) {
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
    // The material's shader module (T0142.15): two materials with different
    // modules are different pipelines whatever their flags say.
    if (customModule != nullptr) {
        mix(std::hash<std::string_view>{}(customModule));
    }
    return hash;
}

/// Prefixes every `ATTRIBn` field of the generated `VSInput` struct with
/// `[[vk::location(n)]]`, for the Slang path only.
///
/// **Slang numbers vertex inputs sequentially; the pipeline numbers them by
/// semantic.** `GetVSInputStructAndLayout` emits `Tangent : ATTRIB7` and a
/// layout whose tangent element carries `InputIndex` 7 -- Diligent's HLSL
/// path maps the semantic's own digits to the SPIR-V location, so they agree.
/// Slang ignores the digits and would put that field at location 3, and the
/// tangent buffer would feed it nothing: a mis-bound vertex stream, which
/// renders as garbage or black with no validation error naming the cause.
/// Measured on the CLI before this was written, not discovered after.
///
/// The attribute is DXC/Slang syntax that Diligent's own compilers do not
/// accept, which is why the injection happens on a Slang-path copy rather than
/// in the shared generated string.
/// Appends the custom interpolator fields to the generated `VSOutput` struct
/// (T0146.4).
///
/// **Textual, and that is the only place it can be**: `GetVSOutputStruct`
/// returns a string built from the PSO flags, and there is no hook in it. The
/// insertion point is the closing brace, which is the last `}` in a string
/// whose entire content is one struct — so a change to their generator that
/// broke this assumption would produce a shader that does not compile, named
/// here, rather than fields silently in the wrong place.
///
/// @param vsOutput the generated struct.
/// @returns the struct with four `float4` slots before its closing brace, or
///          @p vsOutput unchanged when the brace cannot be found (logged).
std::string addCustomInterpolators(const std::string& vsOutput) {
    const std::size_t brace = vsOutput.rfind('}');
    if (brace == std::string::npos) {
        HP_LOG_ERROR(kLog, "the generated VSOutput struct has no closing brace; a module's "
                           "custom interpolators were not added");
        return vsOutput;
    }
    std::string out;
    out.reserve(vsOutput.size() + 160);
    out.append(vsOutput, 0, brace);
    out += kCustomInterpolators;
    out.append(vsOutput, brace, std::string::npos);
    return out;
}

std::string addVkInputLocations(const std::string& vsInput) {
    std::string out;
    out.reserve(vsInput.size() + 128);
    std::size_t fields = 0;
    std::size_t lineStart = 0;
    while (lineStart <= vsInput.size()) {
        std::size_t lineEnd = vsInput.find('\n', lineStart);
        if (lineEnd == std::string::npos) {
            lineEnd = vsInput.size();
        }
        const std::string_view line(vsInput.data() + lineStart, lineEnd - lineStart);

        const std::size_t attrib = line.find("ATTRIB");
        std::string digits;
        if (attrib != std::string_view::npos && line.find(':') != std::string_view::npos) {
            for (std::size_t i = attrib + 6; i < line.size() && line[i] >= '0' && line[i] <= '9';
                 ++i) {
                digits.push_back(line[i]);
            }
        }
        if (!digits.empty()) {
            std::size_t indent = 0;
            while (indent < line.size() && (line[indent] == ' ' || line[indent] == '\t')) {
                ++indent;
            }
            out.append(line.substr(0, indent));
            out += "[[vk::location(";
            out += digits;
            out += ")]] ";
            out.append(line.substr(indent));
            ++fields;
        } else {
            out.append(line);
        }
        if (lineEnd < vsInput.size()) {
            out.push_back('\n');
        }
        lineStart = lineEnd + 1;
    }
    if (fields == 0) {
        // A VSInput with no ATTRIB fields means the generator's format moved
        // under us; the pipeline would mis-bind silently, so say so here.
        HP_LOG_ERROR(kLog, "no ATTRIB fields found in the generated VSInput struct; "
                           "vertex input locations were not assigned");
    }
    return out;
}

/// Maps one reflected constant-buffer variable onto a material parameter type.
///
/// Diligent describes a variable by class (scalar / vector / matrix), basic
/// type and a rows/columns pair. Only the shapes `hp::ShaderParamType` carries
/// are accepted; everything else is refused so the caller can name it, exactly
/// as the slang path does.
bool mapDiligentParamType(const Diligent::ShaderCodeVariableDesc& variable, ShaderParamType& out) {
    // **A vector's width is `max(NumRows, NumColumns)`, and reading only one of
    // them was a real bug here.** Diligent's own header says "for shaders
    // compiled from GLSL, NumRows and NumColumns are swapped", and its SPIR-V
    // reader performs that swap only when it believes the source was HLSL --
    // which it cannot know for bytecode handed in directly. So a `float3`
    // arrives as 3x1 on this path and would arrive as 1x3 on another. A vector
    // has a 1 in the other slot either way, which is what makes taking the
    // larger correct rather than a guess.
    unsigned width = 0;
    switch (variable.Class) {
    case Diligent::SHADER_CODE_VARIABLE_CLASS_SCALAR:
        width = 1;
        break;
    case Diligent::SHADER_CODE_VARIABLE_CLASS_VECTOR:
        if (variable.NumRows != 1 && variable.NumColumns != 1) {
            return false;
        }
        width = std::max<unsigned>(variable.NumRows, variable.NumColumns);
        break;
    default:
        return false;
    }

    switch (variable.BasicType) {
    case Diligent::SHADER_CODE_BASIC_TYPE_FLOAT:
        break;
    case Diligent::SHADER_CODE_BASIC_TYPE_INT:
    case Diligent::SHADER_CODE_BASIC_TYPE_UINT:
        if (width != 1) {
            return false;
        }
        out = ShaderParamType::Int;
        return true;
    case Diligent::SHADER_CODE_BASIC_TYPE_BOOL:
        if (width != 1) {
            return false;
        }
        out = ShaderParamType::Bool;
        return true;
    default:
        return false;
    }
    switch (width) {
    case 1:
        out = ShaderParamType::Float;
        return true;
    case 2:
        out = ShaderParamType::Float2;
        return true;
    case 3:
        out = ShaderParamType::Float3;
        return true;
    case 4:
        out = ShaderParamType::Float4;
        return true;
    default:
        return false;
    }
}

/// Whether a compiled stage's bytecode names a resource (T0147.2).
///
/// **The used set, not the declared set**, for the same reason the module
/// signature is built that way: slang strips a resource nothing samples, so
/// this answers "does this shader actually read it" on the dev path and the
/// cooked path alike, from the one artefact both have.
bool shaderNamesResource(Diligent::IShader* shader, const char* wanted) {
    if (shader == nullptr) {
        return false;
    }
    const Diligent::Uint32 resources = shader->GetResourceCount();
    for (Diligent::Uint32 i = 0; i < resources; ++i) {
        Diligent::ShaderResourceDesc resource;
        shader->GetResourceDesc(i, resource);
        if (resource.Name != nullptr && std::strcmp(resource.Name, wanted) == 0) {
            return true;
        }
    }
    return false;
}

/// Reads the module's declared *parameters* back out of the compiled shader
/// (T0160.2).
///
/// **The path a shipped game takes**, where the bytecode came from a cooked
/// archive and no slang compile happened. Also the path taken when the compile
/// *did* happen and the module simply declares nothing, which costs one walk
/// over a handful of resources and keeps this code exercised on every
/// developer machine rather than only in a packaged build -- the failure mode
/// a fallback nobody runs always has.
///
/// Parameters only, since T0161: the layout's `textures` come from the same
/// walk that builds the module signature (`buildModuleSignatureDesc`), one
/// mechanism on both paths, so the binder and the signature cannot disagree.
void reflectFromShader(Diligent::IShader* shader, ShaderParamLayout& out) {
    if (shader == nullptr) {
        return;
    }
    const Diligent::Uint32 resources = shader->GetResourceCount();
    for (Diligent::Uint32 i = 0; i < resources; ++i) {
        Diligent::ShaderResourceDesc resource;
        shader->GetResourceDesc(i, resource);
        if (resource.Name == nullptr) {
            continue;
        }
        const std::string_view name{resource.Name};
        if (name != kShaderParamsBlock ||
            resource.Type != Diligent::SHADER_RESOURCE_TYPE_CONSTANT_BUFFER) {
            continue;
        }
        const Diligent::ShaderCodeBufferDesc* buffer = shader->GetConstantBufferDesc(i);
        if (buffer == nullptr) {
            HP_LOG_WARN(kLog,
                        "'{}' is present in the shader but carries no reflection; the material's "
                        "declared parameters will not be written",
                        kShaderParamsBlock);
            continue;
        }
        out.blockBytes = buffer->Size;
        for (Diligent::Uint32 v = 0; v < buffer->NumVariables; ++v) {
            const Diligent::ShaderCodeVariableDesc& variable = buffer->pVariables[v];
            if (variable.Name == nullptr) {
                continue;
            }
            ShaderParam param;
            param.name = variable.Name;
            if (!mapDiligentParamType(variable, param.type)) {
                HP_LOG_WARN(kLog,
                            "'{}.{}' has a type this engine cannot carry in a material; it keeps "
                            "whatever the shader initialises it to",
                            kShaderParamsBlock, param.name);
                continue;
            }
            param.offset = variable.Offset;
            // Diligent reports no per-variable size, and every shape accepted
            // above is exactly four bytes per component -- which is what the
            // slang path measures independently and what the writer needs.
            param.size = 4U * std::max<Diligent::Uint8>(variable.NumRows, variable.NumColumns);
            out.params.push_back(std::move(param));
        }
    }
    if (out.blockBytes > kShaderParamsMaxBytes) {
        HP_LOG_ERROR(kLog,
                     "a module declares a {}-byte '{}' block and the engine binds {} bytes; its "
                     "parameters are not written",
                     out.blockBytes, kShaderParamsBlock, kShaderParamsMaxBytes);
        out.params.clear();
        out.blockBytes = 0;
    }
}

} // namespace

SurfacePipeline::SurfacePipeline(Diligent::IRenderDevice* device,
                                 Diligent::IRenderStateCache* cache,
                                 Diligent::IDeviceContext* context, const CreateInfo& info)
    // `InitSignature = false`, then `CreateSignature()` here: the base
    // constructor would call `CreateCustomSignature` through the base vtable
    // and the engine's resources would silently not join the signature. This
    // is the pattern the flag exists for.
    : Diligent::PBR_Renderer(device, cache, context, info, /*InitSignature = */ false) {
    CreateSignature();

    // Every name the engine's signatures own, for the subtraction that
    // decides which of a module's resources are the *module's* (T0161). Read
    // back from the created signatures rather than restated, so a resource
    // added to `CreateCustomSignature` above -- or by DiligentFX upstream --
    // can never be double-declared into a module signature, which is a loud
    // PSO-creation failure.
    std::uint32_t sampledImages = 0;
    std::uint32_t immutableSamplers = 0;
    std::uint32_t constantBuffers = 0;
    for (const auto& signature : m_ResourceSignatures) {
        if (!signature) {
            continue;
        }
        const Diligent::PipelineResourceSignatureDesc& desc = signature->GetDesc();
        for (Diligent::Uint32 i = 0; i < desc.NumResources; ++i) {
            engineResourceNames_.emplace(desc.Resources[i].Name);
            if (desc.Resources[i].ResourceType == Diligent::SHADER_RESOURCE_TYPE_TEXTURE_SRV) {
                ++sampledImages;
            } else if (desc.Resources[i].ResourceType ==
                       Diligent::SHADER_RESOURCE_TYPE_CONSTANT_BUFFER) {
                ++constantBuffers;
            }
        }
        immutableSamplers += desc.NumImmutableSamplers;
        for (Diligent::Uint32 i = 0; i < desc.NumImmutableSamplers; ++i) {
            engineResourceNames_.emplace(desc.ImmutableSamplers[i].SamplerOrTextureName);
        }
    }
    // The counted budget, logged where it is decided (T0161.7). Every plain
    // material's SRB binds against exactly this; the four module slots and
    // the params buffer that used to sit here now cost only the materials
    // that declare them. The test suite pins these numbers so the budget
    // cannot drift silently -- which is how the T0160 audit had to *count*
    // them the first time, out of the source, after the fact.
    HP_LOG_DEBUG(kLog,
                 "base signature: {} sampled images, {} immutable samplers, {} constant "
                 "buffers",
                 sampledImages, immutableSamplers, constantBuffers);
}

void SurfacePipeline::CreateCustomSignature(Diligent::PipelineResourceSignatureDescX&& desc) {
    // **The height map** (T0141.7): the engine's own texture slot, beside
    // DiligentFX's seventeen rather than squatting in a disabled one --
    // reusing, say, the clearcoat slot would make T0143 a breaking change.
    // Mutable like the material textures; a one-slice `Texture2DArray`, the
    // shape every texture in this engine binds as. Wrap sampler, because a
    // height map tiles with the surface it displaces.
    //
    // Declared in the signature for **every** pipeline, used only by
    // permutations carrying `kPsoFlagHeightMap` -- the same superset pattern
    // as the seventeen: a signature resource an SPIR-V module never names
    // costs nothing at draw time.
    desc.AddResource(Diligent::SHADER_TYPE_PIXEL, kHeightMapVariable,
                     Diligent::SHADER_RESOURCE_TYPE_TEXTURE_SRV,
                     Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE);
    desc.AddImmutableSampler(Diligent::SHADER_TYPE_PIXEL, "g_HeightMap_sampler",
                             Diligent::Sam_LinearWrap);

    // **The engine's screen intermediates** (T0147), and their side of D35's
    // two-namespace line: a game *declares* its own resources and they are
    // reflected into a per-module signature; what the engine *feeds* is named
    // by the engine and lives here, where `buildModuleSignatureDesc`'s
    // subtraction leaves it alone for nothing.
    //
    // `VS_PS`, so a vertex hook can read scene depth too -- a billboard that
    // shrinks near geometry is a vertex-stage technique, and a stage flag on a
    // binding costs nothing (the same argument the sampler palette makes below).
    //
    // **No samplers of their own.** A module samples them through the palette
    // -- `HpSamplerLinearClamp` for colour, `HpSamplerPointClamp` for depth --
    // so the base signature's immutable-sampler count is unchanged and the
    // sampling vocabulary stays the one D35 decided on. Clamped is the point:
    // a screen-space read at the frame's edge must not wrap round to the other
    // side, which is exactly the artefact a wrap sampler produces in a
    // refraction.
    //
    // Declared for **every** pipeline and named by almost none: slang strips a
    // resource nothing samples, so a standard material's bytecode carries
    // neither, and a signature resource an SPIR-V module never names costs
    // nothing at draw time. That is the same superset pattern as the height map
    // above and DiligentFX's seventeen.
    desc.AddResource(Diligent::SHADER_TYPE_VS_PS, kSceneColourVariable,
                     Diligent::SHADER_RESOURCE_TYPE_TEXTURE_SRV,
                     Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE);
    desc.AddResource(Diligent::SHADER_TYPE_VS_PS, kSceneDepthVariable,
                     Diligent::SHADER_RESOURCE_TYPE_TEXTURE_SRV,
                     Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE);

    // **The sampler palette** (T0161.2, D35): the one piece of a game's
    // resource vocabulary that must exist before any module does, because
    // sampler *state* is the single thing SPIR-V cannot carry. An author
    // picks filtering by naming one of these in code -- the declarations are
    // in `HpMaterial.slang` -- so the choice travels inside the bytecode and
    // a cooked build needs no sampler metadata at all.
    //
    // Immutable samplers with no texture resource attached: Diligent gives
    // each its own set-layout binding and matches a shader's separate
    // `SamplerState` by name, the pattern the old per-slot samplers proved.
    // `VS_PS` so T0146's vertex modules inherit the palette without a base
    // signature change -- stage flags on a binding cost nothing.
    //
    // **What is deliberately no longer here** (T0161.6): `HpMaterialParams`
    // and the four `HpTexture0..3` slots. A module's resources live in the
    // *per-module* signature built from its own reflection
    // (`buildModuleSignatureDesc`), under the author's names -- and their
    // removal from this one is forced, not stylistic: the module signature
    // discovers resources by subtraction against this signature's names, so
    // a slot left here would shadow the module's copy for every legacy
    // module and the two declarations would otherwise collide loudly at PSO
    // creation. A plain glTF material's SRB carries four fewer images and
    // four fewer samplers than it did under T0160.
    // 8x anisotropy: the common desktop default. The palette is vocabulary,
    // not tuning -- an author-set level is the additive escape D35 records.
    // Indexed against the public name table so the signature and the refusal
    // message (`kShaderSamplerPalette`) cannot drift apart.
    const Diligent::SamplerDesc* paletteState[] = {
        &Diligent::Sam_LinearWrap, &Diligent::Sam_LinearClamp, &Diligent::Sam_PointWrap,
        &Diligent::Sam_PointClamp, &Diligent::Sam_Aniso8xWrap, &Diligent::Sam_Aniso8xClamp,
    };
    static_assert(std::size(paletteState) == std::size(kShaderSamplerPalette),
                  "every palette name needs its sampler state, in the same order");
    for (std::size_t i = 0; i < std::size(paletteState); ++i) {
        desc.AddImmutableSampler(Diligent::SHADER_TYPE_VS_PS, kShaderSamplerPalette[i],
                                 *paletteState[i]);
    }

    Diligent::PBR_Renderer::CreateCustomSignature(std::move(desc));
}

const ShaderParamLayout* SurfacePipeline::paramLayout(const char* customModule) const {
    if (customModule == nullptr || customModule[0] == '\0') {
        return nullptr;
    }
    const auto found = paramLayouts_.find(std::string{customModule});
    return found != paramLayouts_.end() ? &found->second : nullptr;
}

SurfacePipeline::ScreenInputUse SurfacePipeline::screenInputUse(const char* customModule) const {
    if (customModule == nullptr || customModule[0] == '\0') {
        // The standard material's own shader samples neither, and no game code
        // can change that -- so this is a fact rather than a cache miss.
        return ScreenInputUse::No;
    }
    const auto found = screenInputUse_.find(std::string{customModule});
    return found != screenInputUse_.end() ? found->second : ScreenInputUse::Unknown;
}

Diligent::IPipelineState* SurfacePipeline::pipeline(const Diligent::GraphicsPipelineDesc& graphics,
                                                    const PSOKey& key,
                                                    const char* customModule) {
    HP_PROFILE_ZONE();

    const std::size_t hash = cacheKey(graphics, key, customModule);
    if (const auto found = pipelines_.find(hash); found != pipelines_.end()) {
        return found->second;
    }

    Diligent::RefCntAutoPtr<Diligent::IPipelineState> built = build(graphics, key, customModule);
    // **Cached even when null.** A shader that will not compile does not compile
    // any better on the next frame, and retrying would turn one logged failure
    // into one per frame -- which is the trap T0141 records against the *draw*
    // path and which applies just as well here.
    pipelines_[hash] = built;
    return built;
}

void SurfacePipeline::configure(CreateInfo& info) {
    // Off because they belong to other tickets, and because each one that is on
    // demands resources the engine has no way to supply yet -- IBL wants a
    // precomputed environment, lights want a populated light buffer.
    info.EnableIBL = false;
    // **On, and the debug views are why.** Both were off, and both were invisible:
    // the rock test set packs a real ambient-occlusion map into its ORM red
    // channel, `ensureBindings` bound it, and `GetOcclusion` was never called
    // because `USE_AO_MAP` was 0 -- so `Occlusion` stayed at the material factor
    // of 1.0 and the channel rendered flat white. Loaded, packed, bound, ignored.
    //
    // Nothing in a shaded frame could have shown that. It was found the first
    // minute `SurfaceDebugView::Occlusion` existed, which is the argument for
    // having built it.
    info.EnableAO = true;
    info.EnableEmissive = true;
    info.EnableShadows = false;
    // **Sizes the frame attributes buffer**, which is
    // `CameraAttribs * 2 + renderer params + PBRLightAttribs * MaxLightCount`.
    // Zero here is what made `PSO_FLAG_USE_LIGHTS` pointless even when set.
    info.MaxLightCount = static_cast<Diligent::Uint32>(kMaxLights);
    // **This is 28.2.** The renderer creates white/black/default-normal
    // textures, which is what a material with no texture assigned samples. So
    // "entities with no material get a visible default" costs nothing here --
    // and turning it off is what would make an unassigned mesh invisible.
    info.CreateDefaultTextures = true;
    // **The sRGB decode happens in the shader, because the view cannot do it
    // here.** Base-colour and emissive textures are authored in sRGB; lighting
    // has to be done in linear. There are two places to convert: the texture
    // view (free, the sampler hardware does it) or the shader (`TO_LINEAR`,
    // a few instructions).
    //
    // The view is the better one and it is not available to us.
    // `GLTF_PBR_Renderer` reaches it through `GetPBRTextureSRV`, which creates
    // an sRGB view per slot and is **file-static in their .cpp** -- not merely
    // private, but invisible outside that translation unit. The glTF loader
    // creates `RGBA8_TYPELESS` textures, whose *default* shader view is
    // `RGBA8_UNORM`: linear, so a base-colour texture read through it is too
    // bright by the sRGB curve.
    //
    // `TEX_COLOR_CONVERSION_MODE_SRGB_TO_LINEAR` is DiligentFX's own name for
    // this exact situation -- their comment reads "should be used if the
    // textures are in sRGB color space, but the texture views are in linear
    // color space" -- and it costs an instruction per colour sample. T0134
    // recorded the gap; this closes it rather than carrying it forward, and
    // T0097 can still move the conversion into the view later without the
    // shader noticing.
    info.TexColorConversionMode =
        Diligent::PBR_Renderer::CreateInfo::TEX_COLOR_CONVERSION_MODE_SRGB_TO_LINEAR;

    // **Which slot of a glTF material feeds which slot of the renderer**, and
    // leaving it unset is a silent no-op rather than an error.
    //
    // `CreateInfo` fills this array with **-1**, and every consumer treats -1 as
    // "this renderer does not use that texture". So an unset array makes three
    // separate things quietly do nothing:
    //
    //   * `DefineMacros` skips the `BaseColorTextureAttribId` constants, so the
    //     shader cannot name its own texture attributes;
    //   * `ensureBindings` binds nothing, because every slot looks disabled;
    //   * `WritePBRMaterialShaderAttribs` writes no per-texture attributes at
    //     all, so the UV selectors and slices are never sent.
    //
    // None of the three logs anything in a release build, and none of them
    // mattered until something sampled a texture -- which is why this was found
    // by a shader failing to compile rather than by an image looking wrong. That
    // is the only reason it was found at all.
    //
    // `GLTF_PBR_Renderer` does this in a private wrapper struct around its
    // `CreateInfo`, so a renderer built on `PBR_Renderer` directly -- which is
    // what D26 chose -- has to do it here. The constants are the loader's own,
    // so the mapping stays correct by construction if `DefaultTextureAttributes`
    // is ever reordered.
    //
    // **Only the five glTF core textures are mapped.** The rest stay -1 on
    // purpose: D24 keeps clearcoat, sheen, anisotropy, iridescence, transmission
    // and volume off, so a mapping for them would claim support that the
    // pipeline flags, the signature and the shader all lack. Whichever ticket
    // turns one on adds its line here.
    auto& textureSlots = info.TextureAttribIndices;
    textureSlots[Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR] =
        Diligent::GLTF::DefaultBaseColorTextureAttribId;
    textureSlots[Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC] =
        Diligent::GLTF::DefaultMetallicRoughnessTextureAttribId;
    textureSlots[Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL] =
        Diligent::GLTF::DefaultNormalTextureAttribId;
    // Mapped although `EnableAO` and `EnableEmissive` are off below: the index
    // says where the data *lives*, not whether the renderer reads it. T0087 and
    // T0096 turn the features on and need no change here.
    textureSlots[Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_OCCLUSION] =
        Diligent::GLTF::DefaultOcclusionTextureAttribId;
    textureSlots[Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE] =
        Diligent::GLTF::DefaultEmissiveTextureAttribId;
    // **The engine is row-major and the shaders must be told so.**
    // `hp::float4x4` is Diligent's, documented in `hp/Math.hpp` as row-major and
    // multiplied left to right (`World * View * Proj`) -- and `PBR_Renderer`
    // defaults this to *false*, compiling its shaders for column-major.
    //
    // Left at the default, every matrix written into the frame constants is read
    // transposed. There is no error, no validation warning and no failed draw:
    // the geometry is simply transformed somewhere off screen, and the frame
    // comes back pure clear colour with `submitted == 1`. It cost an afternoon
    // and it is indistinguishable, from the outside, from a depth or culling bug.
    //
    // It also has to agree with the transpose flag passed to
    // `WritePBRPrimitiveShaderAttribs` in `SceneRenderer`, which is spelled
    // `!PackMatrixRowMajor` for exactly that reason -- set here, the two move
    // together instead of disagreeing silently.
    info.PackMatrixRowMajor = true;
}

Diligent::ITextureView* SurfacePipeline::defaultTexture(TEXTURE_ATTRIB_ID id) const {
    switch (id) {
        case TEXTURE_ATTRIB_ID_PHYS_DESC:
            // Roughness 1, metallic 0 — glTF packs roughness in green and
            // metallic in blue, and this texture is the 0x0000FF00 that says so.
            // **White here would make every untextured surface a mirror**, which
            // is the wrong answer in the loudest possible way.
            return m_pDefaultPhysDescSRV;
        case TEXTURE_ATTRIB_ID_NORMAL:
            return m_pDefaultNormalMapSRV;
        default:
            // White: the identity for a multiplied factor, which is what every
            // remaining slot is.
            return m_pWhiteTexSRV;
    }
}

Diligent::RefCntAutoPtr<Diligent::IPipelineState>
SurfacePipeline::build(const Diligent::GraphicsPipelineDesc& graphics, const PSOKey& key,
                       const char* customModule) {
    HP_PROFILE_ZONE();

    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso;

    // The macro set for this key. **Without it `PBR_Shading.fxh` does not
    // compile at all**: every optional feature in it is behind a macro that
    // `DefineMacros` defines, so this is the single reason the subclass exists
    // rather than a free function building pipelines beside the renderer.
    Diligent::ShaderMacroHelper macros = DefineMacros(key);

    // The engine's own permutation bits, appended after DiligentFX's.
    // `HP_UNSHADED` is the shader contract's name for it (`HpMaterial.slang`
    // defaults it to 0), so defining it here is what turns the user-defined
    // PSO bit into the compile-time branch the contract promises.
    macros.Add("HP_UNSHADED", (key.GetFlags() & kPsoFlagUnshaded) != 0 ? 1 : 0);
    macros.Add("HP_USE_HEIGHT_MAP", (key.GetFlags() & kPsoFlagHeightMap) != 0 ? 1 : 0);
    macros.Add("HP_TRIPLANAR", (key.GetFlags() & kPsoFlagTriplanar) != 0 ? 1 : 0);
    macros.Add("HP_CUSTOM_MATERIAL",
               (customModule != nullptr && customModule[0] != '\0') ? 1 : 0);

    // The generated interface structs, which both stages include by name --
    // exactly as `RenderPBR.psh` does. Reusing the base class's generators is
    // what keeps the two halves of `HpSurface.slang` agreeing about `VSOutput`
    // without either restating it, and what keeps the input layout and the
    // vertex struct describing the same thing.
    Diligent::InputLayoutDescX inputLayout;
    std::string vsInputStruct;
    GetVSInputStructAndLayout(key.GetFlags(), vsInputStruct, inputLayout);
    std::string vsOutputStruct = GetVSOutputStruct(key.GetFlags(), /*UseVkPointSize = */ false,
                                                   /*UsePrimitiveId = */ false);
    const bool custom = customModule != nullptr && customModule[0] != '\0';
    if (custom) {
        // The four `HpCustom` slots (T0146.4), for custom-material
        // permutations only -- a standard material's interpolator count is
        // exactly what it was before the vertex stage existed, which is half
        // of what makes its output byte-identical.
        vsOutputStruct = addCustomInterpolators(vsOutputStruct);
    }
    const std::string psOutputStruct = GetPSOutputStruct(key.GetFlags());

    // **Slang compiles both stages to SPIR-V, and Diligent receives bytecode**
    // (D28). Diligent never learns slang exists; its signature matches
    // resources by name, and the pinned submodule already prefers slang's
    // instance names (`SPIRVShaderResources.cpp`, DiligentCore #698). This
    // used to be a per-backend decision -- the OpenGL backend could not
    // consume slang output because slang's HLSL and GLSL emitters rename
    // every resource and GL binds by name, so GL kept Diligent's HLSL path
    // over the same bytes. D29 removed that backend, and with it the second
    // compiler and the constraint that pinned these shaders to the subset
    // both compilers accepted.
    //
    // The VSInput struct is annotated with [[vk::location]] because slang
    // numbers vertex inputs sequentially while the pipeline's layout numbers
    // them by semantic index -- measured as Tangent landing at location 3
    // against a layout expecting 7.
    const std::string vsInputStructSlang = addVkInputLocations(vsInputStruct);

    // The material's shader module, routed through a generated one-line
    // include (T0142.15). **Always emitted, even when empty**, and both
    // consumers require that: the content hasher walks `#include` lines
    // textually before preprocessing, so `HpSurface.slang`'s include of this
    // file must resolve for every pipeline -- and routing the module through
    // a literal include line, rather than an `#include MACRO`, is what lets
    // the hasher recurse into the module's own text, which is what makes the
    // SPIR-V cache key change when a game edits its shader.
    const std::string materialModule =
        custom ? std::string("#include \"") + customModule + "\"\n"
               : std::string("// no custom material module for this pipeline\n");
    // **`PSMainFooter.generated` is deliberately gone** (T0146.5). It existed
    // because `RenderPBR.psh` includes it unconditionally, and it was carried
    // here as long as any DiligentFX shader was compiled by this path. Neither
    // half of `HpSurface.slang` includes it, and nothing else is compiled here
    // any more, so a source nobody resolves would only rot.
    const Diligent::MemoryShaderSourceFileInfo generatedSources[] = {
        {"VSInputStruct.generated", vsInputStructSlang},
        {"VSOutputStruct.generated", vsOutputStruct},
        {"PSOutputStruct.generated", psOutputStruct},
        {"HpMaterialModule.generated", materialModule},
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
    // no other copy of them to fall back to. **The slang bridge consumes this
    // same compound factory** (142.4) -- one resolution order, two compilers.
    Diligent::IShaderSourceInputStreamFactory* sources[] = {generated, engine};
    Diligent::RefCntAutoPtr<Diligent::IShaderSourceInputStreamFactory> factory;
    Diligent::CreateCompoundShaderSourceFactory({sources, _countof(sources)}, &factory);
    if (!factory) {
        HP_LOG_ERROR(kLog, "could not combine the shader sources for this pipeline");
        return pso;
    }

    Diligent::RefCntAutoPtr<Diligent::IShader> vertexShader;
    Diligent::RefCntAutoPtr<Diligent::IShader> pixelShader;

    // The module's own resource signature (T0161.4), or null for the standard
    // material and for a module that declares nothing -- in which case the
    // pipeline carries exactly the signatures it did before T0161 existed.
    Diligent::RefCntAutoPtr<Diligent::IPipelineResourceSignature> moduleSignature;

    {
        // The permutation macros, forwarded exactly as `DefineMacros` produced
        // them (142.5). `ShaderMacroArray` keeps the helper's storage alive
        // for the duration of the calls below.
        const Diligent::ShaderMacroArray macroArray = macros;
        std::vector<SlangMacro> slangMacros;
        slangMacros.reserve(macroArray.Count);
        for (Diligent::Uint32 i = 0; i < macroArray.Count; ++i) {
            slangMacros.push_back(
                {macroArray.Elements[i].Name, macroArray.Elements[i].Definition});
        }

        // The module's declared parameters, reflected off the compile below
        // (T0160.2). Requested only for a custom module: the standard material
        // declares nothing, and asking would defeat the bytecode cache for
        // every pipeline in the engine. The resource-shape list rides the same
        // request (T0161.3): it is the one fact only slang can see, and it
        // feeds the dimension check below.
        //
        // **Reflection is of the program, not of a stage** -- one
        // `ProgramLayout` covers both entry points, so a texture declared for
        // the vertex hook is shaped-checked by the same walk as a pixel one.
        ShaderParamLayout layout;
        std::vector<SlangReflectedResource> slangResources;
        SlangReflectionRequest reflection;
        reflection.layout = &layout;
        reflection.resources = &slangResources;

        // **One compile, two entry points** (T0146.5). This was two compile
        // requests -- one for `RenderPBR.vsh`, one for `HpSurface.slang` --
        // until the engine owned the vertex main; now the file is parsed,
        // preprocessed and include-walked once for the pair.
        std::vector<std::uint8_t> vertexSpirv;
        std::vector<std::uint8_t> pixelSpirv;
        const SlangProgramStage programStages[] = {
            {kVertexEntryPoint, ShaderStage::Vertex, &vertexSpirv},
            {kPixelEntryPoint, ShaderStage::Pixel, &pixelSpirv},
        };
        std::string diagnostics;
        if (!compileSlangProgram(kSurfaceShader, programStages, std::size(programStages),
                                 slangMacros.data(), slangMacros.size(), factory, diagnostics,
                                 custom ? &reflection : nullptr)) {
            // **Logged here, on the compile attempt.** One line per failed
            // pipeline, never per draw (T0141); the null result is cached.
            HP_LOG_ERROR(kLog, "slang failed to compile '{}': {}", kSurfaceShader,
                         diagnostics.empty() ? "(no diagnostics)" : diagnostics);
            return pso;
        }
        if (!diagnostics.empty()) {
            // Warnings from a successful compile -- worth seeing, once,
            // because a shader has no other channel.
            HP_LOG_WARN(kLog, "slang compiled '{}' with diagnostics: {}", kSurfaceShader,
                        diagnostics);
        }

        const auto makeShader = [&](const std::vector<std::uint8_t>& spirv, const char* name,
                                    Diligent::SHADER_TYPE type, bool reflectParams)
            -> Diligent::RefCntAutoPtr<Diligent::IShader> {
            Diligent::RefCntAutoPtr<Diligent::IShader> shader;
            Diligent::ShaderCreateInfo shaderInfo;
            // **`ByteCode`, not `Source`** -- the two are mutually exclusive
            // and their size fields are a union, so setting the wrong one
            // sends SPIR-V down the text path.
            shaderInfo.ByteCode = spirv.data();
            shaderInfo.ByteCodeSize = spirv.size();
            // **`"main"`, although the slang entry points are `vsMain` and
            // `psMain`.** Slang renames an entry point to `main` when it emits
            // a per-entry-point SPIR-V module -- measured on the pinned
            // `slangc 2026.14.1` with `-target spirv-asm`, where the module
            // reads `OpEntryPoint Vertex %vsMain "main"`. The source name is
            // what the compile request is told; `main` is what the bytecode
            // carries.
            shaderInfo.EntryPoint = "main";
            // **The cooked-build half of reflection** (T0160.2). A shipped
            // game links no slang (D28's boundary table) and its bytecode
            // arrives from an archive, so the compile above reflects nothing
            // there. Diligent parses the constant-buffer layout straight out
            // of the SPIR-V, which is the runtime-side mechanism D28 named
            // when it argued for slang's reflection -- same bytes, so the
            // offsets cannot disagree. What it cannot see is the
            // `[HpRange]`/`[HpTooltip]` hints, which exist only in the source
            // and are wanted only by an editor, which has a compiler.
            //
            // Off for every other pipeline: Diligent's own comment says the
            // reflection "introduces some overhead", and nothing reads it.
            shaderInfo.LoadConstantBufferReflection = reflectParams;
            shaderInfo.Desc = {name, type, /*UseCombinedTextureSamplers = */ false};
            GetDevice()->CreateShader(shaderInfo, &shader);
            if (!shader) {
                HP_LOG_ERROR(kLog, "the device refused slang-compiled SPIR-V for '{}' ({})",
                             kSurfaceShader, name);
            }
            return shader;
        };

        // `reflectParams` on both stages for a custom module, because either
        // may be the one that carries the parameter block: slang strips it
        // from the stage that does not read it, and the cooked path has only
        // Diligent's reflection to read it back with.
        vertexShader = makeShader(vertexSpirv, "hp surface VS (slang)",
                                  Diligent::SHADER_TYPE_VERTEX, /*reflectParams = */ custom);
        pixelShader = makeShader(pixelSpirv, "hp surface PS (slang)",
                                 Diligent::SHADER_TYPE_PIXEL, /*reflectParams = */ custom);
        if (!vertexShader || !pixelShader) {
            return pso;
        }

        // -------------------------------------------------------------------
        // **The screen resources' validity rule** (T0147), enforced here
        // rather than written down and hoped for.
        //
        // `g_SceneColour` and `g_SceneDepth` are snapshots of the frame taken
        // between the opaque pass and the blend pass, so a material that is
        // not blended reads them *before* they were written -- last frame's
        // image at best, an uninitialised texture on the first. That is
        // precisely the class of bug that works on one driver and not the
        // next, so it is refused: the pipeline is not created, the renderer
        // substitutes the missing-material checkerboard, and this line says
        // which module and why.
        //
        // The alpha mode is already in the key, so the check costs nothing and
        // fires at the one moment both facts -- what the bytecode reads and
        // which pass it will draw in -- are in the same scope.
        // -------------------------------------------------------------------
        const bool readsScreen = shaderNamesResource(pixelShader, kSceneColourVariable) ||
                                 shaderNamesResource(pixelShader, kSceneDepthVariable) ||
                                 shaderNamesResource(vertexShader, kSceneColourVariable) ||
                                 shaderNamesResource(vertexShader, kSceneDepthVariable);
        if (custom) {
            // Recorded **before** the refusal below, deliberately: a module
            // that asked for the screen and was refused has still told the
            // renderer what it wants, which is what keeps the snapshot
            // decision exact rather than a frame late.
            screenInputUse_[std::string{customModule}] =
                readsScreen ? ScreenInputUse::Yes : ScreenInputUse::No;
        }
        if (readsScreen && key.GetAlphaMode() != Diligent::PBR_Renderer::ALPHA_MODE_BLEND) {
            HP_LOG_ERROR(kLog,
                         "shader module '{}' samples {} or {}, which only a material with "
                         "alphaMode: Blend may do -- the snapshot is taken after the opaque "
                         "pass, so an opaque read would see the previous frame. Set the "
                         "material's alpha mode to Blend, or stop sampling them",
                         custom ? customModule : "(the standard material)", kSceneColourVariable,
                         kSceneDepthVariable);
            return pso;
        }

        if (custom) {
            if (layout.params.empty()) {
                // Either the module declares no parameters -- common, and
                // this costs one cheap walk -- or the bytecode came from a
                // cooked archive and no compile happened. The two are
                // indistinguishable from here and want the same answer.
                //
                // **The vertex stage is asked second, and only if the pixel
                // stage had nothing** (T0146): slang strips the block from the
                // stage that does not read it, so a module whose parameters
                // only drive its vertex hook has an empty `HpMaterialParams`
                // in the pixel bytecode and a full one in the vertex bytecode.
                // Reading only the pixel stage there would silently write no
                // parameters at all on the cooked path.
                reflectFromShader(pixelShader, layout);
                if (layout.params.empty()) {
                    reflectFromShader(vertexShader, layout);
                }
            }

            // **The module's resources become its signature** (T0161.4), by
            // subtraction against the engine's own names -- see
            // `ModuleResourceSignature.hpp` for why the used set and why the
            // cache key is the resource set itself.
            //
            // **Both stages, because there is one module** (T0146): the same
            // `HpMaterial` implements `vertex()` and `baseColor()`, so a
            // texture it declares may be sampled in either, and a signature
            // built from the pixel stage alone would fail PSO creation by name
            // on the vertex stage's use of it.
            Diligent::PipelineResourceSignatureDescX moduleDesc;
            std::vector<std::string> moduleTextures;
            const std::vector<ModuleSignatureStage> signatureStages = {
                {pixelShader, Diligent::SHADER_TYPE_PIXEL},
                {vertexShader, Diligent::SHADER_TYPE_VERTEX},
            };
            ModuleSignatureRequest request;
            request.stages = &signatureStages;
            request.engineResources = &engineResourceNames_;
            // Empty exactly when no slang compile ran (the cooked path);
            // reflection on the dev path always lists the engine's own
            // resources at least. Null skips the dimension check there --
            // the dev build that cooked the archive already ran it.
            request.slangResources = slangResources.empty() ? nullptr : &slangResources;
            request.moduleName = customModule;
            request.bindingIndex = 1;
            request.policy.requireTexture2DArray = true;
            request.policy.allowBuffers = false;
            request.policy.onlyConstantBuffer = kShaderParamsBlock;
            if (!buildModuleSignatureDesc(request, moduleDesc, moduleTextures)) {
                // Refused, by name, in the log above. The null is cached and
                // the caller draws the checkerboard -- the same loud
                // convention as a compile failure, because that is what this
                // is: a module the engine cannot honestly bind.
                return pso;
            }
            layout.textures.reserve(moduleTextures.size());
            for (std::string& name : moduleTextures) {
                layout.textures.push_back(ShaderTextureSlot{std::move(name)});
            }

            if (moduleDesc.NumResources > 0) {
                const std::size_t key = moduleSignatureKey(moduleDesc);
                auto found = moduleSignatures_.find(key);
                if (found == moduleSignatures_.end()) {
                    moduleDesc.SetName("hp module resources");
                    Diligent::RefCntAutoPtr<Diligent::IPipelineResourceSignature> created;
                    GetDevice()->CreatePipelineResourceSignature(moduleDesc, &created);
                    if (!created) {
                        HP_LOG_ERROR(kLog,
                                     "the device refused the module resource signature for "
                                     "'{}'",
                                     customModule);
                        return pso;
                    }
                    found = moduleSignatures_.emplace(key, std::move(created)).first;
                }
                moduleSignature = found->second;
            }

            paramLayouts_[std::string{customModule}] = std::move(layout);
        }
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

    // Declared from WindingConvention.hpp -- see D33 and T0152.
    // Defaulting it happened to be correct and was still the bug: the next
    // pipeline (the shadow pass) would have defaulted it too, unexamined.
    psoInfo.GraphicsPipeline.RasterizerDesc.FrontCounterClockwise =
        hp::kFrontFaceCounterClockwise;

    // Blend state follows the alpha mode, the same way. Opaque and mask do not
    // blend; `Blend` is premultiplied alpha, matching what T0106.4 will want for
    // particles. **Depth writes stay on for blended geometry here** because
    // there is no OIT path in this engine yet -- T0045 owns the sorted
    // transparent queue, and turning writes off before that exists would trade
    // one wrong image for another.
    Diligent::RenderTargetBlendDesc& blend = psoInfo.GraphicsPipeline.BlendDesc.RenderTargets[0];
    if (key.GetAlphaMode() == Diligent::PBR_Renderer::ALPHA_MODE_BLEND) {
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
    // **The base class's signature first.** It already describes the frame,
    // primitive and material buffers and every engine texture slot, and
    // building a parallel one would mean `CreateResourceBinding` handing back
    // bindings this pipeline could not accept.
    for (auto& signature : m_ResourceSignatures) {
        psoInfo.AddSignature(signature);
    }
    // The module's own, at binding 1, when it declared anything (T0161). This
    // is where a resource duplicated across the two signatures would fail PSO
    // creation by name -- which is the guard that keeps the HpTexture0..3
    // migration honest: the slots *cannot* quietly exist in both.
    if (moduleSignature) {
        psoInfo.AddSignature(moduleSignature);
    }

    GetDevice()->CreateGraphicsPipelineState(psoInfo, &pso);
    if (!pso) {
        HP_LOG_ERROR(kLog, "the device refused the engine's surface pipeline");
    }
    return pso;
}

} // namespace hp
