// Drawing a scene as a composited layer (T0027.3, T0027.4).
//
// `SceneView` renders a scene into targets it owns. This renders a scene into
// targets **somebody else** owns, in an ordered stack alongside other layers.
// Both exist, and both are right:
//
//   * `SceneView` is what the editor viewport and T0033 consume — one scene, one
//     texture, self-contained.
//   * This is what a *frame* is made of — a world pass, then whatever gameplay
//     composites on top (T0094), then a HUD.
//
// **They share `SceneRenderer`, deliberately.** The alternative — this layer
// growing its own copy of the resolve-and-submit sequence — is how two paths
// drift until the editor and the runtime disagree about what a scene looks like.
//
// **A HUD is a camera on another view slot, not a different kind of layer**
// (27.4). `hudLayer()` builds one: an orthographic camera on slot 1, no depth
// test, nothing cleared. That is the whole of 2D-over-3D, and it needs no UI
// library — which matters, because choosing one here would be this ticket
// answering T0069's question. What library draws HUD *widgets* stays open.
#pragma once

#include <hp/Api.hpp>
#include <hp/DrawSubmission.hpp>
#include <hp/RenderStack.hpp>
#include <hp/SceneRenderer.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace hp {

class Scene;
class AssetPool;

/// Renders a scene into the stack's shared target, through the camera on its
/// view slot.
///
/// **Binds no targets and clears nothing itself.** `RenderStack` has already
/// bound and cleared per this layer's `clear` and `useDepth` policy by the time
/// `onRenderLayer` runs, and a layer that re-bound them would silently undo the
/// stack's compositing — which presents as later layers overwriting earlier ones
/// rather than as a binding mistake.
class HP_API SceneRenderLayer final : public IRenderLayer {
public:
    /// Constructs a layer that renders nothing until it is given a scene.
    /// @param name a stable name for the profiling zone and logs. Must outlive
    ///        the layer; a string literal is the expected answer.
    explicit SceneRenderLayer(const char* name = "scene");

    /// Releases the renderer and everything it created.
    ~SceneRenderLayer() override;

    /// Creates the GPU-side renderer.
    ///
    /// Separate from the constructor because a layer is normally built before
    /// the device is, and because the target formats must match the stack's --
    /// they are baked into the pipeline state.
    ///
    /// **`useDepth` must already be set, and this is the ordering that matters.**
    /// The pipeline state records whether the pass has a depth attachment, so a
    /// HUD layer must be configured before it is created — `configureAsHud`
    /// then `create`, not the other way round. Getting it wrong is caught at the
    /// first frame with a named error rather than a validation failure; see
    /// `onRenderLayer`.
    /// @param device the device to create against. Must not be null.
    /// @param context the immediate context, for one-off setup uploads. Must not
    ///        be null.
    /// @param colour the format of the colour target the stack composites into.
    /// @param depth the format of the stack's depth target. Ignored when
    ///        `useDepth` is false, which builds a pipeline state with no depth
    ///        attachment at all.
    /// @returns whether creation succeeded. False leaves the layer inert rather
    ///          than half-created, and it logs why.
    bool create(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
                TargetFormat colour = TargetFormat::Colour,
                TargetFormat depth = TargetFormat::Depth);

    /// Releases every device resource. Safe to call more than once.
    /// @returns nothing.
    void release();

    /// @returns whether `create` succeeded and this layer can draw.
    [[nodiscard]] bool valid() const;

    /// Points the layer at what it draws.
    ///
    /// Both are borrowed and neither is owned, so **whoever owns them must
    /// outlive the layer or clear this first**. Passing nulls is how a layer is
    /// parked without removing it from the stack.
    /// @param scene the scene to draw, or nullptr.
    /// @param pool where mesh GUIDs resolve, or nullptr.
    /// @returns nothing.
    void setScene(Scene* scene, const AssetPool* pool);

    /// @returns the scene this layer draws, or nullptr.
    [[nodiscard]] Scene* scene() const;

