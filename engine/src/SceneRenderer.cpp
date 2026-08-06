#include <hp/SceneRenderer.hpp>

#include "SurfacePipeline.hpp"

#include <hp/Light.hpp>

#include <hp/Assets.hpp>
#include <hp/DepthConvention.hpp>
#include <hp/Log.hpp>
#include <hp/Material.hpp>
#include <hp/Profiling.hpp>

#include <GLTFBuilder.hpp>
#include <GLTFLoader.hpp>
#include <GraphicsTypesX.hpp>
#include <GraphicsUtilities.h>
#include <MapHelper.hpp>
#include <RefCntAutoPtr.hpp>
#include <RenderDevice.h>

#include <GLTF_PBR_Renderer.hpp>
#include <PBR_Renderer.hpp>

#include <array>
#include <cstring>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// The shader-side structures, included the way Diligent's own consumers do it:
// the `.fxh` files are HLSL that compiles as C++, and the namespace is supplied
// by the includer rather than by the file.
namespace Diligent {
namespace HLSL {
#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/PBR/public/PBR_Structures.fxh"
#include "Shaders/PBR/private/RenderPBR_Structures.fxh"
} // namespace HLSL
} // namespace Diligent

namespace hp {
namespace {

const LogCategory kLog("render.scene");

/// Must agree with `FrameTargets`' mapping, which is file-local there.
///
/// Duplicated rather than shared because exposing it would put a Diligent enum
/// in a public header for no other reason. If these two ever disagree the
/// pipeline state is created for a format the target does not have, and the
/// device rejects the draw — loudly, at least, rather than silently.
Diligent::TEXTURE_FORMAT toDiligentFormat(TargetFormat format) {
    switch (format) {
    case TargetFormat::Colour:
        return Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
    case TargetFormat::ColourHDR:
        return Diligent::TEX_FORMAT_RGBA16_FLOAT;
    case TargetFormat::Depth:
        return Diligent::TEX_FORMAT_D32_FLOAT;
    }
    return Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
}

/// The shading features currently enabled, which is the whole of them.
///
/// **This is the adoption boundary from D24, stated in code** — promoted by
/// T0134 from the stopgap T0028 left here. `RenderInfo::Flags` in DiligentFX is
/// a **mask**: it can only remove flags, never add one, so the boundary is
/// expressed as what survives rather than as what is switched off.
///
/// Everything excluded belongs to a ticket that has not landed, and **each bit
/// comes off when its ticket does** rather than being enabled speculatively:
///
/// | Flag | Ticket |
/// |---|---|
/// | `PSO_FLAG_USE_LIGHTS` | T0079 — also raises `MaxLightCount` and sets `Renderer.LightCount` |
/// | `PSO_FLAG_USE_IBL` | T0087 — also `EnableIBL` and the precomputed cubemaps |
/// | `PSO_FLAG_ENABLE_SHADOWS` | T0086 — also `EnableShadows` and `MaxShadowCastingLightCount` |
/// | `PSO_FLAG_ENABLE_TONE_MAPPING` | T0096 — **and D24 recommends it stays off**; tonemapping belongs in a pass over an HDR target, because the in-shader path tonemaps per draw before blending and leaves bloom nothing to read |
/// | `PSO_FLAG_COMPUTE_MOTION_VECTORS` | T0111/T0096 — `PrevCamera` is already written every frame, so only the flag is missing |
///
/// **Three of these are why every mesh currently renders pure black**: no
/// lights, no IBL, no emissive. That is measured, not assumed, and it is
/// recorded on T0079.
constexpr Diligent::PBR_Renderer::PSO_FLAGS kFeatureMask =
    static_cast<Diligent::PBR_Renderer::PSO_FLAGS>(
        Diligent::PBR_Renderer::PSO_FLAG_VERTEX_ATTRIBS |
        Diligent::PBR_Renderer::PSO_FLAG_DEFAULT_TEXTURES |
        Diligent::PBR_Renderer::PSO_FLAG_USE_LIGHTS |
        // A material asset's UV transforms (T0060) are shader math behind this
        // flag; without it the attribs are written and silently ignored. Set
        // per material, only when a transform is not the identity.
        Diligent::PBR_Renderer::PSO_FLAG_ENABLE_TEXCOORD_TRANSFORM |
        // The engine's unshaded permutation (T0141.12/141.15) -- the
        // missing-material fallback and `Material::unlit` both need pixels
        // scene lights cannot dim.
        SurfacePipeline::kPsoFlagUnshaded |
        // Parallax occlusion over a material's height map (T0141.7).
        SurfacePipeline::kPsoFlagHeightMap |
        // World-space triplanar projection (T0141.8).
        SurfacePipeline::kPsoFlagTriplanar);

/// Features the engine **turns on**, as opposed to what the mask above permits.
///
/// **These are two different things and conflating them costs an afternoon.**
/// The mask can only ever *remove* flags — T0134 recorded that in as many words
/// — so adding `PSO_FLAG_USE_LIGHTS` to `kFeatureMask` and stopping there
/// changes nothing at all: the flag has to be present in the accumulated set
/// before the AND can preserve it. It was written that way first, and the frame
/// came back exactly as black as before, with every counter still agreeing that
/// a draw had been issued.
///
/// So a feature that is not derived from the material or the vertex layout —
/// lights, and later IBL and shadows — is enabled **here**, and the mask is what
/// keeps everything else off.
constexpr Diligent::PBR_Renderer::PSO_FLAGS kEnabledFeatures =
    Diligent::PBR_Renderer::PSO_FLAG_USE_LIGHTS;

// --- material assets on the GPU (T0141.12) ----------------------------------
//
// A `hp::Material` is authoring data; what the renderer binds is a
// `GLTF::Material`, because that is what `WritePBRMaterialShaderAttribs` reads
// and reimplementing its packing is how a layout drifts silently (D24). The
// conversion below is the whole translation, in one place.

/// The five texture slots a material asset carries, in renderer terms.
struct MaterialTextureSlot {
    /// Which renderer slot this feeds.
    Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID slot;

    /// Index in the glTF attribute table -- must match the
    /// `TextureAttribIndices` mapping `SurfacePipeline::configure` sets up.
    Diligent::Uint32 attribIndex;

    /// The texture the material names, default when none.
    Guid texture;

