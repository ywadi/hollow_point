// Lights, and resolving them for a frame (T0079).
//
// **The shader-side light model is not ours and was not designed here** — D24
// adopted `PBRLightAttribs` as the engine's vocabulary, and it already specifies
// directional, point and spot lights with colour, intensity, range and a spot
// cone. This header is the *authoring* side of that: what a designer puts on an
// entity, in units a person can reason about, converted at the edge.
//
// Two things deliberately absent, for the same reason they are absent from
// `Camera`:
//
// - **Position and direction.** They come from the entity's `WorldTransform`
//   (T0101), so a light on a boom arm, a head socket or a swinging lamp works
//   with no special case. A light is not exempt from the hierarchy.
// - **Anything about shadows.** A light that casts is T0086's, including the
//   separate shadow-casting mask that T0085.5 moved there. Adding a
//   `castsShadows` bool here now would be a field nothing reads, which is the
//   mistake `Camera::cullingMask` spent three tickets being.
//
// **The direction convention is glTF's**: a light points along the **negative Z**
// axis of its local space, matching `KHR_lights_punctual`. That is what the
// importer will produce and what `GLTF_PBR_Renderer` assumes, so choosing
// anything else here would mean converting at two edges instead of none.
#pragma once

#include <hp/Api.hpp>
#include <hp/Layers.hpp>
#include <hp/Math.hpp>
#include <hp/Scene.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hp {

/// What shape a light throws.
enum class LightType : std::uint8_t {
    /// Parallel rays from infinitely far away — the sun. Position is ignored;
    /// only orientation matters.
    Directional,

    /// A point radiating in every direction, attenuating with distance.
    Point,

    /// A cone. Attenuates with distance and falls off between the inner and
    /// outer cone angles.
    Spot,
};

/// How many lights a frame can carry.
///
/// Sixteen, which is `PBR_Renderer`'s own default. **It is not free**: the frame
/// attributes buffer is sized `CameraAttribs * 2 + renderer params +
/// PBRLightAttribs * count`, so raising it grows a buffer written every frame.
/// Lowering it is the cheaper mistake.
inline constexpr std::size_t kMaxLights = 16;

/// A light source. Position and orientation come from the entity (T0101).
struct Light {
    /// What shape the light throws.
    LightType type = LightType::Directional;

    /// Linear RGB colour, normally in [0, 1]. Values above 1 are allowed and
    /// meaningful once T0096's HDR chain exists.
    float3 colour{1.0F, 1.0F, 1.0F};

    /// Brightness multiplier.
    ///
    /// **Unitless here, deliberately.** glTF's punctual lights are in lux for
    /// directional and candela for point/spot, and honouring that properly means
    /// a photometric pipeline that T0096 owns. Until exposure and tonemapping
    /// exist, a physical unit would be precision about a number nothing yet
    /// interprets physically. Revisit with T0096, not before.
    float intensity = 1.0F;

    /// How far a point or spot light reaches, in metres. Ignored for a
    /// directional light.
    ///
    /// Attenuation follows glTF's recommendation:
    /// `clamp(1 - (d/range)^4, 0, 1) / d^2`.
    float range = 10.0F;

    /// Half-angle of the spot's full-brightness centre, in radians. Ignored
    /// unless `type` is `Spot`.
    float innerConeAngle = 0.4363F; // 25 degrees

    /// Half-angle at which a spot light reaches zero, in radians. Must be
    /// greater than `innerConeAngle`, and no more than pi/2. Ignored unless
    /// `type` is `Spot`.
    float outerConeAngle = 0.6109F; // 35 degrees

    /// Whether this light contributes at all. The cheap way to switch a light
    /// off without destroying it or moving it away.
    bool enabled = true;

    /// Which object layers this light illuminates (T0085.4).
    ///
    /// Tested against a `MeshRenderer::layers` during selection — put the player
    /// on its own layer and a light on a mask excluding it, and that light does
    /// not touch the player.
    ///
    /// **Added with the code that honours it**, not before. `Camera::cullingMask`
    /// spent three tickets stored and unread, and that is the mistake this field
    /// deliberately avoided repeating until 79.3 existed to apply it.
    ///
    /// Shadow casting is a **separate** mask and belongs to T0086: "lit by this
    /// light but does not cast its shadow" is a common requirement, so the two
    /// must not be collapsed into one field.
    LayerMask layers = LayerMask::all();
};

/// Where something in the hierarchy is, and which way it faces.
///
/// **Split out of `ResolvedLight` so T0093 does not fork it.** Vision cones and
/// radial reveals are cone- and point-shaped projectors that reuse light-shaped
/// machinery but are emphatically **not** lights — a vision source must not
/// illuminate — and T0079's cross-ticket obligation says in as many words that
/// gathering must stay reusable for a component that does not shade, "or T0093
/// ends up forking it".
///
/// This is the part that would have been forked: deriving a world position and a
/// facing direction from a transform, including the glTF negative-Z convention
/// and the degenerate-matrix guard. It is three lines that are wrong in two
/// interesting ways, which is exactly the kind of thing that must exist once.
struct ResolvedPlacement {
    /// World-space position. Meaningless for a directional light.
    float3 position{};

