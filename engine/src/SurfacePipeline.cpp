#include "SurfacePipeline.hpp"

#include "SlangCompiler.hpp"

#include <hp/Light.hpp>
#include <hp/Log.hpp>
#include <hp/Profiling.hpp>
#include <hp/ShaderSources.hpp>

#include <GLTFLoader.hpp>
#include <GraphicsTypesX.hpp>
#include <RenderDevice.h>
#include <ShaderSourceFactoryUtils.h>

#include <string>
#include <string_view>
#include <vector>

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
/// On the Slang path it is compiled by slang like our own shader is -- both
/// stages through one compiler, so the varying interface between them is
/// assigned by one set of rules rather than two compilers happening to agree.
///
/// **141.7's vertex displacement is what changes this**, and it is the only
/// thing that should: at that point the engine writes its own and this constant
/// moves to `HpSurface.slang`'s vertex half. Naming it here rather than
/// inlining the string is what makes that a one-line change.
constexpr const char* kVertexShader = "RenderPBR.vsh";

/// The engine's pixel shader — ours, and the whole point of D26. A `.slang`
/// file (D28), compiled by slang to SPIR-V at pipeline-build time.
constexpr const char* kPixelShader = "HpSurface.slang";

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
SurfacePipeline::build(const Diligent::GraphicsPipelineDesc& graphics, const PSOKey& key) {
    HP_PROFILE_ZONE();

    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso;

    // The macro set for this key. **Without it `PBR_Shading.fxh` does not
    // compile at all**: every optional feature in it is behind a macro that
    // `DefineMacros` defines, so this is the single reason the subclass exists
    // rather than a free function building pipelines beside the renderer.
    Diligent::ShaderMacroHelper macros = DefineMacros(key);

    // The engine's own permutation bits, appended after DiligentFX's.
    // `HP_UNSHADED` is the shader contract's name for it (`HpMaterial.fxh`
    // defaults it to 0), so defining it here is what turns the user-defined
    // PSO bit into the compile-time branch the contract promises.
    macros.Add("HP_UNSHADED", (key.GetFlags() & kPsoFlagUnshaded) != 0 ? 1 : 0);

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

    // Empty PSMainFooter, and it has to exist: `RenderPBR.vsh` includes it
    // unconditionally, and a missing include is a compile error rather than
    // an empty expansion.
    constexpr const char* kPsMainFooter = R"(
    PSOutput PSOut;
    PSOut.Color = OutColor;
    return PSOut;
)";
    const Diligent::MemoryShaderSourceFileInfo generatedSources[] = {
        {"VSInputStruct.generated", vsInputStructSlang},
        {"VSOutputStruct.generated", vsOutputStruct},
        {"PSOutputStruct.generated", psOutputStruct},
        {"PSMainFooter.generated", kPsMainFooter},
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

        const auto compileStage = [&](const char* file, ShaderStage stage, const char* name,
                                      Diligent::SHADER_TYPE type)
            -> Diligent::RefCntAutoPtr<Diligent::IShader> {
            Diligent::RefCntAutoPtr<Diligent::IShader> shader;
            std::vector<std::uint8_t> spirv;
            std::string diagnostics;
            if (!compileSlangToSpirv(file, stage, slangMacros.data(), slangMacros.size(), factory,
                                     spirv, diagnostics)) {
                // **Logged here, on the compile attempt.** One line per failed
                // pipeline, never per draw (T0141); the null result is cached.
                HP_LOG_ERROR(kLog, "slang failed to compile '{}': {}", file,
                             diagnostics.empty() ? "(no diagnostics)" : diagnostics);
                return shader;
            }
            if (!diagnostics.empty()) {
                // Warnings from a successful compile -- worth seeing, once,
                // because a shader has no other channel.
                HP_LOG_WARN(kLog, "slang compiled '{}' with diagnostics: {}", file, diagnostics);
            }
            Diligent::ShaderCreateInfo shaderInfo;
            // **`ByteCode`, not `Source`** -- the two are mutually exclusive
            // and their size fields are a union, so setting the wrong one
            // sends SPIR-V down the text path.
            shaderInfo.ByteCode = spirv.data();
            shaderInfo.ByteCodeSize = spirv.size();
            shaderInfo.EntryPoint = "main";
            shaderInfo.Desc = {name, type, /*UseCombinedTextureSamplers = */ false};
            GetDevice()->CreateShader(shaderInfo, &shader);
            if (!shader) {
                HP_LOG_ERROR(kLog, "the device refused slang-compiled SPIR-V for '{}'", file);
            }
            return shader;
        };

        vertexShader = compileStage(kVertexShader, ShaderStage::Vertex, "hp surface VS (slang)",
                                    Diligent::SHADER_TYPE_VERTEX);
        pixelShader = compileStage(kPixelShader, ShaderStage::Pixel, "hp surface PS (slang)",
                                   Diligent::SHADER_TYPE_PIXEL);
        if (!vertexShader || !pixelShader) {
            return pso;
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

    // `FrontCounterClockwise` stays at Diligent's default (false), and the
    // consequence is measured rather than assumed: with the engine's
    // left-handed view on Vulkan, a glTF front face reaches the rasteriser
    // wound counter-clockwise -- a hardware **back** face under this setting.
    // Setting the flag true would align hardware facing with geometric facing,
    // and was tried: it inverts `SV_IsFrontFace` for every existing surface,
    // the two-sided normal flip inverts with it, and the whole lit suite went
    // black because its test lights sit behind the quads and were calibrated
    // against the flip. Changing this is a real decision about the engine's
    // winding convention (recorded on T0141), not a one-line fix; until it is
    // taken, single-sided culling compensates in `SceneRenderer` by culling
    // `FRONT`, and T0086's shadow bias must revisit this before shipping.

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