    /// Feeds a texture this layer produced to material shaders, by name
    /// (T0147.4, T0094.5).
    ///
    /// **This is where the game-fed half is meant to be called from.** A
    /// gameplay-authored layer (T0094) renders its fog-of-war field, its flow
    /// map or its minimap into a target it owns, then hands the view here; a
    /// material module that declared a texture of that name samples it. The
    /// engine never learns what the bytes mean, which is the point.
    ///
    /// **Feed it again after a resize** — the view is not kept alive by this
    /// call, matching `FrameTargets`' rule.
    /// @param name the shader-side declaration name.
    /// @param view the view to bind, or null to remove the entry.
    /// @returns nothing.
    void setGameTexture(std::string_view name, Diligent::ITextureView* view);

    /// Scales the environment's contribution for this layer (T0170.5). See
    /// `SceneRenderer::setEnvironmentIntensity`, which this forwards to
    /// unchanged; `0` turns the default sky off.
    /// @param intensity the multiplier. Clamped at 0 from below.
    /// @returns nothing.
    void setEnvironmentIntensity(float intensity);

    /// Draws the scene. See the class comment for what it deliberately does not
    /// do.
    /// @param pass the bound targets and context, from `RenderStack`.
    /// @returns nothing.
    void onRenderLayer(const RenderPassContext& pass) override;

    /// @returns the name given at construction.
    [[nodiscard]] const char* name() const override;

    /// Which camera slot to resolve (T0081.2).
    ///
    /// Slot 0 is the world. A HUD layer uses another, which is how it gets an
    /// orthographic camera without the world knowing anything about it.
    std::uint8_t viewSlot = 0;

    /// What the last `onRenderLayer` did. Read after a frame, for logs and
    /// tests.
    ///
    /// **Statistics are not evidence that anything was drawn.** `submitted`
    /// counts draw calls issued, and a frame where every one of them was
    /// rejected by the depth test reports exactly the same numbers as a correct
    /// one. Assert on pixels.
    DrawSubmitStats lastFrame{};

    /// Whether a camera was found on `viewSlot` last frame.
    ///
    /// **False is not an error.** A scene with no camera for this slot is the
    /// normal state of a scene being built, and of a HUD layer in a scene that
    /// has no HUD.
    bool lastFrameHadCamera = false;

private:
    // No pimpl, and that is the unusual choice worth explaining: every member
    // below is already a Diligent-free type, `SceneRenderer` being itself a
    // pimpl over DiligentFX. A second indirection would hide nothing that is not
    // already hidden and would cost an allocation per layer.
    const char* name_;
    Scene* scene_ = nullptr;
    const AssetPool* pool_ = nullptr;
    SceneRenderer renderer_;

    /// Whether `create` baked a depth attachment into the pipeline state, so a
    /// later disagreement with what the stack binds can be named.
    bool createdWithDepth_ = true;

    /// Reported once, not once per frame — a per-frame error in a render loop
    /// buries everything else in the log within a second.
    bool warnedDepthMismatch_ = false;

    /// Reused between frames so a frame does not allocate a draw list.
    DrawList drawList_;
};

/// Configures a layer as a HUD drawn over the world (27.4).
///
/// Sets the three things that make 2D-over-3D work, all of which are wrong by
/// default for this purpose:
///
///   * **`useDepth = false`** — the trap the per-layer depth policy exists for.
///     A HUD that depth-tests against the world vanishes behind whatever
///     geometry is near the camera, and that reads as flickering UI rather than
///     as a depth bug.
///   * **`clear = LayerClear::None`** — a HUD composites over what is beneath
///     it. Clearing colour would erase the world it is drawn on.
///   * **`order` above the world layer**, and `viewSlot` off 0, so it resolves
///     its own camera.
///
/// The camera itself is content, not engine: put an orthographic `Camera` on an
/// entity with this slot. Nothing here creates one.
/// @param layer the layer to configure.
/// @param slot the view slot its camera lives on. Must not be the world's.
/// @param order the sort key. Above the world layer's, and below whatever
///        tonemapping will eventually sit at (T0096).
/// @returns nothing.
HP_API void configureAsHud(SceneRenderLayer& layer, std::uint8_t slot = 1, int order = 100);

} // namespace hp
