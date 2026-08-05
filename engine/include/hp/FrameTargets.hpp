// Named, explicitly-managed render targets for a frame (T0046, D22).
//
// Passes share resources: a depth buffer the world pass writes and
// post-processing reads, a ping-pong pair for blurs, the offscreen colour target
// the editor viewport displays. Something has to own their creation, resizing
// and lifetime, and this is deliberately that and nothing more.
//
// **Not a render graph.** T0047 owns that question if it is ever asked. In
// particular there is **no resource aliasing** here — reusing memory between
// non-overlapping targets is the main thing a render graph automates, and doing
// it by hand produces intermittent corruption in exactly the way that costs days
// to find. If memory pressure ever justifies it, that is an argument for T0047,
// not for hand-rolling it here.
//
// **Diligent types are named, and that is deliberate (D22).** The interfaces are
// pure-virtual, so a gameplay module can call through the pointers this hands out
// with no Diligent library linked — which is what makes a gameplay-authored pass
// (T0094) able to consume a frame target exactly as an engine pass does. They are
// forward-declared rather than included: the pointers are all this header needs,
// and `BasicMath.hpp` already costs a consumer 146,000 preprocessed lines (D21)
// without adding the RHI to every translation unit that wants a render target.
//
// **Nothing here is refcounted across the boundary.** This object owns its
// targets; callers borrow them. A `RefCntAutoPtr` in this API would let a
// gameplay module hold a target alive past a resize, which is precisely the leak
// this exists to prevent.
#pragma once

#include <hp/Api.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
struct ITextureView;
} // namespace Diligent

namespace hp {

/// What a target is for, which is what decides its pixel format.
///
/// A role rather than a format, so formats are chosen in **one** place
/// (`FrameTargets.cpp`) rather than being spelled out at each declaration and
/// drifting apart. That is 46.4's requirement stated as a type.
enum class TargetFormat : std::uint8_t {
    /// Display-referred colour, matching the swap chain. For anything composited
    /// straight to the screen.
    Colour,

    /// Linear high-dynamic-range colour, for the world pass before tonemapping
    /// (T0096). Sixteen-bit float per channel.
    ColourHDR,

    /// Depth. Single-channel 32-bit float.
    ///
    /// `D32_FLOAT` rather than `D24S8`, and the reason is forward-looking: a
    /// float depth buffer is what makes **reverse-Z** possible, and reverse-Z is
    /// the standard fix for depth precision collapsing at distance. Whether to
    /// actually use reverse-Z is T0130.3's decision and is **not** made here —
    /// but choosing this format costs nothing today and is what keeps that
    /// decision open. `D24S8` would foreclose it silently.
    ///
    /// No stencil. Nothing needs one yet, and a target that carries an unused
    /// stencil is memory spent on nothing.
    Depth,
};

/// One declared target.
struct FrameTargetDesc {
    /// The name passes look it up by. Must be unique within a `FrameTargets`.
    std::string name;

    /// What it is for, which decides its format.
    TargetFormat format = TargetFormat::Colour;

    /// Size relative to the viewport, so a half-resolution bloom buffer is
    /// `0.5F` rather than a second size to keep in sync. Clamped to at least one
    /// pixel in each dimension — a zero-sized texture is a device error on some
    /// backends and silently ignored on others, which is worse.
    float scale = 1.0F;
};

/// Owns a named set of render targets, created once and resized with the
/// viewport rather than rebuilt per frame.
class HP_API FrameTargets {
public:
    /// Constructs an empty set. Declares nothing and allocates no GPU memory.
    FrameTargets();

    /// Releases every target.
    ~FrameTargets();

    /// Not copyable: a copy would own the same textures and release them twice.
    FrameTargets(const FrameTargets&) = delete;

    /// Not copyable; see the copy constructor.
    /// @returns nothing -- deleted.
    FrameTargets& operator=(const FrameTargets&) = delete;

    /// Moves the set, leaving the source empty.
    /// @param other the set to move from.
    FrameTargets(FrameTargets&& other) noexcept;

    /// Moves the set, releasing whatever this one held.
    /// @param other the set to move from.
    /// @returns this set.
    FrameTargets& operator=(FrameTargets&& other) noexcept;

    /// Declares a target. Call before `create`; declaring after it is a no-op
    /// and says so, because silently ignoring it would produce a null lookup
    /// far from the mistake.
    /// @param desc the target to declare.
    /// @returns nothing.
    void declare(FrameTargetDesc desc);

    /// Creates every declared target at the given size.
    /// @param device the device to create on. Must outlive this object.
    /// @param width viewport width in pixels.
    /// @param height viewport height in pixels.
    /// @returns false if any target failed, having released the rest — a
    ///          half-created set is the state that produces a null lookup three
    ///          passes later with nothing pointing at the cause.
    bool create(Diligent::IRenderDevice* device, int width, int height);

    /// Recreates every target at a new size.
    ///
    /// A no-op when the size is unchanged, which is what makes it safe to call
    /// every frame — 46.3 asks for debouncing, and this is where it lives rather
    /// than in every caller.
    /// @param width new viewport width in pixels.
    /// @param height new viewport height in pixels.
    /// @returns false if recreation failed; the set is then empty.
    bool resize(int width, int height);

    /// Releases every target. Safe to call more than once.
    /// @returns nothing.
    void release();

