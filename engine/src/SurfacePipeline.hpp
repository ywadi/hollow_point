// Pipeline states built from the engine's own shaders (T0141.10, D26).
//
// **Internal to the engine, and it must stay that way.** It names Diligent types
// in its interface, which D21/D22 keep out of public headers -- consumers see
// `SceneRenderer`, never this.
//
// ## Why a subclass rather than a replacement
//
// D26 chose to own the surface stage without modifying DiligentFX. That does not
// mean writing a renderer from nothing: `PBR_Renderer` carries a large amount of
// machinery that would be genuinely unpleasant to reimplement and that we have
// no reason to own --
//
//   * `DefineMacros` -- the shader macro set for a given PSO key, which is what
//     makes `PBR_Shading.fxh` compile at all;
//   * `GetVSInputStructAndLayout`, `GetVSOutputStruct`, `GetPSOutputStruct` --
//     the generated interface structs the shaders include by name;
//   * the resource signature, the frame/primitive/material constant buffers, and
//     the default white/black/normal textures.
//
// All of that is `protected` or public, so it is reachable. What is **private**
// is `CreatePSO`, which is why this is not an override: the subclass reuses the
// plumbing and creates its own pipeline states beside it.
//
// The consequence worth stating: `PBR_Renderer::GetPsoCacheAccessor` is **not**
// used here. Its cache builds pipelines from *their* shaders, which is precisely
// what this exists to stop.
#pragma once

#include <PBR_Renderer.hpp>
#include <RefCntAutoPtr.hpp>

#include <unordered_map>

namespace hp {

/// Creates and caches pipeline states that run the engine's shaders.
class SurfacePipeline final : public Diligent::PBR_Renderer {
public:
    /// The engine's own unshaded permutation (T0141.12, 141.15).
    ///
    /// **A user-defined PSO flag, because DiligentFX's `PSO_FLAG_UNSHADED` is
    /// the wrong tool and that is measured, not guessed.** Their flag is a
    /// depth/wireframe-pass mode: the `PSOKey` constructor strips every texture
    /// and vertex-attribute flag when it is set, and their footer outputs the
    /// frame-wide `UnshadedColor` -- the *material* never reaches the target.
    /// What the engine needs is Godot's `unshaded`: full surface stage, no
    /// lighting. The missing-material checkerboard is exactly that -- it must
    /// *sample* the placeholder texture and must not be dimmable by scene
    /// lights.
    ///
    /// `build()` turns this bit into the `HP_UNSHADED` macro the shader
    /// contract already declares (`HpMaterial.slang`), so the shader-side switch
    /// is compile-time -- the lighting code is not in the pipeline at all,
    /// which is the entire point of unshaded (141.15).
    ///
    /// User-defined bits survive the `PSOKey` constructor untouched and are
    /// ignored by `DefineMacros` and the generated-struct builders, which is
    /// what makes this safe to carry through their machinery.
    static constexpr Diligent::PBR_Renderer::PSO_FLAGS kPsoFlagUnshaded =
        Diligent::PBR_Renderer::PSO_FLAG_FIRST_USER_DEFINED;

    /// The material carries a height map and the surface stage runs parallax
    /// occlusion over it (T0141.7). Becomes the `HP_USE_HEIGHT_MAP` macro.
    ///
    /// A permutation bit rather than a runtime branch for the same reason
    /// `HP_UNSHADED` is one: the march is a texture-sampling loop, and every
    /// material without a height map must not carry it in registers. Only
    /// meaningful together with `PSO_FLAG_USE_TEXCOORD0` — parallax displaces
    /// texture coordinates, so a mesh without any renders flat and the caller
    /// strips the bit.
    static constexpr Diligent::PBR_Renderer::PSO_FLAGS kPsoFlagHeightMap =
        static_cast<Diligent::PBR_Renderer::PSO_FLAGS>(
            Diligent::PBR_Renderer::PSO_FLAG_FIRST_USER_DEFINED << 1ULL);

    /// The material projects its textures from world space (T0141.8).
    /// Becomes the `HP_TRIPLANAR` macro; needs no texture coordinates, which
    /// is the whole point. Takes precedence over `kPsoFlagHeightMap` — there
    /// are no UVs for a parallax march to displace.
    static constexpr Diligent::PBR_Renderer::PSO_FLAGS kPsoFlagTriplanar =
        static_cast<Diligent::PBR_Renderer::PSO_FLAGS>(
            Diligent::PBR_Renderer::PSO_FLAG_FIRST_USER_DEFINED << 2ULL);

    /// The height map's resource names in the signature and the shader.
    static constexpr const char* kHeightMapVariable = "g_HeightMap";

