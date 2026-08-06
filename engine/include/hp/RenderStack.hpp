// Composited visual layers (T0027, T0094, D22).
//
// A frame is not one draw. It is a world pass, then whatever gameplay wants on
// top of it — fog of war, a minimap, a portal view — then a HUD, then debug
// overlays. `RenderStack` owns that ordering and nothing else.
//
// **The stack accepts layers implemented outside the engine, and that is the
// point rather than a feature.** D22 measured that Diligent's interfaces are
// pure-virtual, so a gameplay module calls through the pointers in
// `RenderPassContext` with no Diligent library linked — it issues real draw
// calls, with no C API and no command-buffer abstraction in between. What it
// cannot do is create a device: the factories are free functions in libraries
// linked PRIVATE, so a module that tries fails at link. Gameplay may use what it
// is handed and may never make one, and the linker enforces that rather than a
// review.
//
// **Compositing is single-target (27.5), and this is that decision.** Every layer
// draws into the same colour target, in order. Per-layer targets would enable
// per-layer post-processing and cost a full-resolution surface plus a composite
// pass per layer, which is bandwidth spent before anything asked for it. The
// escape hatch is deliberate: a layer that needs its own target renders into one
// it owns and blits, which is what a portal view or a minimap does anyway.
//
// **Depth is per-layer, and getting this wrong is the classic bug.** The HUD must
// not depth-test against the world, or it disappears behind geometry that happens
// to be near the camera. So a layer states whether it wants depth, and the stack
// binds it or does not.
//
// **Ordering relative to tonemapping matters and is not this header's to enforce
// yet.** Tonemapping applies to the world, not to UI drawn on top of it (T0096);
// getting that backwards makes UI look washed out and is a common engine bug.
// When the HDR chain lands, it goes between the world layers and the UI layers,
// which is what `order` exists to express.
#pragma once

#include <hp/Api.hpp>
#include <hp/DepthConvention.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
struct ITextureView;
} // namespace Diligent

namespace hp {

class FrameTargets;

/// Everything a layer needs to draw, handed to it for the duration of one call.
///
/// **Every pointer here is valid for that call and not beyond.** Storing one and
/// using it next frame is a use-after-free the moment the window resizes, since
/// a resize recreates the targets. The device outlives the frame, but even that
/// is destroyed when `RenderLayer` detaches, so a layer holding GPU resources
/// must release them before then.
struct RenderPassContext {
    /// The device, for creating pipelines, buffers and textures.
    Diligent::IRenderDevice* device = nullptr;

    /// The immediate context, for issuing draws. **Not thread-safe** — it
    /// belongs to whichever thread runs the frame.
    Diligent::IDeviceContext* context = nullptr;

    /// The shared frame targets, for a layer that needs to read scene depth or
    /// colour rather than only write. Null when the stack was given none.
    FrameTargets* targets = nullptr;

    /// The colour target every layer composites into. Already bound.
    Diligent::ITextureView* colour = nullptr;

    /// The depth target, or nullptr when this layer asked not to use depth.
    /// Already bound if present.
    Diligent::ITextureView* depth = nullptr;

    /// Target width in pixels.
    int width = 0;

    /// Target height in pixels.
    int height = 0;

    /// The device's texture-space convention (`yToV`), for a layer doing its
    /// own render-to-texture work.
    ///
    /// **Carried here rather than looked up**, because there is no other way
    /// for a gameplay-authored layer to obtain it, and a pass that hardcodes
    /// the V flip samples upside down the day the report changes. (This used
    /// to carry the clip-space Z range too; T0144.3 removed it with the OpenGL
    /// backend, and the projection no longer needs anything from the device.)
    ClipSpace clip{};

    /// The frame's time in seconds (T0159.5), from whatever clock drives the
    /// caller of `RenderStack::render`. A scene layer forwards it to
    /// `SceneRenderer`, where it reaches shaders as `HpSurfaceInput::Time`.
    /// Zero when the caller passes none — defined, never stale.
    double timeSeconds = 0.0;
};

/// What a layer clears before it draws.
enum class LayerClear : std::uint8_t {
    /// Clear nothing — the normal case for a layer drawn on top of another.
    None,

    /// Clear colour only.
    Colour,

    /// Clear depth only. What a layer wants when it draws over an existing image
    /// but needs its own depth ordering, such as a first-person viewmodel that
    /// must not intersect the world.
    Depth,

    /// Clear both. The first layer of a frame usually wants this.
    ColourAndDepth,
};

/// A visual layer. Implement this in the engine, in the editor, or in a gameplay
/// module (T0094) — the stack does not care which.
///
/// The configuration is public data rather than virtual accessors because it is
/// data: a caller flips `enabled` at run time, and routing that through a virtual
/// would buy nothing and cost every implementer four overrides.
class HP_API IRenderLayer {
public:
    /// Constructs a layer with default configuration.
    IRenderLayer() = default;

    /// Virtual, because the stack does not own layers but callers may.
    virtual ~IRenderLayer();

    /// Not copyable: a layer is an identity in a stack, not a value.
    IRenderLayer(const IRenderLayer&) = delete;

    /// Not copyable; see the copy constructor.
    /// @returns nothing -- deleted.
    IRenderLayer& operator=(const IRenderLayer&) = delete;