    /// Which UV channel the slot samples with.
    std::uint8_t uv;
};

/// The material's slots, in one place so conversion and binding cannot
/// disagree about which GUID feeds which slot.
std::array<MaterialTextureSlot, 5> materialTextureSlots(const Material& material) {
    return {{
        {Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR,
         Diligent::GLTF::DefaultBaseColorTextureAttribId, material.baseColourTexture,
         material.baseColourUv},
        {Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC,
         Diligent::GLTF::DefaultMetallicRoughnessTextureAttribId,
         material.metallicRoughnessTexture, material.metallicRoughnessUv},
        {Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL,
         Diligent::GLTF::DefaultNormalTextureAttribId, material.normalTexture,
         material.normalUv},
        {Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_OCCLUSION,
         Diligent::GLTF::DefaultOcclusionTextureAttribId, material.occlusionTexture,
         material.occlusionUv},
        {Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE,
         Diligent::GLTF::DefaultEmissiveTextureAttribId, material.emissiveTexture,
         material.emissiveUv},
    }};
}

Diligent::TEXTURE_ADDRESS_MODE toDiligentWrap(TextureWrap wrap) {
    switch (wrap) {
    case TextureWrap::Repeat:
        return Diligent::TEXTURE_ADDRESS_WRAP;
    case TextureWrap::Mirror:
        return Diligent::TEXTURE_ADDRESS_MIRROR;
    case TextureWrap::Clamp:
        return Diligent::TEXTURE_ADDRESS_CLAMP;
    }
    return Diligent::TEXTURE_ADDRESS_WRAP;
}

/// Whether a channel's transform actually transforms anything.
///
/// This is what decides `PSO_FLAG_ENABLE_TEXCOORD_TRANSFORM` per material: the
/// flag is per-pipeline shader math, and paying it on every material because
/// one tiles differently would be the wrong trade.
bool isIdentity(const UvChannel& channel) {
    return channel.scale.x == 1.0F && channel.scale.y == 1.0F && channel.offset.x == 0.0F &&
           channel.offset.y == 0.0F && channel.rotation == 0.0F;
}

/// Converts a material asset into the renderer's vocabulary.
///
/// @param material the authored material.
/// @param placeholderBaseColour activates the base-colour slot even though the
///        material names no texture there -- the missing-material fallback
///        binds the checkerboard placeholder into it (T0141.12), and without an
///        active slot the shader's UV selector stays -1 and the texture is
///        never sampled.
/// @returns the converted material. Texture *ids* are all -1: they index a
///          model's own texture array, which a material asset does not have --
///          binding happens by GUID against the pool instead.
Diligent::GLTF::Material toGltfMaterial(const Material& material, bool placeholderBaseColour) {
    Diligent::GLTF::Material gltf;

    Diligent::GLTF::Material::ShaderAttribs& basic = gltf.Attribs;
    basic.BaseColorFactor =
        float4{material.baseColour.x, material.baseColour.y, material.baseColour.z,
               material.baseColour.w};
    basic.EmissiveFactor = float3{material.emissive.x, material.emissive.y, material.emissive.z};
    basic.MetallicFactor = material.metallic;
    basic.RoughnessFactor = material.roughness;
    basic.NormalScale = material.normalScale;
    basic.OcclusionFactor = material.occlusionStrength;
    // `AlphaMode` documents itself as value-matched to Diligent's enum; this is
    // where that promise is spent, so it is asserted here rather than trusted.
    static_assert(static_cast<int>(AlphaMode::Opaque) ==
                  Diligent::GLTF::Material::ALPHA_MODE_OPAQUE);
    static_assert(static_cast<int>(AlphaMode::Mask) == Diligent::GLTF::Material::ALPHA_MODE_MASK);
    static_assert(static_cast<int>(AlphaMode::Blend) ==
                  Diligent::GLTF::Material::ALPHA_MODE_BLEND);
    basic.AlphaMode = static_cast<int>(material.alphaMode);
    basic.AlphaCutoff = material.alphaCutoff;
    // The data-model truth. The *shader* keys unshaded off the PSO bit rather
    // than this field, but an inspector or a debugger reading the buffer should
    // see the honest value.
    basic.Workflow = material.unlit ? Diligent::GLTF::Material::PBR_WORKFLOW_UNLIT
                                    : Diligent::GLTF::Material::PBR_WORKFLOW_METALL_ROUGH;
    gltf.DoubleSided = material.doubleSided;
    // The engine's scalar parameters ride in `CustomData` (T0141.7/141.8) --
    // the per-material channel DiligentFX reserves for exactly this, so no
    // new constant buffer and no layout of our own. `x` is the parallax depth
    // (read under `HP_USE_HEIGHT_MAP`), `y` the triplanar tiling (read under
    // `HP_TRIPLANAR`).
    basic.CustomData.x = material.heightScale;
    basic.CustomData.y = material.triplanarScale;

    Diligent::GLTF::MaterialBuilder builder{gltf};
    for (const MaterialTextureSlot& slot : materialTextureSlots(material)) {
        const bool active = slot.texture.isValid() ||
                            (placeholderBaseColour &&
                             slot.slot == Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR);
        if (!active) {
            // Inactive slot: UV selector stays -1, which the shader reads as
            // "no texture here, use the factor". The renderer's default texture
            // is still bound, and this is what makes that binding inert.
            continue;
        }
        Diligent::GLTF::Material::TextureShaderAttribs& attribs =
            builder.GetTextureAttrib(slot.attribIndex);
        const UvChannel& channel = slot.uv == 0 ? material.uv0 : material.uv1;
        attribs.SetUVSelector(slot.uv);
        attribs.SetWrapUMode(toDiligentWrap(channel.wrapU));
        attribs.SetWrapVMode(toDiligentWrap(channel.wrapV));
        // Composed exactly as the glTF loader composes KHR_texture_transform:
        // scale times rotation (negated -- UV rotation is counter-clockwise,
        // which is a clockwise rotation of the image), offset as the bias.
        attribs.UVScaleAndRotation = Diligent::float2x2::Scale(channel.scale.x, channel.scale.y) *
                                     Diligent::float2x2::Rotation(-channel.rotation);
        attribs.UBias = channel.offset.x;
        attribs.VBias = channel.offset.y;
        builder.SetTextureId(slot.attribIndex, -1);
    }
    builder.Finalize();

    return gltf;
}

} // namespace

/// Everything Diligent lives here, and nothing above this line names it.
struct SceneRenderer::Impl {
    Diligent::IRenderDevice* device = nullptr;

    /// **`SurfacePipeline`, not `PBR_Renderer`** (T0141.10, D26). It *is* a
    /// `PBR_Renderer` -- every buffer, SRB and helper call below is the base
    /// class's and unchanged -- but its pipelines run the engine's own pixel
    /// shader instead of DiligentFX's private one. That single substitution is
    /// what makes parallax, triplanar and unshaded reachable at all.
    std::unique_ptr<SurfacePipeline> renderer;

    /// Camera and frame-wide shader data. One buffer, rewritten per frame.
    Diligent::RefCntAutoPtr<Diligent::IBuffer> frameAttribs;

    /// Target formats, depth state and topology, **built with the engine's depth
    /// convention**. This is the whole reason `PBR_Renderer` is driven directly
    /// rather than `GLTF_PBR_Renderer`, which builds this itself and leaves the
    /// comparison at `LESS`.
    ///
    /// Held rather than handed to `GetPsoCacheAccessor`: that accessor's cache
    /// builds pipelines from *DiligentFX's* shaders, which is precisely what
    /// D26 replaced. `SurfacePipeline::pipeline` takes this per call and caches
    /// on it together with the PSO key.
    Diligent::GraphicsPipelineDesc graphics;

