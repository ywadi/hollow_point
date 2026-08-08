// Turning a draw list into draw calls (T0028).
//
// **Nothing about DiligentFX appears here.** The renderer is held behind an
// `Impl`, and `engine/CMakeLists.txt` links DiligentFX PRIVATE, so the choice
// made on T0134 is confined to one translation unit and replacing it touches
// that file. That is the modularity, and it is enforced by the linker rather
// than by an interface -- **D22** rules out an abstraction with one
// implementation until there is a concrete second target.
//
// Why this drives `PBR_Renderer` and not `GLTF_PBR_Renderer`, which exists for
// exactly this job: `GLTF_PBR_Renderer` builds its pipeline state in its own
// constructor and leaves the depth comparison at Diligent's default of
// `COMPARISON_FUNC_LESS`, with the PSO cache private and no depth field in its
// `CreateInfo`. Under the engine's reverse-Z convention (T0130) that draws
// **nothing at all** -- depth clears to 0, the near plane is 1, and `LESS` lets
// through only fragments below 0. The base class takes the caller's pipeline
// desc, so it can be told `COMPARISON_FUNC_GREATER_EQUAL`. T0134 carries the
// full argument.
#pragma once

#include <hp/Api.hpp>
#include <hp/CameraSystem.hpp>
#include <hp/DrawSubmission.hpp>
#include <hp/FrameTargets.hpp>
#include <hp/Light.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>

namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
struct ITextureView;
} // namespace Diligent

namespace hp {

class AssetPool;

/// The targets a render draws into and the snapshots it copies them to, so a
/// material shader can sample what is behind it (T0147).
///
/// **All four or none.** The renderer needs the first two in order to put the
/// targets back after the copy, and the second two are what it copies into and
/// what the shader samples; leaving any of them null disables the intermediate
/// it belongs to. A default-constructed value is the pre-T0147 behaviour
/// exactly: no copy is issued, `g_SceneColour` and `g_SceneDepth` bind the
/// engine's white and black stand-ins, and a material that reads them is
/// warned about once.
///
/// **Every pointer is valid for the call and not beyond**, the same rule
/// `FrameTargets` states: a resize recreates the textures behind these views.
///
/// This is the *engine-fed* half of the game resource model — engine names in
/// the engine's base signature — and D35's other half, a game's own
/// declarations, needs nothing here.
struct SceneScreenInputs {
    /// The colour render-target view this render is drawing into.
    ///
    /// Needed because the snapshot is a copy: the render pass ends for it and
    /// the renderer re-binds exactly these two afterwards. **It does not
    /// choose targets** — it restores the ones it was handed.
    Diligent::ITextureView* colour = nullptr;

    /// The depth-stencil view this render is drawing into. See `colour`.
    Diligent::ITextureView* depth = nullptr;

    /// The shader-resource view of the texture `colour` is copied into, and
    /// what a shader samples as `g_SceneColour`. Must be the same size and
    /// format as `colour`'s texture; a mismatch is refused and logged once.
    Diligent::ITextureView* colourSnapshot = nullptr;

    /// The shader-resource view of the texture `depth` is copied into, and
    /// what a shader samples as `g_SceneDepth`. See `colourSnapshot`.
    Diligent::ITextureView* depthSnapshot = nullptr;
};

/// What one submit pass did, for logging and for tests.
struct DrawSubmitStats {
    /// Items whose mesh resolved and were drawn.
    std::size_t submitted = 0;

    /// Items whose mesh GUID is not in the pool.
    ///
    /// **These draw nothing and get no placeholder**, deliberately. A missing
    /// *texture* has an obvious stand-in; a missing *mesh* does not, and
    /// inventing a cube puts geometry in the world that no artist authored --
    /// which is worse than an empty space and a loud error. A visible marker for
    /// this belongs in T0061's debug draw, not here. Counted so the absence is
    /// reportable rather than silent.
    std::size_t missingMesh = 0;
};

/// Draws a `DrawList` through a resolved camera.
///
/// Owns the GPU-side renderer, its pipeline states and its per-model resource
/// bindings, so it is created once against a device and reused. Not tied to a
/// scene, a camera or a target: everything that varies per frame is a parameter,
/// which is what keeps this callable for portals, reflection probes and
/// thumbnails rather than only for "the" viewport (T0120.2).
class HP_API SceneRenderer {
public:
    /// Constructs an empty renderer that holds no device resources.
    SceneRenderer();

    /// Releases the renderer and everything it created.
    ~SceneRenderer();

    /// Not copyable: it owns GPU objects, and two owners would double-release.
    SceneRenderer(const SceneRenderer&) = delete;

    /// Not copyable; see the copy constructor.
    /// @returns nothing -- deleted.
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    /// Moves the renderer.
    /// @param other the renderer to move from.
    SceneRenderer(SceneRenderer&& other) noexcept;

    /// Moves the renderer, releasing whatever this one held.
    /// @param other the renderer to move from.
    /// @returns this renderer.
    SceneRenderer& operator=(SceneRenderer&& other) noexcept;

    /// Creates the pipeline states and shaders.
    ///
    /// The target formats are baked into the pipeline state, so a renderer
    /// created for one pair cannot draw into another -- recreate it if the
    /// targets change format. Size may change freely; only format is baked.
    /// @param device the device to create against. Must not be null.
    /// @param context the immediate context, needed for one-off setup uploads.
    ///        Must not be null.
    /// @param colour the colour target's format.
    /// @param depth the depth target's format, or **nothing for a pass that
    ///        renders without depth at all** — a HUD or an overlay (T0027.4).
    ///        This is not a convenience: a pipeline state declares whether it has
    ///        a depth target, and drawing with no depth bound through a state
    ///        that declares one is a render-pass incompatibility, not a
    ///        harmless mismatch. Absent, depth test and depth write are both off
    ///        and draw order within the pass is submission order.
    /// @returns whether creation succeeded. False leaves this renderer empty and
    ///          logs why; it is never a partial state.
    bool create(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
                TargetFormat colour, std::optional<TargetFormat> depth);