    /// Fills in the engine's renderer settings.
    ///
    /// **One place, because two places is how the last bug got in.**
    /// `SceneRenderer` and the pipeline test each built their own `CreateInfo`
    /// by hand, they drifted, and the field they both omitted —
    /// `TextureAttribIndices` — defaults to a value that means "disabled"
    /// everywhere it is read. Nothing logged; three subsystems just quietly did
    /// nothing.
    ///
    /// `InputLayout` is **not** set here and is the caller's job: `CreateInfo`
    /// holds a *pointer* to the layout's elements, so a layout built inside this
    /// function would dangle the moment it returned.
    ///
    /// @param info the settings to fill in.
    static void configure(CreateInfo& info);

    /// Constructs the renderer. Arguments are `PBR_Renderer`'s own.
    /// @param device the render device.
    /// @param cache optional render-state cache; null is fine.
    /// @param context the immediate context.
    /// @param info the renderer settings.
    SurfacePipeline(Diligent::IRenderDevice* device, Diligent::IRenderStateCache* cache,
                    Diligent::IDeviceContext* context, const CreateInfo& info);

    /// Returns a pipeline state running the engine's shaders for this key.
    ///
    /// Cached on the key and the graphics description together, because two
    /// targets with different formats need different pipelines for the same
    /// shader — and a cache keyed on the shader alone would hand back one built
    /// for the wrong render pass, which Vulkan reports as an incompatibility
    /// rather than a wrong image.
    ///
    /// @param graphics render-target formats, depth state and topology.
    /// @param key which shader features this draw needs.
    /// @param customModule virtual path of the material's shader module
    ///        (T0142.15), or null for the standard material. Part of the cache
    ///        key: two materials with different modules are different
    ///        pipelines whatever their flags say.
    /// @returns the pipeline, or nullptr when it could not be created. **Not
    ///          fatal** — the caller skips the draw, and the compile failure is
    ///          logged here, on the attempt, never from the draw path (T0141).
    [[nodiscard]] Diligent::IPipelineState* pipeline(const Diligent::GraphicsPipelineDesc& graphics,
                                                     const PSOKey& key,
                                                     const char* customModule = nullptr);

    /// The stand-in view for a texture slot a material does not fill.
    ///
    /// **Every slot the shader declares has to be bound, whether the material
    /// uses it or not**, and that is not a nicety. `SceneRenderer` builds every
    /// pipeline with the colour, normal and physical-descriptor maps enabled —
    /// one permutation for every material rather than one per texture
    /// combination — so an untextured material still gets a shader that declares
    /// all three. Leaving them unbound makes the backend's validation layer
    /// complain and makes the sample return **zero**, which is a black surface,
    /// not a white one.
    ///
    /// The values match glTF's meaning of "absent": white for colour and
    /// occlusion, a flat +Z normal, and roughness 1 / metallic 0 for the
    /// physical descriptor. `CreateInfo::CreateDefaultTextures` is what creates
    /// them.
    ///
    /// **This accessor exists because `m_pDefaultPhysDescSRV` is protected with
    /// no public getter** — white, black and the default normal map all have
    /// one, and that one does not. Reaching it is a thing the subclass can do
    /// and a free function beside the renderer could not, which is the same
    /// reason `SurfacePipeline` is a subclass at all.
    ///
    /// @param id which texture slot.
    /// @returns the default view, or nullptr when `CreateDefaultTextures` is off.
    [[nodiscard]] Diligent::ITextureView* defaultTexture(TEXTURE_ATTRIB_ID id) const;

private:
    /// Adds the engine's own resources to the base signature (T0141.7).
    ///
    /// **This is the designed extension point**: `CreateSignature` builds the
    /// whole descriptor and hands it here before creating the object, so the
    /// engine's height map joins the one signature every SRB and pipeline
    /// already shares — no second signature, no second `CommitShaderResources`.
    ///
    /// It only dispatches virtually because the constructor passes
    /// `InitSignature = false` and runs `CreateSignature()` in its own body:
    /// the base constructor would call it through the base vtable.
    void CreateCustomSignature(Diligent::PipelineResourceSignatureDescX&& desc) override;

    /// Builds one pipeline. Called only on a cache miss.
    [[nodiscard]] Diligent::RefCntAutoPtr<Diligent::IPipelineState>
    build(const Diligent::GraphicsPipelineDesc& graphics, const PSOKey& key,
          const char* customModule);

    /// Keyed on the PSO key's own hash combined with the graphics description's
    /// formats. `PSOKey` has a hasher; the formats are folded in by hand because
    /// nothing in Diligent hashes a `GraphicsPipelineDesc`.
    std::unordered_map<std::size_t, Diligent::RefCntAutoPtr<Diligent::IPipelineState>> pipelines_;
};

} // namespace hp