    /// One SRB per material per model, keyed by the mesh asset's GUID.
    ///
    /// Cached because an SRB is expensive to create and a model's materials do
    /// not change between frames. Keyed on the GUID rather than the pointer so a
    /// hot reload (T0058) that replaces the asset under the same identity
    /// invalidates the right entry.
    struct ModelBindings {
        std::vector<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>> material;
        const Diligent::GLTF::Model* builtFor = nullptr;
    };
    std::unordered_map<Guid, ModelBindings> bindings;

    /// One material *asset*, converted and bound (T0141.12).
    ///
    /// Everything a draw needs when a surface's slot resolves to something
    /// other than the model's imported material: the converted attribs, the
    /// SRB with the asset's textures (or the placeholder, or the renderer
    /// defaults), and the PSO bits the material adds.
    struct MaterialBinding {
        /// The converted attribs `WritePBRMaterialShaderAttribs` reads.
        Diligent::GLTF::Material gltf;

        /// The SRB binding this material's textures.
        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;

        /// What it was built from. **The rebuild trigger**: when the pool
        /// returns a different object under the same GUID -- a hot reload
        /// (T0058) -- the binding is rebuilt. A texture replaced *behind* an
        /// unchanged material object is not detected here; that is T0058's
        /// invalidation problem and is recorded there rather than half-solved
        /// with a per-frame pool walk.
        std::shared_ptr<Material> source;

        /// Keeps the bound textures alive as long as the SRB names them.
        std::vector<std::shared_ptr<TextureAsset>> textures;

        /// PSO bits this material adds: unshaded, the UV-transform math.
        Diligent::PBR_Renderer::PSO_FLAGS extraFlags = Diligent::PBR_Renderer::PSO_FLAG_NONE;
    };

    /// Assigned materials, keyed by asset GUID.
    std::unordered_map<Guid, MaterialBinding> materials;

    /// The one missing-material binding (T0141.12): `missingMaterial()` with
    /// the checkerboard bound as its base colour. Built once, on first need --
    /// every missing slot in the scene draws through this.
    std::optional<MaterialBinding> fallback;

    /// The checkerboard placeholder (T0023.6), pinned for the SRBs that bind it.
    std::shared_ptr<TextureAsset> placeholder;

    /// Missing materials already reported, so the log line fires **once per
    /// GUID** rather than per draw per frame -- the T0141 rule: report on the
    /// transition, never from the draw path.
    std::unordered_set<Guid> reportedMissing;

    /// Scratch, reused across frames so a draw does not allocate.
    Diligent::GLTF::ModelTransforms transforms;

    /// The lights chosen for the current object. Reused across draws, because
    /// selection runs once per drawable thing in the scene.
    LightList selected;

    /// Creates the per-material SRBs for a model, once.
    ///
    /// @param guid the mesh asset's identity, used as the cache key.
    /// @param model the model whose materials need binding.
    /// @returns the bindings, or nullptr if they could not be created.
    ModelBindings* ensureBindings(Guid guid, const Diligent::GLTF::Model& model);

    /// Converts and binds one material asset, once per asset (T0141.12).
    ///
    /// @param context the context, for the texture state transitions.
    /// @param pool where the material's textures are resolved from.
    /// @param resolved an `Assigned` slot resolution.
    /// @returns the binding, or nullptr when the SRB could not be created.
    MaterialBinding* ensureMaterialBinding(Diligent::IDeviceContext* context,
                                           const AssetPool& pool,
                                           const ResolvedMaterial& resolved);

    /// The missing-material binding: `missingMaterial()` plus the checkerboard.
    /// @param context the context, for the texture state transitions.
    /// @returns the binding, or nullptr when it could not be built.
    MaterialBinding* ensureFallbackBinding(Diligent::IDeviceContext* context);

    /// Does the actual conversion and SRB construction for the two above.
    /// @param context the context, for the texture state transitions.
    /// @param material the authored material.
    /// @param pool where texture GUIDs resolve; null for the fallback, whose
    ///        material names none.
    /// @param placeholderBaseColour bind the checkerboard as the base colour.
    /// @param out receives the binding.
    /// @returns whether the binding is usable.
    bool buildMaterialBinding(Diligent::IDeviceContext* context, const Material& material,
                              const AssetPool* pool, bool placeholderBaseColour,
                              MaterialBinding& out);

