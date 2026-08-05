// Object layers and the masks that filter them (T0085).
//
// **One definition, used by three subsystems**, which is the whole point: a
// camera's culling mask, a light's illumination mask and — when T0051 arrives —
// a physics collision matrix all mean the same thing by "layer 3". Two
// definitions would drift, and the drift is invisible until an object is lit by
// a light that should not reach it.
//
// **An object carries layers; a viewer carries a mask.** A mesh says which
// layers it is on, a camera or light says which layers it affects, and the test
// is one AND. That asymmetry is deliberate — it reads the same way round as the
// question being asked ("does this light reach this object?") and it is what
// keeps the test a single instruction in the hottest loop in the renderer.
//
// **Layers are not gameplay tags (T0074), and conflating them is a real
// mistake.** Layers are a small fixed set filtered in hot loops with one AND;
// tags are unlimited, hierarchical, and looked up. Use layers for what the
// *engine* filters, tags for what the *game* means.
//
// **Filtering happens during culling, never per pixel.** A shader-side mask test
// wastes the entire cost of drawing the object. `parseScene` is where the camera
// filter lives, and per-object light selection is where the light filter lives,
// so excluded work is never submitted.
//
// This header includes nothing, so the hot paths that need it do not pay for
// anything else.
#pragma once

#include <cstdint>

namespace hp {

/// How many distinct layers exist.
///
/// **32, because a `uint32` mask is one register** and the test is one AND.
/// Going wider costs in the hottest loop in the renderer to buy something that
/// is almost never needed — every engine that has picked this number has picked
/// 32, and none of them regret it.
inline constexpr int kMaxLayers = 32;

/// The layer everything starts on.
///
/// Zero rather than "unassigned": an object with no opinion must still be
/// visible to a camera with no opinion, and a nullable layer would make the
/// common case the one that needs handling.
inline constexpr int kDefaultLayer = 0;

/// A set of layers, as a bitmask.
///
/// Used both as "the layers this object is on" and as "the layers this viewer
/// affects" — the two are the same type because the test between them is
/// symmetric, and giving them separate types would buy nothing but conversions.
///
/// A value type with no invariants to break: any bit pattern is meaningful, and
/// the empty mask legitimately means "nothing", which is how a light is switched
/// off without destroying it.
struct LayerMask {
    /// The raw bits. Public because this is a value, not an encapsulation —
    /// serialisation, reflection (T0053) and an inspector widget all want it.
    std::uint32_t bits = 0;

    /// Constructs an empty mask, matching nothing.
    constexpr LayerMask() = default;

    /// Constructs from raw bits.
    /// @param value the bit pattern.
    constexpr explicit LayerMask(std::uint32_t value) : bits(value) {}

    /// @returns a mask matching every layer. The default for a camera or light
    ///          that has not been told otherwise.
    [[nodiscard]] static constexpr LayerMask all() { return LayerMask{0xFFFFFFFFU}; }

    /// @returns a mask matching nothing.
    [[nodiscard]] static constexpr LayerMask none() { return LayerMask{0U}; }

    /// A mask containing exactly one layer.
    /// @param index the layer, 0 to `kMaxLayers - 1`. **Out of range yields an
    ///        empty mask** rather than wrapping — a wrapped shift is undefined
    ///        behaviour, and silently aliasing layer 32 onto layer 0 is the kind
    ///        of bug that presents as an unrelated object being lit.
    /// @returns the mask.
    [[nodiscard]] static constexpr LayerMask layer(int index) {
        return (index < 0 || index >= kMaxLayers)
                   ? LayerMask{0U}
                   : LayerMask{static_cast<std::uint32_t>(1U) << static_cast<unsigned>(index)};
    }

    /// Whether this mask and another share any layer.
    ///
    /// **The one test the hot loops run.** Reads as "does this viewer's mask
    /// intersect this object's layers", which is the question being asked.
    /// @param other the other mask.
    /// @returns true when at least one layer is in both.
    [[nodiscard]] constexpr bool intersects(LayerMask other) const {
        return (bits & other.bits) != 0U;
    }

    /// @param index the layer to test.
    /// @returns whether that layer is in this mask.
    [[nodiscard]] constexpr bool has(int index) const { return intersects(layer(index)); }

    /// Adds a layer.
    /// @param index the layer to add. Out of range is ignored.
    /// @returns nothing.
    constexpr void add(int index) { bits |= layer(index).bits; }

    /// Removes a layer.
    /// @param index the layer to remove. Out of range is ignored.
    /// @returns nothing.
    constexpr void remove(int index) { bits &= ~layer(index).bits; }

    /// @returns whether this mask matches nothing.
    [[nodiscard]] constexpr bool empty() const { return bits == 0U; }

    /// @param other the mask to compare with.
    /// @returns whether both masks hold the same layers.
    [[nodiscard]] constexpr bool operator==(const LayerMask& other) const {
        return bits == other.bits;
    }
};

/// The layer set an object with no explicit opinion belongs to.
/// @returns a mask containing only `kDefaultLayer`.
[[nodiscard]] constexpr LayerMask defaultObjectLayers() {
    return LayerMask::layer(kDefaultLayer);
}

} // namespace hp
