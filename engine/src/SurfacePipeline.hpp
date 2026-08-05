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
    /// @returns the pipeline, or nullptr when it could not be created. **Not
    ///          fatal** — the caller skips the draw, and the compile failure is
    ///          logged here, on the attempt, never from the draw path (T0141).
    [[nodiscard]] Diligent::IPipelineState* pipeline(const Diligent::GraphicsPipelineDesc& graphics,
                                                     const PSOKey& key);

private:
    /// Builds one pipeline. Called only on a cache miss.
    [[nodiscard]] Diligent::RefCntAutoPtr<Diligent::IPipelineState>
    build(const Diligent::GraphicsPipelineDesc& graphics, const PSOKey& key);

    /// Keyed on the PSO key's own hash combined with the graphics description's
    /// formats. `PSOKey` has a hasher; the formats are folded in by hand because
    /// nothing in Diligent hashes a `GraphicsPipelineDesc`.
    std::unordered_map<std::size_t, Diligent::RefCntAutoPtr<Diligent::IPipelineState>> pipelines_;
};

} // namespace hp
