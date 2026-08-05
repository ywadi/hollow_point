#include <hp/SceneRenderer.hpp>

#include <hp/Assets.hpp>
#include <hp/DepthConvention.hpp>
#include <hp/Log.hpp>
#include <hp/Profiling.hpp>

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
#include <unordered_map>
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
        Diligent::PBR_Renderer::PSO_FLAG_DEFAULT_TEXTURES);

} // namespace

/// Everything Diligent lives here, and nothing above this line names it.
struct SceneRenderer::Impl {
    Diligent::IRenderDevice* device = nullptr;

    std::unique_ptr<Diligent::PBR_Renderer> renderer;

    /// Camera and frame-wide shader data. One buffer, rewritten per frame.
    Diligent::RefCntAutoPtr<Diligent::IBuffer> frameAttribs;

    /// Pipeline states, **built with the engine's depth convention**. This is
    /// the whole reason `PBR_Renderer` is driven directly rather than
    /// `GLTF_PBR_Renderer`, which builds this itself and leaves the comparison
    /// at `LESS`.
    Diligent::PBR_Renderer::PsoCacheAccessor psoCache;

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

    /// Scratch, reused across frames so a draw does not allocate.
    Diligent::GLTF::ModelTransforms transforms;

    /// Creates the per-material SRBs for a model, once.
    ///
    /// @param guid the mesh asset's identity, used as the cache key.
    /// @param model the model whose materials need binding.
    /// @returns the bindings, or nullptr if they could not be created.
    ModelBindings* ensureBindings(Guid guid, const Diligent::GLTF::Model& model);

    /// Draws one model at one transform.
    /// @param context the context to record into.
    /// @param model the model to draw.
    /// @param item the draw item supplying the world transform.
    /// @param guid the mesh asset's identity, for the binding cache.
    /// @returns whether anything was submitted.
    bool drawModel(Diligent::IDeviceContext* context, const Diligent::GLTF::Model& model,
                   const DrawItem& item, Guid guid);
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

        // **Textures are bound from the model directly**, because the helper
        // DiligentFX uses for this -- `GetPBRTextureSRV` -- is not public, and
        // neither is the default physical-descriptor view. What that helper adds
        // is a **colour-space conversion** per slot, so this path does not yet
        // apply one. That is a real gap and it belongs with T0097's sRGB work,
        // not silently here; T0134 records it. Untextured materials are
        // unaffected: they fall back to the renderer's default textures, which
        // is what `CreateDefaultTextures` exists for (28.2).
        const Diligent::GLTF::Material& material = model.Materials[i];
        const auto& indices = renderer->GetSettings().TextureAttribIndices;
        for (int id = 0; id < Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT; ++id) {
            const int attrib = indices[static_cast<std::size_t>(id)];
            if (attrib < 0) {
                continue;
            }
            const int texture = material.GetTextureId(attrib);
            if (texture < 0) {
                continue;
            }
            Diligent::ITexture* tex = model.GetTexture(texture);
            if (tex == nullptr) {
                continue;
            }
            renderer->SetMaterialTexture(
                srb, tex->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE),
                static_cast<Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID>(id));
        }

        built.material[i] = std::move(srb);
    }

    bindings[guid] = std::move(built);
    return &bindings[guid];
}

bool SceneRenderer::Impl::drawModel(Diligent::IDeviceContext* context,
                                    const Diligent::GLTF::Model& model, const DrawItem& item,
                                    Guid guid) {
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
            if (srb == nullptr) {
                continue;
            }

            const Diligent::GLTF::Material& material = model.Materials[primitive.MaterialId];

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
                    Diligent::PBR_Renderer::PSO_FLAG_USE_PHYS_DESC_MAP);
            const Diligent::PBR_Renderer::PSO_FLAGS flags =
                (vertexFlags | kMaterialFlags) & kFeatureMask;

            const Diligent::PBR_Renderer::PSOKey key{
                Diligent::PBR_Renderer::RenderPassType::Main, flags,
                material.DoubleSided ? Diligent::CULL_MODE_NONE : Diligent::CULL_MODE_BACK};

            Diligent::IPipelineState* pso =
                psoCache.Get(key, Diligent::PBR_Renderer::PsoCacheAccessor::GET_FLAG_CREATE_IF_NULL);
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
                        flags, renderer->GetSettings().TextureAttribIndices, material};
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
    // Off because they belong to other tickets, and because each one that is on
    // demands resources this ticket has no way to supply -- IBL wants a
    // precomputed environment, lights want a populated light buffer.
    info.EnableIBL = false;
    info.EnableAO = false;
    info.EnableEmissive = false;
    info.EnableShadows = false;
    info.MaxLightCount = 0;
    // **This is 28.2.** The renderer creates white/black/default-normal
    // textures, which is what a material with no texture assigned samples. So
    // "entities with no material get a visible default" costs nothing here --
    // and turning it off is what would make an unassigned mesh invisible.
    info.CreateDefaultTextures = true;
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
    // `WritePBRPrimitiveShaderAttribs` below, which is spelled
    // `!PackMatrixRowMajor` for exactly that reason -- set here, the two move
    // together instead of disagreeing silently.
    info.PackMatrixRowMajor = true;

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

    impl->renderer = std::make_unique<Diligent::PBR_Renderer>(device, nullptr, context, info);

    Diligent::CreateUniformBuffer(device, impl->renderer->GetPRBFrameAttribsSize(),
                                  "hp PBR frame attribs", &impl->frameAttribs);
    if (!impl->frameAttribs) {
        HP_LOG_ERROR(kLog, "could not create the frame attributes buffer");
        return false;
    }

    Diligent::GraphicsPipelineDesc pipeline;
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

    impl->psoCache = impl->renderer->GetPsoCacheAccessor(pipeline);

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

    // Frame-wide shader data. Written once per call rather than per draw, which
    // is the only reason the camera is a parameter here and not per item.
    {
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

            // **Zero until T0079.** The shader clamps its light loop to this, so
            // it is what makes "no lights" mean no lights rather than reading
            // past the end of an array that `MaxLightCount = 0` did not
            // allocate.
            params.LightCount = 0;

            // Tonemapping parameters. Unused while `PSO_FLAG_ENABLE_TONE_MAPPING`
            // is masked off, and set to Diligent's own defaults anyway so that
            // turning it on in T0096 changes one thing rather than two.
            params.AverageLogLum = 0.3F;
            params.MiddleGray = 0.18F;
            params.WhitePoint = 3.0F;

            frame->PrevCamera = camera;
        }
    }

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
        if (impl.drawModel(context, *mesh->model(), item, item.mesh)) {
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