    /// Looks up a colour target's render-target view.
    /// @param name the declared name.
    /// @returns the view, or nullptr when the name is unknown, the set is not
    ///          created, or the target is a depth target. **Valid for this frame
    ///          only** — a resize invalidates it, so do not cache it across
    ///          frames.
    [[nodiscard]] Diligent::ITextureView* renderTarget(std::string_view name) const;

    /// Looks up a depth target's depth-stencil view.
    /// @param name the declared name.
    /// @returns the view, or nullptr when the name is unknown or the target is
    ///          not a depth target. Same single-frame lifetime as
    ///          `renderTarget`.
    [[nodiscard]] Diligent::ITextureView* depthStencil(std::string_view name) const;

    /// Looks up any target's shader-resource view, for a pass that reads it.
    ///
    /// Depth targets have one too, which is what lets the transparent pass fade
    /// soft particles against scene depth (T0106.5) and what a later distortion
    /// pass will need from scene colour.
    /// @param name the declared name.
    /// @returns the view, or nullptr when the name is unknown. Same single-frame
    ///          lifetime as `renderTarget`.
    [[nodiscard]] Diligent::ITextureView* shaderResource(std::string_view name) const;

    // --- ping-pong (46.5) ---------------------------------------------------
    //
    // A multi-pass effect -- a separable blur, an iterative bloom downsample --
    // alternates between two targets: read A write B, then read B write A. It
    // is expressible with two `declare` calls and always has been; what these
    // add is the parity, and the parity is the part that gets written wrong.
    //
    // **Binding a texture as both source and target is the failure this
    // prevents.** It is undefined behaviour, it usually *appears* to work, and
    // what it produces is a subtly wrong image that nobody traces back to the
    // bind. Asking for "the target for pass N" rather than tracking a boolean
    // means a caller cannot get the sense inverted.

    /// Declares a pair of identical targets for a multi-pass effect.
    ///
    /// Creates `"<name>.a"` and `"<name>.b"`, both with `desc`'s format and
    /// scale. They are ordinary targets: `renderTarget` and `shaderResource`
    /// work on the suffixed names, so a debug view can show either.
    /// @param desc the pair's shared description. Its `name` is the pair's name.
    /// @returns nothing.
    void declarePingPong(FrameTargetDesc desc);

    /// The target a given pass writes into.
    /// @param name the pair's name, as declared.
    /// @param pass the zero-based pass index. Even passes write `.b`, odd write
    ///        `.a`, so pass 0 reads the `.a` an earlier stage filled.
    /// @returns the render-target view, or nullptr when the pair is unknown or
    ///          not created.
    [[nodiscard]] Diligent::ITextureView* pingPongTarget(std::string_view name, int pass) const;

    /// The target a given pass reads from — always the other one.
    /// @param name the pair's name, as declared.
    /// @param pass the zero-based pass index, the same value passed to
    ///        `pingPongTarget`.
    /// @returns the shader-resource view, or nullptr when the pair is unknown or
    ///          not created. **Never the same texture `pingPongTarget` returns
    ///          for the same pass**, which is the whole point.
    [[nodiscard]] Diligent::ITextureView* pingPongSource(std::string_view name, int pass) const;

    /// @param name the pair's name.
    /// @returns whether a ping-pong pair was declared under that name.
    [[nodiscard]] bool hasPingPong(std::string_view name) const;

    /// Copies a colour target back to CPU memory.
    ///
    /// **Slow, and deliberately so** — it flushes and waits for the GPU to go
    /// idle. This is for tests, screenshots and thumbnail generation (T0120.2),
    /// never for a per-frame path.
    ///
    /// It lives here rather than in each consumer because Diligent is linked
    /// PRIVATE: nothing outside the engine can reach the RHI to do it, so a test
    /// that wants to assert on pixels has no other way to get them. `SceneView`
    /// delegates to this rather than carrying a second copy.
    /// @param context the immediate context. Must not be null.
    /// @param name the declared name of a colour target. A depth target is
    ///        refused — its format is not four bytes of RGBA and pretending
    ///        otherwise would return plausible nonsense.
    /// @param outRgba filled with `width() * height() * 4` bytes, row-major from
    ///        the top-left, 8 bits per channel **in the target's own encoding**
    ///        — which is sRGB for `TargetFormat::Colour`, so these are not
    ///        linear values. Cleared on failure.
    /// @returns whether the readback succeeded.
    bool readback(Diligent::IDeviceContext* context, std::string_view name,
                  std::vector<std::uint8_t>& outRgba) const;

    /// @returns whether every declared target currently exists.
    [[nodiscard]] bool ready() const;

    /// @returns the current viewport width in pixels, or 0 before creation.
    [[nodiscard]] int width() const;

    /// @returns the current viewport height in pixels, or 0 before creation.
    [[nodiscard]] int height() const;

    /// @returns GPU memory currently held by these targets, in bytes (46.6).
    ///
    /// Computed from the declared formats and sizes rather than queried from the
    /// driver, so it is what we asked for rather than what was allocated —
    /// alignment and tiling mean the real figure is somewhat larger. Good enough
    /// to answer "what is eating VRAM", which is the question it exists for.
    [[nodiscard]] std::uint64_t memoryBytes() const;

    /// @returns the declared targets, in declaration order, for a profiler or
    ///          debug panel that wants to list them.
    [[nodiscard]] const std::vector<FrameTargetDesc>& declared() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hp