    /// Releases every device resource. Safe to call more than once.
    /// @returns nothing.
    void release();

    /// @returns whether `create` succeeded and this renderer can draw.
    [[nodiscard]] bool valid() const;

    /// Feeds a texture a game layer produced to material shaders, by name
    /// (T0147.4, T0094.5).
    ///
    /// **The second source for a declared texture.** A module declares
    /// `Texture2DArray visibility;` exactly as it declares any other texture
    /// (T0161, D35); a `.hpmat` binds one to an *asset*, and this binds one to
    /// whatever a game layer just rendered — a fog-of-war field (T0093), a flow
    /// simulation, a minimap. One declaration mechanism, two feeds, and the
    /// `.hpmat` wins when both name the same slot.
    ///
    /// **Not refcounted, deliberately**, matching `FrameTargets`' rule: this
    /// object does not keep the texture alive, and a resize invalidates the
    /// view. Feed it again after a resize; feeding null removes the entry.
    ///
    /// @param name the shader-side declaration name. Empty is ignored.
    /// @param view the view to bind, or null to remove the entry.
    /// @returns nothing.
    void setGameTexture(std::string_view name, Diligent::ITextureView* view);

    /// Removes every game-fed texture. What a layer calls on detach.
    /// @returns nothing.
    void clearGameTextures();

    /// Scales the environment's contribution to every surface (T0170.5).
    ///
    /// **The engine lights every scene from a default sky**, which is what
    /// makes metal look like metal and a surface facing away from a lamp
    /// something other than black. This is the dial on it: `1` is the default,
    /// `0` turns it off entirely and gives exactly the pre-environment image.
    ///
    /// **Why it exists rather than being a constant.** A test that asserts an
    /// exact lit colour is measuring one lamp against one material, and an
    /// ambient term it did not ask for is noise in that measurement — so the
    /// gpu suite turns it off and keeps testing what it tests. A game wants the
    /// opposite. Both are legitimate, so it is a setting.
    ///
    /// Feeds `PBRRendererShaderParameters::IBLScale`, so it costs nothing per
    /// draw and needs no pipeline rebuild — the permutation is unchanged and
    /// only the multiplier moves. Values above 1 over-brighten deliberately;
    /// negative values are clamped to 0.
    ///
    /// **This is not the environment itself.** Choosing *which* sky, and a
    /// per-scene ambient, are T0087's remaining scope.
    ///
    /// @param intensity the multiplier. Clamped at 0 from below.
    /// @returns nothing.
    void setEnvironmentIntensity(float intensity);

    /// @returns the current environment intensity. `1` unless set.
    [[nodiscard]] float environmentIntensity() const;

    /// Draws a list through a view.
    ///
    /// Targets must already be bound by the caller -- this issues draws and does
    /// not set render targets, because the layer that owns the target is the one
    /// that knows what else shares it (T0027). The **one** exception is the
    /// screen snapshot (T0147): when @p screen names them, the renderer copies
    /// the frame between its opaque and blend passes and re-binds exactly the
    /// two views it was handed. It still never chooses a target.
    ///
    /// **Two passes since T0147, and that changed draw order.** Every `Opaque`
    /// and `Mask` primitive is submitted before every `Blend` one, rather than
    /// in draw-list order — which is a correctness fix in its own right (a
    /// transparent surface drawn before the opaque geometry behind it blended
    /// against the clear colour) and is what gives the snapshot a moment to
    /// happen in. Ordering *within* the blend pass is still submission order;
    /// the back-to-front sort is T0045's.
    /// @param context the immediate context to record into. Must not be null.
    /// @param list what to draw, from `parseScene`.
    /// @param view the resolved camera, from `buildView`.
    /// @param pool where mesh GUIDs are resolved. Items whose mesh is absent are
    ///        skipped and counted.
    /// @param stats optional; filled with what the pass did. Null to ignore.
    /// @returns how many items were drawn.
    /// @param lights what illuminates the scene, from `gatherLights`. **An
    ///        empty list renders black**, which is not an error and is exactly
    ///        what every frame did before T0079.
    /// @param timeSeconds the frame's time, in seconds — `Clock::elapsed()`
    ///        from whichever clock owns this view's timeline (T0159.5). Reaches
    ///        shaders as `g_Frame.Renderer.Time` and the contract's
    ///        `HpSurfaceInput::Time`. Defaulted to zero so a caller that has no
    ///        clock (a thumbnail, a one-shot test render) gets a defined value,
    ///        not a stale or undefined one.
    /// @param screen the targets being drawn into and the snapshots a material
    ///        shader samples (T0147). Defaulted to nothing, which is what a
    ///        caller with no need for scene colour or depth passes — no copy is
    ///        issued and the intermediates read the engine's stand-ins.
    std::size_t render(Diligent::IDeviceContext* context, const DrawList& list,
                       const ResolvedView& view, const AssetPool& pool,
                       const LightList& lights = {}, DrawSubmitStats* stats = nullptr,
                       double timeSeconds = 0.0, const SceneScreenInputs& screen = {});

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hp