    /// Draws one model at one transform.
    /// @param context the context to record into.
    /// @param model the model to draw.
    /// @param item the draw item supplying the world transform and the
    ///        per-surface material overrides.
    /// @param guid the mesh asset's identity, for the binding cache.
    /// @param pool where material and texture overrides resolve from.
    /// @returns whether anything was submitted.
    bool drawModel(Diligent::IDeviceContext* context, const Diligent::GLTF::Model& model,
                   const DrawItem& item, Guid guid, const AssetPool& pool);
};

SceneRenderer::Impl::ModelBindings*
SceneRenderer::Impl::ensureBindings(Guid guid, const Diligent::GLTF::Model& model) {
    auto it = bindings.find(guid);
    // Rebuild when the model object behind the GUID changed, which is what a hot
    // reload (T0058) looks like from here: same identity, different data. An SRB
    // built against the old model's textures would bind freed views.
    if (it != bindings.end() && it->second.builtFor == &model) {
        return &it->second;
    }

    ModelBindings built;
    built.builtFor = &model;
    built.material.resize(model.Materials.size());

    for (std::size_t i = 0; i < model.Materials.size(); ++i) {
        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
        renderer->CreateResourceBinding(&srb);
        if (!srb) {
            HP_LOG_ERROR(kLog, "could not create a shader resource binding for material {}", i);
            return nullptr;
        }
        renderer->InitCommonSRBVars(srb, frameAttribs);

        // **Every slot is bound, whether the material fills it or not**, and the
        // `else` branch here is the whole point rather than an afterthought.
        //
        // The pipeline key below enables the colour, normal and
        // physical-descriptor maps for *every* material -- one permutation
        // instead of one per texture combination -- so the shader an untextured
        // material runs still declares all three. An unbound texture array
        // samples **zero**, so skipping the binding turns an untextured surface
        // black, with the light present and the material read correctly. That
        // was measured, and it is what parked 141.11 for a commit.
        //
        // DiligentFX does exactly this in `GLTF_PBR_Renderer::InitMaterialSRB`;
        // this engine cannot call it because that is a `GLTF_PBR_Renderer`
        // method and `SurfacePipeline` deliberately is not one (D26).
        //
        // The one thing their helper adds that this does not is `GetPBRTextureSRV`'s
        // per-slot **colour-space view**. It is file-static in their .cpp and
        // unreachable, so the conversion happens in the shader instead:
        // `TexColorConversionMode` is set to `SRGB_TO_LINEAR` below, which is the
        // mode DiligentFX documents for exactly this case -- sRGB texture data
        // behind a UNORM view. The glTF loader creates `RGBA8_TYPELESS` textures
        // whose default shader view is `RGBA8_UNORM`, so that is the case we are in.
        const Diligent::GLTF::Material& material = model.Materials[i];
        const auto& indices = renderer->GetSettings().TextureAttribIndices;
        for (int id = 0; id < Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT; ++id) {
            const auto slot = static_cast<Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID>(id);
            const int attrib = indices[static_cast<std::size_t>(id)];
            if (attrib < 0) {
                continue;
            }

            Diligent::ITextureView* view = nullptr;
            if (const int texture = material.GetTextureId(attrib); texture >= 0) {
                if (Diligent::ITexture* tex = model.GetTexture(texture)) {
                    view = tex->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
                }
            }
            if (view == nullptr) {
                view = renderer->defaultTexture(slot);
            }
            if (view == nullptr) {
                // Only reachable with `CreateDefaultTextures` off, which this
                // renderer never does. Logged rather than skipped silently,
                // because the consequence downstream is a black surface with no
                // other diagnostic.
                HP_LOG_ERROR(kLog, "no texture and no default for slot {} of material {}", id, i);
                continue;
            }
            renderer->SetMaterialTexture(srb, view, slot);
        }

        // The engine's height slot (T0141.7): a glTF material has no height
        // map, so every model SRB binds the flat white default.
        if (Diligent::IShaderResourceVariable* height = srb->GetVariableByName(
                Diligent::SHADER_TYPE_PIXEL, SurfacePipeline::kHeightMapVariable)) {
            height->Set(renderer->GetWhiteTexSRV());
        }

        built.material[i] = std::move(srb);
    }

    bindings[guid] = std::move(built);
    return &bindings[guid];
}

bool SceneRenderer::Impl::buildMaterialBinding(Diligent::IDeviceContext* context,
                                               const Material& material, const AssetPool* pool,
                                               bool placeholderBaseColour, MaterialBinding& out) {
    HP_PROFILE_ZONE();

    out.gltf = toGltfMaterial(material, placeholderBaseColour);

    renderer->CreateResourceBinding(&out.srb);
    if (!out.srb) {
        HP_LOG_ERROR(kLog, "could not create a shader resource binding for a material asset");
        return false;
    }
    renderer->InitCommonSRBVars(out.srb, frameAttribs);

    // Textures the SRB will bind but nothing has transitioned yet. Model
    // textures are transitioned by the glTF loader; pool textures and the
    // placeholder have no context at load time, so their first binding is
    // where the transition happens. `OldState = UNKNOWN` uses the state
    // Diligent tracks, so a texture two materials share transitions once.
    std::vector<Diligent::StateTransitionDesc> barriers;

    const auto bindSlot = [&](Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID slot,
                              Diligent::ITextureView* view) {
        if (view == nullptr) {
            view = renderer->defaultTexture(slot);
        }
        if (view == nullptr) {
            // Only reachable with `CreateDefaultTextures` off, which this
            // renderer never does; see `ensureBindings`.
            HP_LOG_ERROR(kLog, "no texture and no default for material slot {}",
                         static_cast<int>(slot));
            return;
        }
        renderer->SetMaterialTexture(out.srb, view, slot);
    };

    for (const MaterialTextureSlot& slot : materialTextureSlots(material)) {
        Diligent::ITextureView* view = nullptr;

        std::shared_ptr<TextureAsset> texture;
        if (slot.texture.isValid() && pool != nullptr) {
            texture = pool->get<TextureAsset>(slot.texture);
        }
        if (texture && texture->valid()) {
            view = texture->shaderResource();
            barriers.emplace_back(texture->texture(), Diligent::RESOURCE_STATE_UNKNOWN,
                                  Diligent::RESOURCE_STATE_SHADER_RESOURCE,
                                  Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE);
            out.textures.push_back(std::move(texture));
        } else if (slot.texture.isValid()) {
            // Named and not in the pool: the texture-level miss, T0023.6's
            // case rather than T0060.10's, and it gets the same checkerboard.
            if (placeholder && placeholder->valid()) {
                view = placeholder->shaderResource();
            }
        } else if (placeholderBaseColour &&
                   slot.slot == Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR &&
                   placeholder && placeholder->valid()) {
            // The missing-material fallback: the checkerboard *is* the base
            // colour map (T0141.12).
            view = placeholder->shaderResource();
        }

        bindSlot(slot.slot, view);
    }

    // **The engine's own height slot, outside the seventeen** (T0141.7).
    // Bound by name because `SetMaterialTexture` only knows Diligent's
    // attrib ids. White is the no-map binding and means "flat": a constant
    // height field gives the parallax march a zero offset, so an unbound-slot
    // permutation and a flat material agree by construction.
    {
        Diligent::ITextureView* view = nullptr;
        std::shared_ptr<TextureAsset> texture;
        if (material.heightTexture.isValid() && pool != nullptr) {
            texture = pool->get<TextureAsset>(material.heightTexture);
        }
        if (texture && texture->valid()) {
            view = texture->shaderResource();
            barriers.emplace_back(texture->texture(), Diligent::RESOURCE_STATE_UNKNOWN,
                                  Diligent::RESOURCE_STATE_SHADER_RESOURCE,
                                  Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE);
            out.textures.push_back(std::move(texture));
        } else if (material.heightTexture.isValid() && placeholder && placeholder->valid()) {
            // Named and not loaded: the checkerboard, uniformly with every
            // other texture-level miss (T0023.6). As a height field it renders
            // as a waffle of square bumps -- loud, which is the point.
            view = placeholder->shaderResource();
        }
        if (view == nullptr) {
            view = renderer->GetWhiteTexSRV();
        }
        if (Diligent::IShaderResourceVariable* variable = out.srb->GetVariableByName(
                Diligent::SHADER_TYPE_PIXEL, SurfacePipeline::kHeightMapVariable)) {
            variable->Set(view);
        }
        if (material.heightTexture.isValid() && !material.triplanar) {
            // Triplanar wins when both are set: there are no UVs for the
            // parallax march to displace, and compiling it in would pay for a
            // loop whose result is discarded.
            out.extraFlags |= SurfacePipeline::kPsoFlagHeightMap;
        }
    }

    if (material.triplanar) {
        out.extraFlags |= SurfacePipeline::kPsoFlagTriplanar;
    }

    if (!barriers.empty() && context != nullptr) {
        context->TransitionResourceStates(static_cast<Diligent::Uint32>(barriers.size()),
                                          barriers.data());
    }

    if (material.unlit) {
        out.extraFlags |= SurfacePipeline::kPsoFlagUnshaded;
    }
    // The UV-transform math is per pipeline; pay for it only when a bound
    // channel actually transforms. An unused channel's settings are inert by
    // construction, so only channels a slot selects are consulted.
    for (const MaterialTextureSlot& slot : materialTextureSlots(material)) {
        if (!slot.texture.isValid()) {
            continue;
        }
        const UvChannel& channel = slot.uv == 0 ? material.uv0 : material.uv1;
        if (!isIdentity(channel)) {
            out.extraFlags |= Diligent::PBR_Renderer::PSO_FLAG_ENABLE_TEXCOORD_TRANSFORM;
            break;
        }
    }

    return true;
}

SceneRenderer::Impl::MaterialBinding*
SceneRenderer::Impl::ensureMaterialBinding(Diligent::IDeviceContext* context,
                                           const AssetPool& pool,
                                           const ResolvedMaterial& resolved) {
    auto it = materials.find(resolved.guid);
    // Rebuilt when the pool holds a different object under the same GUID --
    // same identity, new data, which is what a hot reload (T0058) looks like
    // from here. Mirrors `ensureBindings`' rule for models.
    if (it != materials.end() && it->second.source == resolved.material) {
        return &it->second;
    }

    MaterialBinding built;
    if (!buildMaterialBinding(context, *resolved.material, &pool, /*placeholderBaseColour=*/false,
                              built)) {
        return nullptr;
    }
    built.source = resolved.material;

    MaterialBinding& stored = materials[resolved.guid];
    stored = std::move(built);
    return &stored;
}

SceneRenderer::Impl::MaterialBinding*
SceneRenderer::Impl::ensureFallbackBinding(Diligent::IDeviceContext* context) {
    if (fallback) {
        return &*fallback;
    }

    MaterialBinding built;
    // `missingMaterial()` decides what the fallback *is* (unlit, magenta,
    // opaque -- T0060.10); this only puts it on the GPU. One definition.
    if (!buildMaterialBinding(context, missingMaterial(), /*pool=*/nullptr,
                              /*placeholderBaseColour=*/true, built)) {
        return nullptr;
    }
    fallback = std::move(built);
    return &*fallback;
}

bool SceneRenderer::Impl::drawModel(Diligent::IDeviceContext* context,
                                    const Diligent::GLTF::Model& model, const DrawItem& item,
                                    Guid guid, const AssetPool& pool) {
    HP_PROFILE_ZONE();

    constexpr Diligent::Uint32 kSceneIndex = 0;
    if (kSceneIndex >= model.Scenes.size()) {
        return false;
    }

    ModelBindings* modelBindings = ensureBindings(guid, model);
    if (modelBindings == nullptr) {
        return false;
    }

    // The entity's world transform is applied here, as the model transform, so
    // one loaded mesh can be drawn at many places without touching the model.
    model.ComputeTransforms(kSceneIndex, transforms, item.world);

    std::array<Diligent::IBuffer*, 8> vertexBuffers{};
    const auto vertexBufferCount = static_cast<Diligent::Uint32>(model.GetVertexBufferCount());
    if (vertexBufferCount > vertexBuffers.size()) {
        HP_LOG_ERROR(kLog, "model needs {} vertex buffers, more than the {} supported",
                     vertexBufferCount, vertexBuffers.size());
        return false;
    }
    for (Diligent::Uint32 i = 0; i < vertexBufferCount; ++i) {
        vertexBuffers[i] = model.GetVertexBuffer(i);
    }
    context->SetVertexBuffers(0, vertexBufferCount, vertexBuffers.data(), nullptr,
                              Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                              Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
    if (Diligent::IBuffer* indexBuffer = model.GetIndexBuffer()) {
        context->SetIndexBuffer(indexBuffer, 0,
                                Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    // Which vertex attributes the model actually carries. The pipeline state is
    // keyed on this, so a mesh without normals gets a different shader rather
    // than reading garbage.
    Diligent::PBR_Renderer::PSO_FLAGS vertexFlags = Diligent::PBR_Renderer::PSO_FLAG_NONE;
    for (Diligent::Uint32 i = 0; i < model.GetNumVertexAttributes(); ++i) {
        if (!model.IsVertexAttributeEnabled(i)) {
            continue;
        }
        const Diligent::GLTF::VertexAttributeDesc& attribute = model.GetVertexAttribute(i);
        const char* name = attribute.Name;
        if (std::strcmp(name, Diligent::GLTF::NormalAttributeName) == 0) {
            vertexFlags |= Diligent::PBR_Renderer::PSO_FLAG_USE_VERTEX_NORMALS;
        } else if (std::strcmp(name, Diligent::GLTF::Texcoord0AttributeName) == 0) {
            vertexFlags |= Diligent::PBR_Renderer::PSO_FLAG_USE_TEXCOORD0;
        } else if (std::strcmp(name, Diligent::GLTF::Texcoord1AttributeName) == 0) {
            vertexFlags |= Diligent::PBR_Renderer::PSO_FLAG_USE_TEXCOORD1;
        } else if (std::strcmp(name, Diligent::GLTF::VertexColorAttributeName) == 0) {
            vertexFlags |= Diligent::PBR_Renderer::PSO_FLAG_USE_VERTEX_COLORS;
        } else if (std::strcmp(name, Diligent::GLTF::TangentAttributeName) == 0) {
            vertexFlags |= Diligent::PBR_Renderer::PSO_FLAG_USE_VERTEX_TANGENTS;
        }
    }

    bool drewAnything = false;
    const Diligent::GLTF::Scene& scene = model.Scenes[kSceneIndex];

    for (const Diligent::GLTF::Node* node : scene.LinearNodes) {
        if (node == nullptr || node->pMesh == nullptr) {
            continue;
        }
        const float4x4& nodeMatrix = transforms.NodeGlobalMatrices[node->Index];

        for (const Diligent::GLTF::Primitive& primitive : node->pMesh->Primitives) {
            if (primitive.MaterialId >= modelBindings->material.size()) {
                continue;
            }
            Diligent::IShaderResourceBinding* srb =
                modelBindings->material[primitive.MaterialId];
            const Diligent::GLTF::Material& imported = model.Materials[primitive.MaterialId];
            const Diligent::GLTF::Material* material = &imported;
            Diligent::PBR_Renderer::PSO_FLAGS extraFlags = Diligent::PBR_Renderer::PSO_FLAG_NONE;

            // **The three-state table, drawn** (T0060.10 decides it, this
            // spends it). `Imported` keeps the model's own material -- the
            // common case and byte-identical to what drew before this existed.
            const ResolvedMaterial resolved =
                resolveMaterialSlot(pool, item.materials, primitive.MaterialId);
            if (resolved.state == MaterialSlot::Assigned) {
                if (MaterialBinding* binding = ensureMaterialBinding(context, pool, resolved)) {
                    material = &binding->gltf;
                    srb = binding->srb;
                    extraFlags = binding->extraFlags;
                }
                // A failed binding falls back to the imported material rather
                // than dropping the draw; the failure was logged when the
                // build was attempted, once, not here.
            } else if (resolved.state == MaterialSlot::Missing) {
                if (reportedMissing.insert(resolved.guid).second) {
                    // Once per GUID, on the first sighting -- the transition.
                    // The draw path below stays silent (T0141).
                    HP_LOG_WARN(kLog,
                                "material {} is not loaded; rendering the missing-material "
                                "pattern in its place",
                                resolved.guid.toString());
                }
                if (MaterialBinding* binding = ensureFallbackBinding(context)) {
                    material = &binding->gltf;
                    srb = binding->srb;
                    extraFlags = binding->extraFlags;
                }
            }
            if (srb == nullptr) {
                continue;
            }

            // The mask is what keeps this ticket out of T0079/T0087/T0096 --
            // see `kFeatureMask`. Material flags are intersected with it rather
            // than trusted, because a glTF that references an emissive map would
            // otherwise ask for a shader feature nothing here configures.
            // `GetMaterialPSOFlags` lives on `GLTF_PBR_Renderer`, not the base,
            // so it is inlined here -- and with this `CreateInfo` it collapses
            // to a constant. Its optional flags are each gated on a setting
            // (`EnableAO`, `EnableEmissive`, `EnableClearCoat`, sheen,
            // anisotropy, iridescence, transmission, volume) and every one of
            // them is off above, so only the three always-on maps survive.
            // **If any of those settings is ever enabled, this must go back to
            // consulting the material** -- which is T0134's business, and the
            // static_assert below is what will make that impossible to forget.
            constexpr Diligent::PBR_Renderer::PSO_FLAGS kMaterialFlags =
                static_cast<Diligent::PBR_Renderer::PSO_FLAGS>(
                    Diligent::PBR_Renderer::PSO_FLAG_USE_COLOR_MAP |
                    Diligent::PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP |
                    Diligent::PBR_Renderer::PSO_FLAG_USE_PHYS_DESC_MAP |
                    // Enabled with `EnableAO`/`EnableEmissive` in
                    // `SurfacePipeline::configure`, and the two must move
                    // together: the setting builds the signature slot, the flag
                    // compiles the shader that reads it. One without the other
                    // is either an unread texture or an unbound sampler.
                    Diligent::PBR_Renderer::PSO_FLAG_USE_AO_MAP |
                    Diligent::PBR_Renderer::PSO_FLAG_USE_EMISSIVE_MAP);
            Diligent::PBR_Renderer::PSO_FLAGS flags =
                (vertexFlags | kMaterialFlags | kEnabledFeatures | extraFlags) & kFeatureMask;
            if ((flags & Diligent::PBR_Renderer::PSO_FLAG_USE_TEXCOORD0) == 0) {
                // Parallax displaces texture coordinates; a mesh without any
                // renders flat rather than compiling a march it cannot feed.
                flags &= ~SurfacePipeline::kPsoFlagHeightMap;
            }

            // Alpha mode reaches the pipeline from whichever material is
            // actually drawn: it selects the blend state and the compile-time
            // cutout test in `SurfacePipeline::build`. Before T0141.12 this
            // was hardwired to opaque, which was honest only because nothing
            // could author a non-opaque material yet.
            //
            // **The fallback inherits the imported material's sidedness.** The
            // missing-material pattern's first rule is visible, never
            // invisible -- and `missingMaterial()` is single-sided, so letting
            // it drive the cull mode would cull exactly the double-sided
            // surfaces whose material went missing. Shading is the fallback's;
            // which faces exist stays the surface's.
            const bool doubleSided = resolved.state == MaterialSlot::Missing
                                         ? imported.DoubleSided
                                         : material->DoubleSided;
            static_assert(static_cast<int>(Diligent::PBR_Renderer::ALPHA_MODE_BLEND) ==
                              static_cast<int>(Diligent::GLTF::Material::ALPHA_MODE_BLEND),
                          "the two alpha enums must stay value-identical for this cast");
            // `CULL_MODE_BACK`, per the winding convention (WindingConvention.hpp,
            // D33): hardware facing equals glTF facing, so single-sided means what
            // the glTF spec means -- back faces, winding-defined, are culled. The
            // T0141.12 era culled FRONT here to compensate for test assets whose
            // winding contradicted their normals; T0152 re-wound the assets and the
            // compensation reverted with them. Note the determinant rule (T0152.5):
            // a negative-determinant node transform flips effective winding by
            // specification, and when that lands it flips this enum per draw.
            const Diligent::PBR_Renderer::PSOKey key{
                Diligent::PBR_Renderer::RenderPassType::Main, flags,
                static_cast<Diligent::PBR_Renderer::ALPHA_MODE>(material->Attribs.AlphaMode),
                doubleSided ? Diligent::CULL_MODE_NONE : Diligent::CULL_MODE_BACK};

            Diligent::IPipelineState* pso = renderer->pipeline(graphics, key);
            if (pso == nullptr) {
                continue;
            }
            context->SetPipelineState(pso);
            context->CommitShaderResources(srb,
                                           Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);

            {
                void* attribs = nullptr;
                context->MapBuffer(renderer->GetPBRPrimitiveAttribsCB(), Diligent::MAP_WRITE,
                                   Diligent::MAP_FLAG_DISCARD, attribs);
                if (attribs != nullptr) {
                    Diligent::GLTF_PBR_Renderer::PBRPrimitiveShaderAttribsData data{
                        flags, &nodeMatrix, &nodeMatrix, 0};
                    Diligent::GLTF_PBR_Renderer::WritePBRPrimitiveShaderAttribs(
                        attribs, data, !renderer->GetSettings().PackMatrixRowMajor,
                        renderer->GetSettings().UseSkinPreTransform);
                }
                context->UnmapBuffer(renderer->GetPBRPrimitiveAttribsCB(), Diligent::MAP_WRITE);
            }

            // **The material constant buffer is a second, separate buffer**, and
            // leaving it unwritten is why the first version of this drew nothing
            // at all. The shader reads base colour, metallic/roughness and the
            // alpha cutoff from here; against an uninitialised buffer that is a
            // zero base colour and a cutoff that discards every fragment. The
            // symptom is indistinguishable from a depth or culling failure --
            // a draw is issued, statistics report a submission, and the target
            // comes back pure clear colour.
            {
                void* materialAttribs = nullptr;
                context->MapBuffer(renderer->GetPBRMaterialAttribsCB(), Diligent::MAP_WRITE,
                                   Diligent::MAP_FLAG_DISCARD, materialAttribs);
                if (materialAttribs != nullptr) {
                    const Diligent::GLTF_PBR_Renderer::PBRMaterialShaderAttribsData data{
                        flags, renderer->GetSettings().TextureAttribIndices, *material};
                    Diligent::GLTF_PBR_Renderer::WritePBRMaterialShaderAttribs(materialAttribs,
                                                                               data);
                }
                context->UnmapBuffer(renderer->GetPBRMaterialAttribsCB(), Diligent::MAP_WRITE);
            }

            if (primitive.HasIndices()) {
                Diligent::DrawIndexedAttribs draw{primitive.IndexCount, Diligent::VT_UINT32,
                                                  Diligent::DRAW_FLAG_VERIFY_ALL};
                draw.FirstIndexLocation = primitive.FirstIndex;
                draw.BaseVertex = primitive.FirstVertex;
                context->DrawIndexed(draw);
            } else {
                Diligent::DrawAttribs draw{primitive.VertexCount,
                                           Diligent::DRAW_FLAG_VERIFY_ALL};
                draw.StartVertexLocation = primitive.FirstVertex;
                context->Draw(draw);
            }
            drewAnything = true;
        }
    }

    return drewAnything;
}

SceneRenderer::SceneRenderer() = default;
SceneRenderer::~SceneRenderer() = default;
SceneRenderer::SceneRenderer(SceneRenderer&&) noexcept = default;
SceneRenderer& SceneRenderer::operator=(SceneRenderer&&) noexcept = default;

bool SceneRenderer::create(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
                           TargetFormat colour, std::optional<TargetFormat> depth) {
    HP_PROFILE_ZONE();

    if (device == nullptr || context == nullptr) {
        HP_LOG_ERROR(kLog, "create() needs a device and a context");
        return false;
    }

    auto impl = std::make_unique<Impl>();
    impl->device = device;

    Diligent::PBR_Renderer::CreateInfo info;
    SurfacePipeline::configure(info);

    // **Without this the vertex shader has no inputs and nothing is ever drawn.**
    // `CreateInfo::InputLayout` defaults to empty, and `PBR_Renderer` builds its
    // vertex-shader input struct from it -- so an unset layout produces a
    // pipeline that compiles, binds, issues draws and rasterises nothing. The
    // symptom is a frame of pure clear colour with `submitted == 1`, which reads
    // exactly like a depth or culling failure and is neither.
    //
    // `GLTF::DefaultVertexAttributes` is what `MeshAsset`'s models are loaded
    // with (T0023 passes no override), so the layout the renderer expects and
    // the layout the buffers actually have are the same by construction rather
    // than by coincidence.
    const Diligent::InputLayoutDescX inputLayout = Diligent::GLTF::VertexAttributesToInputLayout(
        Diligent::GLTF::DefaultVertexAttributes.data(),
        static_cast<Diligent::Uint32>(Diligent::GLTF::DefaultVertexAttributes.size()));
    info.InputLayout = inputLayout;

    // **The `IRenderStateCache` slot stays null, and that is a decision, not
    // an omission** (T0141.3, 2026-08-06). What the slot would cache today is
    // nothing: with `EnableIBL` off the base constructor compiles zero
    // shaders, and `SurfacePipeline::build` creates its shaders and PSOs
    // against the device directly. The measured startup cost was slang's cold
    // compile, and that is paid off by the persistent SPIR-V bytecode cache
    // in `SlangCompiler.cpp` instead. Revisit when T0087 turns IBL on -- the
    // BRDF precompute then runs through this slot -- or if PSO creation from
    // cached SPIR-V ever shows up in a measurement.
    impl->renderer = std::make_unique<SurfacePipeline>(device, nullptr, context, info);

    // The checkerboard, created up front rather than on first miss: it is 16x16
    // and the alternative is a draw path that can fail to build a texture,
    // which is a worse place to be than paying a kilobyte on startup. Its
    // absence is survivable -- the fallback then binds the renderer's white
    // default and the mesh still draws, just less loudly wrong.
    impl->placeholder = makePlaceholderTexture(device);
    if (impl->placeholder && impl->placeholder->valid()) {
        // Created without a context, so the transition happens here -- the
        // same pattern the glTF loader and `PBR_Renderer`'s own defaults use.
        const Diligent::StateTransitionDesc barrier{
            impl->placeholder->texture(), Diligent::RESOURCE_STATE_UNKNOWN,
            Diligent::RESOURCE_STATE_SHADER_RESOURCE,
            Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE};
        context->TransitionResourceStates(1, &barrier);
    }

    Diligent::CreateUniformBuffer(device, impl->renderer->GetPRBFrameAttribsSize(),
                                  "hp PBR frame attribs", &impl->frameAttribs);
    if (!impl->frameAttribs) {
        HP_LOG_ERROR(kLog, "could not create the frame attributes buffer");
        return false;
    }

    Diligent::GraphicsPipelineDesc& pipeline = impl->graphics;
    pipeline.NumRenderTargets = 1;
    pipeline.RTVFormats[0] = toDiligentFormat(colour);
    // `TEX_FORMAT_UNKNOWN` is how a pipeline state says "no depth target", and it
    // has to match what the caller actually binds. A state declaring a DSV format
    // while nothing is bound -- or the reverse -- is a render-pass
    // incompatibility on Vulkan, which is a validation error rather than a
    // slightly wrong image (T0027.4).
    pipeline.DSVFormat =
        depth ? toDiligentFormat(*depth) : Diligent::TEX_FORMAT_UNKNOWN;
    pipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // **T0130, and the line this whole class exists to be able to write.**
    // The engine maps the near plane to depth 1 and the far plane to 0, and
    // clears to `kDepthClearValue` (0). With Diligent's default of
    // `COMPARISON_FUNC_LESS` a fragment would have to be nearer than 0 to pass,
    // so nothing would draw at all -- a black frame rather than inverted
    // geometry. `GLTF_PBR_Renderer` hardcodes exactly that default and keeps its
    // PSO cache private, which is why it is not used.
    //
    // A depth-less pass turns both off rather than keeping the comparison
    // against a buffer that is not there: with no depth attachment, draw order
    // within the pass is submission order, which is what an overlay wants.
    pipeline.DepthStencilDesc.DepthEnable = depth.has_value();
    pipeline.DepthStencilDesc.DepthWriteEnable = depth.has_value();
    pipeline.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_GREATER_EQUAL;
    static_assert(kReverseZ, "the depth comparison above assumes T0130's reverse-Z");

    impl_ = std::move(impl);
    HP_LOG_INFO(kLog, "scene renderer ready, {}",
                depth ? "reverse-Z depth" : "no depth attachment");
    return true;
}

void SceneRenderer::release() {
    impl_.reset();
}

bool SceneRenderer::valid() const {
    return impl_ != nullptr && impl_->renderer != nullptr;
}

std::size_t SceneRenderer::render(Diligent::IDeviceContext* context, const DrawList& list,
                                  const ResolvedView& view, const AssetPool& pool,
                                  const LightList& lights,
                                  DrawSubmitStats* stats) {
    HP_PROFILE_ZONE();

    DrawSubmitStats counted;
    if (!valid() || context == nullptr) {
        if (stats != nullptr) {
            *stats = counted;
        }
        return 0;
    }

    Impl& impl = *impl_;

    // **Written per draw, not per frame, because light selection is per
    // object** (79.3). The camera half is identical every time and re-uploading
    // it is the price of the shader having exactly one frame-wide light array
    // with no per-primitive index list. Measured cost is one more dynamic-buffer
    // map per draw, alongside the two (primitive and material attribs) already
    // there -- proportionate, and the thing to attack first if submission ever
    // becomes the bottleneck. The alternative is clustered forward, which is
    // T0079's named next step and a different shape of frame.
    const auto writeFrameAttribs = [&](const LightList& selected) {
        HP_PROFILE_ZONE_NAMED("frame attribs");
            Diligent::MapHelper<Diligent::HLSL::PBRFrameAttribs> frame{
                context, impl.frameAttribs, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD};
            if (frame) {
                Diligent::HLSL::CameraAttribs& camera = frame->Camera;
                camera.mView = view.view;
                camera.mProj = view.projection;
                camera.mViewProj = view.viewProjection;
                camera.mViewInv = view.view.Inverse();
                camera.mProjInv = view.projection.Inverse();
                camera.mViewProjInv = view.viewProjection.Inverse();

                const float4x4 worldFromView = view.view.Inverse();
                camera.f4Position =
                    float4(worldFromView.m30, worldFromView.m31, worldFromView.m32, 1.0F);

                const auto width = static_cast<float>(view.viewportWidth);
                const auto height = static_cast<float>(view.viewportHeight);
                camera.f4ViewportSize = float4(width, height, 1.0F / width, 1.0F / height);

                // **Near > far is how Diligent is told the buffer is reversed** --
                // its own comment on this helper says so. Passing them the usual way
                // round would leave the shaders reconstructing linear depth
                // backwards, which the depth test alone would not reveal.
                camera.SetClipPlanes(view.camera.farPlane, view.camera.nearPlane);

                // Left-handed, matching the engine's convention (T0056/T0130).
                camera.fHandness = -1.0F;

                // **`Renderer` must be written, and not writing it was a real bug
                // (T0134).** The buffer is mapped `MAP_FLAG_DISCARD`, so every field
                // in this struct is undefined until something assigns it -- and
                // `RenderPBR.psh` reads `Renderer.MipBias` *unconditionally*, on
                // every texture sample of every draw (`ReadBaseLayerProperties`,
                // lines 147-148), along with `OcclusionStrength` and
                // `EmissionScale`.
                //
                // It went unnoticed because the only meshes drawn so far have no
                // textures at all: they sample the renderer's 1x1 defaults, where a
                // garbage mip bias selects the only mip there is. The first textured
                // material would have produced randomly blurred surfaces, varying
                // per frame, with nothing pointing at the cause.
                //
                // Value-initialised first so a field added by a Diligent upgrade
                // starts at zero rather than at whatever was in the buffer.
                Diligent::HLSL::PBRRendererShaderParameters& params = frame->Renderer;
                params = Diligent::HLSL::PBRRendererShaderParameters{};

                // Sets `PrefilteredCubeLastMip`, and is the only field Diligent
                // wants to own. Null env map is correct while IBL is off (T0087).
                impl.renderer->SetInternalShaderParameters(params, nullptr);

                params.OcclusionStrength = 1.0F;
                params.EmissionScale = 1.0F;
                params.IBLScale = float4(1.0F, 1.0F, 1.0F, 1.0F);
                params.PointSize = 1.0F;
                // Zero: no mip bias. A negative value sharpens and a positive one
                // blurs, and T0111's render-scale decision is where a non-zero one
                // would come from, not here.
                params.MipBias = 0.0F;

                // **Per view, from the camera** (T0141). `SurfaceDebugView`
                // restates DiligentFX's `DebugViewType` numbering precisely so
                // this cast is the whole conversion — if the two ever disagree,
                // they disagree loudly at the one place that knows both.
                params.DebugView = static_cast<int>(view.camera.debugView);

                // **Zero until T0079.** The shader clamps its light loop to this, so
                // it is what makes "no lights" mean no lights rather than reading
                // past the end of an array that `MaxLightCount = 0` did not
                // allocate.
                // **Light data follows the frame struct in the same buffer** --
                // `PBRFrameAttribs` deliberately does not declare the array (its
                // length is a shader define), so the C++ side writes past the end of
                // the struct into space `GetPRBFrameAttribsSize` reserved. This is
                // how Diligent's own viewer does it; there is no typed accessor.
                auto* lightArray = reinterpret_cast<Diligent::HLSL::PBRLightAttribs*>(frame + 1);
                const std::size_t lightCount = std::min(selected.size(), kMaxLights);
                for (std::size_t i = 0; i < lightCount; ++i) {
                    const ResolvedLight& source = selected[i];

                    // Converted into Diligent's own glTF light rather than packed by
                    // hand, so `Range4` and the spot cone's scale/offset come from
                    // the code that owns the layout (D24). Reimplementing that
                    // packing is how a layout drifts silently.
                    Diligent::GLTF::Light lamp;
                    switch (source.light.type) {
                    case LightType::Directional:
                        lamp.Type = Diligent::GLTF::Light::TYPE::DIRECTIONAL;
                        break;
                    case LightType::Point:
                        lamp.Type = Diligent::GLTF::Light::TYPE::POINT;
                        break;
                    case LightType::Spot:
                        lamp.Type = Diligent::GLTF::Light::TYPE::SPOT;
                        break;
                    }
                    lamp.Color = source.light.colour;
                    lamp.Intensity = source.light.intensity;
                    lamp.Range = source.light.range;
                    lamp.InnerConeAngle = source.light.innerConeAngle;
                    lamp.OuterConeAngle = source.light.outerConeAngle;

                    Diligent::GLTF_PBR_Renderer::WritePBRLightShaderAttribs(
                        {&lamp, &source.position, &source.direction, 1.0F}, lightArray + i);
                }
                params.LightCount = static_cast<int>(lightCount);

                // Tonemapping parameters. Unused while `PSO_FLAG_ENABLE_TONE_MAPPING`
                // is masked off, and set to Diligent's own defaults anyway so that
                // turning it on in T0096 changes one thing rather than two.
                params.AverageLogLum = 0.3F;
                params.MiddleGray = 0.18F;
                params.WhitePoint = 3.0F;

                frame->PrevCamera = camera;
            }
    };

    // What `GLTF_PBR_Renderer::Begin` does, inlined because it is on the derived
    // class: next-gen backends require a dynamic buffer to be mapped before its
    // first use each frame. Harmless when skinning is unused, and omitting it is
    // a Vulkan validation error rather than a visible bug, so it is easy to miss.
    if (Diligent::IBuffer* joints = impl.renderer->GetJointsBuffer()) {
        Diligent::MapHelper<float4x4> map{context, joints, Diligent::MAP_WRITE,
                                          Diligent::MAP_FLAG_DISCARD};
    }

    for (const DrawItem& item : list) {
        const std::shared_ptr<MeshAsset> mesh = pool.get<MeshAsset>(item.mesh);
        if (!mesh || !mesh->valid() || mesh->model() == nullptr) {
            // No placeholder, deliberately -- see `DrawSubmitStats::missingMesh`.
            ++counted.missingMesh;
            continue;
        }

        // The object's world position is the translation row of its transform.
        const float3 objectPosition{item.world.m30, item.world.m31, item.world.m32};
        selectLightsFor(lights, objectPosition, item.layers, kMaxLights, impl.selected);
        writeFrameAttribs(impl.selected);

        if (impl.drawModel(context, *mesh->model(), item, item.mesh, pool)) {
            ++counted.submitted;
        }
    }

    if (counted.missingMesh > 0) {
        HP_LOG_WARN(kLog, "{} draw item(s) reference a mesh that is not loaded; nothing was drawn "
                          "for them",
                    counted.missingMesh);
    }

    if (stats != nullptr) {
        *stats = counted;
    }
    return counted.submitted;
}

} // namespace hp