    /// Normalised world-space facing, **along the entity's negative Z** per
    /// glTF's `KHR_lights_punctual`.
    float3 direction{0.0F, 0.0F, -1.0F};
};

/// Derives a placement from a world matrix (T0101).
///
/// @param world the entity's world transform.
/// @returns the position and facing. A degenerate (zero-scaled) matrix yields a
///          default facing rather than a NaN — a NaN here would propagate into
///          the shading of *every* object rather than of the one entity that is
///          broken.
[[nodiscard]] HP_API ResolvedPlacement resolvePlacement(const float4x4& world);

/// A light resolved for one frame: the lamp, and where it is.
///
/// A value, like `ResolvedView`, and meaningless after the frame that produced
/// it. Recompute rather than storing.
struct ResolvedLight {
    /// The entity the light lives on.
    entt::entity entity{entt::null};

    /// A copy of the lamp, so a consumer does not need the scene.
    Light light{};

    /// World-space position. Meaningless for a directional light.
    float3 position{};

    /// World-space direction the light points, normalised. **Along the
    /// entity's negative Z**, per glTF.
    float3 direction{0.0F, 0.0F, 1.0F};
};

/// Every light that will contribute to a frame, in registry order.
using LightList = std::vector<ResolvedLight>;

/// Collects the lights in a scene.
///
/// Filters to entities carrying both a `WorldTransform` and an **enabled**
/// `Light`, and takes position and direction from the world matrix.
///
/// **Frustum culling and per-object selection are not here** (79.2/79.3). This
/// returns what exists, capped, and the cap is applied in registry order —
/// which is *not* a stable guarantee, so a scene with more than `maxLights`
/// lights gets an arbitrary subset and says so in the log. Choosing *which*
/// lights matter per object is the design decision the ticket calls out, and it
/// is deliberately not made by accident here.
///
/// @param scene the scene to walk. Not modified.
/// @param maxLights the cap, normally `kMaxLights`.
/// @returns the lights, empty when the scene has none — which is not an error
///          and renders an unlit (black) frame.
[[nodiscard]] HP_API LightList gatherLights(const Scene& scene,
                                            std::size_t maxLights = kMaxLights);

/// Picks the lights that matter to one object (T0079.3, T0085.4).
///
/// **This is the design decision the ticket names**, and the answer here is
/// deliberately the simple one: filter by layer, then take the nearest N by
/// distance from the light to the object, with directional lights always kept
/// because they have no position to be far from.
///
/// Nearest-N is chosen over tiled or clustered forward because it is the option
/// that can be *measured* against a real scene before a harder one is justified
/// — clustered forward is substantially more work and changes the shape of the
/// frame. **Its known weakness is popping**: when an object moves, the set can
/// change abruptly and the lighting jumps. Sorting by distance alone is exactly
/// what causes that, so this is the line to revisit first when it shows up,
/// rather than the conclusion that nearest-N was wrong.
///
/// ## The order is a contract, and `out[0]` is the dominant light (T0145)
///
/// **Nearest, then brightest, then fixed** — a total order:
///
/// 1. ascending distance from the object, with every directional light at 0, so
///    the suns precede every lamp that is not exactly on top of the object;
/// 2. descending Rec. 709 luminance of `colour * intensity`, so the brightest
///    of several equally-near lights wins;
/// 3. ascending entity, so two identical lamps still sort the same way on every
///    run and a frame is reproducible.
///
/// **Step 2 exists because its absence was a bug, found the expensive way.**
/// Until T0145 the comparison was distance alone and `std::sort` is *unstable*,
/// so with two directional lights — both at distance 0 — which one landed at
/// index 0 was unspecified. On the machine it was measured on it was the dim
/// fill, and the rock cube sample's parallax shadow march spent its one
/// expensive ray on the wrong light while reading as correct in its own source
/// (T0158/T0159). The sample ranked by intensity itself to work around it; that
/// workaround is now the engine's rule.
///
/// A shader sees this order as `HpLight::Index`, and `index == 0` is documented
/// there as the dominant light. A technique that can afford one expensive light
/// — a shadow march, a shaped specular, a ramp with a second tint — spends it
/// on that one.
///
/// @param lights every light in the frame, from `gatherLights`.
/// @param objectPosition the object's world position.
/// @param objectLayers the object's `MeshRenderer::layers`.
/// @param maxLights how many to keep. Clamped to `kMaxLights`.
/// @param out filled with the chosen lights, dominant first per the order
///        above. Cleared first, and reused across draws so selection does not
///        allocate per object.
/// @returns nothing.
HP_API void selectLightsFor(const LightList& lights, const float3& objectPosition,
                            LayerMask objectLayers, std::size_t maxLights, LightList& out);

} // namespace hp