    /// Draws this layer. Targets and clears are already applied.
    /// @param pass the device, context and bound targets for this call. Nothing
    ///        in it may be stored beyond the call.
    /// @returns nothing.
    virtual void onRenderLayer(const RenderPassContext& pass) = 0;

    /// @returns a stable name, used for the profiling zone (27.6) and for logs.
    ///          Must remain valid for the layer's lifetime; a string literal is
    ///          the expected answer.
    [[nodiscard]] virtual const char* name() const = 0;

    /// Sort key. Lower renders first. Ties keep insertion order, so two layers
    /// at the same order are stable rather than arbitrary.
    int order = 0;

    /// Whether the layer draws this frame. Flipping this is how a layer is
    /// turned off without reordering the stack.
    bool enabled = true;

    /// What to clear before drawing.
    LayerClear clear = LayerClear::None;

    /// Whether to bind the depth target.
    ///
    /// **False is what a HUD wants.** A UI layer that depth-tests against the
    /// world vanishes behind whatever geometry is near the camera, which reads
    /// as flickering UI rather than as a depth bug.
    bool useDepth = true;

    /// Colour used when `clear` includes colour, as linear RGBA in [0, 1].
    float clearColour[4] = {0.0F, 0.0F, 0.0F, 1.0F};

    /// Depth value used when `clear` includes depth.
    ///
    /// `kDepthClearValue`, which is **0.0** because T0130.3 chose reverse-Z: the
    /// far plane is 0, so "nothing drawn yet" is 0. It is spelled as the
    /// constant rather than as a literal so that the day this convention is
    /// revisited, this line moves with it instead of being the one place that
    /// quietly did not.
    float clearDepth = kDepthClearValue;
};

/// An ordered set of layers, composited into one target per frame.
///
/// **Does not own its layers.** A gameplay module's layer is owned by that
/// module, and a module can be unloaded — so ownership here would mean the
/// engine holding a pointer into a library that no longer exists. The rule is
/// the other way round and must be stated where implementers will read it:
/// **whoever adds a layer removes it before destroying it**, and a gameplay
/// module removes its layers on unload.
class HP_API RenderStack {
public:
    /// Constructs an empty stack.
    RenderStack();

    /// Destroys the stack. Does not destroy the layers in it.
    ~RenderStack();

    /// Not copyable: two stacks holding the same layers would render them twice.
    RenderStack(const RenderStack&) = delete;

    /// Not copyable; see the copy constructor.
    /// @returns nothing -- deleted.
    RenderStack& operator=(const RenderStack&) = delete;

    /// Moves the stack.
    /// @param other the stack to move from.
    RenderStack(RenderStack&& other) noexcept;

    /// Moves the stack.
    /// @param other the stack to move from.
    /// @returns this stack.
    RenderStack& operator=(RenderStack&& other) noexcept;

    /// Inserts a layer, keeping the stack sorted by `order`.
    ///
    /// Adding the same layer twice is refused and logged rather than producing a
    /// layer that draws twice — which looks like a blending bug, not a
    /// double-add.
    /// @param layer the layer to insert. Not owned; must outlive its removal.
    /// @returns nothing.
    void add(IRenderLayer* layer);

    /// Removes a layer. Safe to call for a layer that is not present.
    /// @param layer the layer to remove.
    /// @returns whether it was there.
    bool remove(IRenderLayer* layer);

    /// Removes every layer without destroying any.
    /// @returns nothing.
    void clear();

    /// Re-sorts after a layer's `order` changed.
    ///
    /// Explicit rather than sorting every frame: `order` is public data, so the
    /// stack cannot observe a change, and sorting unconditionally would cost a
    /// pass over the list every frame to catch something that happens rarely.
    /// @returns nothing.
    void reorder();

    /// @returns the layers, in render order.
    [[nodiscard]] const std::vector<IRenderLayer*>& layers() const;

    /// @returns how many layers are in the stack, enabled or not.
    [[nodiscard]] std::size_t size() const;

    /// Renders every enabled layer, in order, into `colour`.
    ///
    /// Binds targets and applies each layer's clear policy before calling it, so
    /// a layer that draws nothing still gets its clear — which is what makes a
    /// disabled world layer show background rather than last frame's image.
    /// @param device the device to hand layers.
    /// @param context the immediate context to hand layers.
    /// @param colour the composite colour target. Nothing renders if null.
    /// @param depth the depth target, or nullptr if none exists. Layers with
    ///        `useDepth` set see it; others do not.
    /// @param targets the shared frame targets, or nullptr.
    /// @param width target width in pixels.
    /// @param height target height in pixels.
    /// @param clip the device's texture-space convention, from
    ///        `RenderLayer::clipSpace()`. Required rather than defaulted: a
    ///        default-constructed `ClipSpace` looks plausible and flips
    ///        nothing, which silently inverts a layer's render-to-texture
    ///        sampling.
    /// @param timeSeconds the frame's time in seconds, handed to every layer
    ///        through `RenderPassContext::timeSeconds` (T0159.5). Defaulted to
    ///        zero — unlike `clip`, a missing time is a *defined* value with a
    ///        visible symptom (nothing animates), not a silent inversion.
    /// @returns how many layers actually rendered, which is what a test asserts
    ///          on to prove `enabled` is honoured.
    std::size_t render(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
                       Diligent::ITextureView* colour, Diligent::ITextureView* depth,
                       FrameTargets* targets, int width, int height, ClipSpace clip,
                       double timeSeconds = 0.0);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hp
