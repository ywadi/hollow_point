// What a camera describes, and the projection it produces (T0130).
//
// Position and orientation are **not** here. They come from the entity's
// `Transform`, and T0101's propagation is what makes a camera on a boom arm or
// a head socket work. This header owns the lens and nothing else.
//
// ---
//
// **The parameterisation is artistic, not physical (130.1).** `verticalFov` is
// the stored truth; focal length is a conversion at the edge.
//
// A physical model — focal length in millimetres against a sensor size — was
// the real alternative, and it is not obviously worse. It gives artists and
// photographers a vocabulary they already share, and it makes depth of field
// physically meaningful rather than two tuning knobs. It was rejected on where
// the conversions land: **every consumer of a camera needs the field of view**
// — the projection matrix, the culling frustum, the editor's gizmo, any code
// framing a target — and none of them needs millimetres. Storing focal length
// means converting at each of those sites, and a conversion repeated at N sites
// is N chances to use a different sensor size. Storing both invites the two to
// disagree, which is the bug that cannot be seen in an inspector.
//
// So: one stored number, conversions available as free functions, and
// `sensorHeightMm` as the reference frame that makes those conversions
// meaningful — because "50mm" says nothing without a sensor to measure it
// against, and a hidden constant is exactly how a 50mm camera comes out looking
// wrong.
//
// **Aspect ratio is not stored (130.2).** It is derived from the viewport and
// passed to `projectionMatrix`. A stored aspect is correct until the first
// window resize and subtly wrong forever after — output stretched by a few
// percent, which nobody spots for weeks and which no test catches unless it was
// written to.
//
// **Depth is reverse-Z (130.3), and that decision reaches further than this
// header.** It is stated and argued in `hp/DepthConvention.hpp`, which is a
// separate header precisely so the things that must honour it -- the render
// stack's clear value, every pipeline state in T0028 -- can read it without
// including Diligent.
#pragma once

#include <hp/Api.hpp>
#include <hp/DepthConvention.hpp>
#include <hp/Layers.hpp>
#include <hp/Math.hpp>
#include <hp/Render.hpp>

#include <cstdint>

namespace hp {

/// The height of the reference sensor, in millimetres, for a camera that does
/// not name its own.
///
/// 24mm is full-frame 35mm (36 x 24), which is the frame most people mean when
/// they say "50mm looks like this".
inline constexpr float kDefaultSensorHeightMm = 24.0F;

/// What happens to the framing when the window is not the shape the camera was
/// authored for (T0081.10).
///
/// **This is a fairness question before it is a rendering one**, which is why it
/// is a policy rather than a constant. On a 21:9 monitor, free aspect literally
/// shows more world than 16:9 — an advantage in any game where what you can see
/// is a mechanic.
///
/// **Per camera rather than global, and that is deliberate.** A world camera can
/// clamp for fairness while a HUD's orthographic camera stays free, and
/// letterboxing a HUD camera is never the right answer. A single global setting
/// could not express that. It is a plain field, so gameplay assigns it directly
/// (D12) and the next frame's projection picks it up — there is no invalidation
/// step to forget.
enum class AspectPolicy : std::uint8_t {
    /// Vertical field of view is preserved; a wider window shows more at the
    /// sides. The engine default, because it is what every other engine does and
    /// what `verticalFov` being the stored truth already implies.
    FreeAspect,

    /// Horizontal extent is held to `referenceAspect`; a wider window gains
    /// vertical view instead of horizontal. No bars, and no ultrawide advantage
    /// — the compromise competitive games usually ship.
    ClampHorizontalFov,

    /// The reference framing is preserved exactly and a wider window gets bars.
    /// Strictest fairness and the most predictable composition, at the cost of
    /// unused screen.
    ///
    /// **The bars are not drawn here.** This policy only shapes the projection;
    /// the viewport rect it implies comes from `letterboxViewport`, and whatever
    /// is outside it is simply never rendered into.
    Letterbox,
};

/// A rectangle of a render target, in normalised coordinates.
///
/// Normalised rather than pixels so it survives a resize: split-screen halves
/// stay halves, and a picture-in-picture inset stays the same fraction of the
/// screen, without anything recomputing them (T0081.4).
///
/// The origin is the top-left corner, matching the render target rather than
/// OpenGL's bottom-left convention — the engine has one texture-space
/// convention and this follows it.
struct ViewportRect {
    /// Left edge, 0 to 1.
    float x = 0.0F;

    /// Top edge, 0 to 1.
    float y = 0.0F;

    /// Width as a fraction of the target. 1 is the full width.
    float width = 1.0F;

    /// Height as a fraction of the target. 1 is the full height.
    float height = 1.0F;

    /// @returns whether the rectangle covers a non-empty area inside the target.
    [[nodiscard]] constexpr bool valid() const {
        return width > 0.0F && height > 0.0F && x >= 0.0F && y >= 0.0F && x + width <= 1.0001F
               && y + height <= 1.0001F;
    }
};

/// What a view draws instead of the shaded image (T0141).
///
/// **A surface has half a dozen inputs and one output, so a wrong one is
/// invisible in the shaded frame.** A normal map that is never applied, an
/// occlusion channel read from the wrong component, a roughness that is always
/// 1 — each of those produces an image that still looks like a lit material,
/// and none of them can be told apart by eye or by an average colour. Rendering
/// a single channel on its own is how each becomes a thing you can look at, and
/// it is what every engine with a material editor provides.
///
/// **The values match DiligentFX's `PBR_Renderer::DebugViewType`**, which is
/// what `PBRRendererShaderParameters::DebugView` is declared to carry. The
/// engine cannot name that enum in a public header (D21 keeps Diligent types
/// out), so it is restated — and restated *with their numbering*, so the two
/// cannot disagree about what a stored `3` means. Only the modes the engine's
/// shader actually implements appear here; the gaps in the sequence are theirs,
/// and a mode is added when the thing it shows exists.
enum class SurfaceDebugView : std::uint8_t {
    /// The shaded image. The default, and the only one a game ever ships.
    None = 0,

    /// The first texture coordinate set as red and green.
    ///
    /// **The one to reach for first when a texture looks wrong**, because it
    /// separates "the sampler is broken" from "the mesh's UVs are not what you
    /// think". A correct unit quad is a black-to-yellow gradient corner to
    /// corner; anything else is a mesh problem, and no amount of staring at the
    /// shaded frame would have told you that.
    Texcoord0 = 1,

    /// Base colour after the texture and every factor, before any lighting.
    BaseColor = 3,

    /// The occlusion channel, as greyscale.
    Occlusion = 5,

    /// Emissive colour.
    Emissive = 6,

    /// Metalness, as greyscale. 0 is a dielectric and 1 is a metal.
    Metallic = 7,

    /// Roughness, as greyscale. 0 is a mirror and 1 is fully rough.
    Roughness = 8,

    /// The interpolated geometric normal, before any normal map.
    ///
    /// Shown as `normal * 0.5 + 0.5`, the usual encoding, so +X is red, +Y is
    /// green and +Z is blue.
    MeshNormal = 12,

    /// The normal the lighting actually used, after the normal map.
    ///
    /// **Meaningful only next to `MeshNormal`.** A flat quad's mesh normal is
    /// one constant colour, so if this looks the same, the normal map is not
    /// being applied — which is exactly the failure that is invisible in a
    /// shaded frame.
    ShadingNormal = 13,
};

/// A point of view. Which camera renders, and into what viewport, is not
/// decided here (T0081).
struct Camera {
    /// Vertical field of view in radians. Ignored when `orthographic` is set.
    ///
    /// **Vertical rather than horizontal, and stored rather than derived.** A
    /// vertical field of view is stable when the window changes shape: an
    /// ultrawide monitor shows more at the sides rather than cropping the top
    /// and bottom, which is what a player expects and what makes a camera
    /// authored on one aspect usable on another.
    float verticalFov{1.0472F}; // 60 degrees

    /// Near clip plane distance, in metres.
    ///
    /// Must be greater than zero. Reverse-Z makes this far less sensitive than
    /// it is under a conventional depth buffer — where pushing the near plane
    /// from 0.1 to 0.01 visibly destroys distant precision — but it does not
    /// make it free, and it cannot be zero at all.
    float nearPlane{0.1F};

    /// Far clip plane distance, in metres.
    float farPlane{1000.0F};

    /// Whether to use an orthographic projection instead of a perspective one.
    bool orthographic{false};

    /// Half-height of the orthographic view volume, in metres. Ignored unless
    /// `orthographic` is set.
    float orthographicSize{5.0F};

    /// Height of the reference sensor in millimetres.
    ///
    /// Not used by the projection at all. It is the frame of reference that
    /// makes `focalLengthFromVerticalFov` and depth of field mean something,
    /// and it is stored per camera rather than fixed as a constant so a
    /// cinematic camera and a gameplay camera can disagree about it without one
    /// of them being wrong.
    float sensorHeightMm{kDefaultSensorHeightMm};

    /// Exposure, as EV100 (130.4).
    ///
    /// **Exposure lives on the camera, and the argument is that it is a
    /// property of a view rather than of a scene.** A frame can contain several
    /// — a main view, a security monitor, a portal — looking at the same world
    /// and needing different exposures. A single post-process exposure cannot
    /// express that without one of them being wrong.
    ///
    /// **T0096 owns the tonemap curve and any auto-exposure, and the precedence
    /// is that auto-exposure writes this field rather than shadowing it**, so
    /// there is exactly one number and the inspector shows what is actually
    /// being applied. A second exposure on the post-process stack would silently
    /// multiply with this one, which is a class of bug where every individual
    /// value looks reasonable.
    ///
    /// 13 is roughly a bright indoor scene. Use `exposureMultiplierFromEv100`
    /// to get the linear multiplier a shader wants.
    float exposureEv100{13.0F};

    /// Whether depth of field applies to this camera (130.5).
    ///
    /// **The storage is here; the pass that implements it is not this ticket's
    /// work** and does not exist yet. That split is deliberate: two floats and a
    /// bool cost nothing now, and adding them after cameras have been authored
    /// into scenes and prefabs costs a component migration.
    bool depthOfField{false};

    /// Lens aperture as an f-number — 1.4 is wide open with shallow focus, 22 is
    /// nearly everything in focus. Ignored unless `depthOfField` is set.
    float aperture{5.6F};

    /// Distance to the plane in perfect focus, in metres. Ignored unless
    /// `depthOfField` is set.
    float focusDistance{10.0F};

    /// What happens when the window is not `referenceAspect` (T0081.10).
    AspectPolicy aspectPolicy{AspectPolicy::FreeAspect};

    /// The aspect this camera was framed for. Ignored under
    /// `AspectPolicy::FreeAspect`, which is why it can carry a sensible value
    /// without implying a policy.
    float referenceAspect{16.0F / 9.0F};

    /// Where in the render target this camera draws (T0081.4).
    ///
    /// Normalised, so split-screen and picture-in-picture survive a resize
    /// without recomputation.
    ViewportRect viewport{};

    /// Which camera wins when several are active. Highest renders.
    ///
    /// Ties are broken by the order entities appear in the registry, which is
    /// **not** a stable guarantee — two cameras at the same priority is a
    /// content bug, and `resolveCamera` logs it rather than picking silently.
    int priority{0};

    /// Whether this camera is a candidate at all. The cheap way to switch
    /// cameras without destroying or reordering anything (T0081.2).
    bool enabled{true};

    /// Which object layers this camera renders (T0081.5, T0085).
    ///
    /// **Not a `RenderStack` layer, and confusing the two is the trap this
    /// comment exists for.** A `RenderStack` layer is a compositing pass — world,
    /// then HUD. This is a bitmask over *object* layers, tested per object during
    /// culling. A weapon viewmodel uses both: its own compositing layer, and a
    /// mask selecting only viewmodel objects.
    ///
    /// **Honoured by `parseScene` since T0085**, which is where the filter
    /// belongs — its output is already the explicit list a cull pass (T0045) is
    /// meant to be inserted around. An entity is drawn when this mask intersects
    /// its `MeshRenderer::layers`.
    ///
    /// All layers by default, so a camera nobody has configured sees everything.
    LayerMask cullingMask{LayerMask::all()};

    /// Which composited view this camera feeds (T0081.2).
    ///
    /// A `RenderStack` layer resolves the highest-priority enabled camera
    /// carrying its slot, so a world layer and a HUD layer each get their own
    /// without either knowing about the other's cameras.
    std::uint8_t viewSlot{0};

    /// What this view draws instead of the shaded image (T0141).
    ///
    /// **On the camera rather than on the renderer**, because it is a property
    /// of *a view* and not of the engine: an editor showing a shaded viewport
    /// beside a normals viewport is one scene with two cameras, and a global
    /// switch could not express that. It also costs nothing to plumb —
    /// `ResolvedView` already carries a copy of the lens.
    ///
    /// Serialised, so a debug view survives a save. That is deliberate rather
    /// than an oversight: a viewport left in a debug mode and reopened shaded
    /// would look like the mode had silently failed.
    SurfaceDebugView debugView{SurfaceDebugView::None};
};

/// Converts a photographic focal length to the vertical field of view it
/// produces (130.1).
/// @param focalLengthMm focal length in millimetres. Must be greater than zero.
/// @param sensorHeightMm sensor height in millimetres, usually a camera's
///        `sensorHeightMm`. Must be greater than zero.
/// @returns the vertical field of view in radians, or 0 for a non-positive
///          input rather than a NaN that propagates into a projection matrix and
///          shows up as a blank screen three subsystems away.
[[nodiscard]] HP_API float verticalFovFromFocalLength(float focalLengthMm, float sensorHeightMm);

/// Converts a vertical field of view to the focal length that produces it.
///
/// The exact inverse of `verticalFovFromFocalLength` at the same sensor height.
/// @param verticalFov vertical field of view in radians, in (0, pi).
/// @param sensorHeightMm sensor height in millimetres. Must be greater than zero.
/// @returns the focal length in millimetres, or 0 for an out-of-range input.
[[nodiscard]] HP_API float focalLengthFromVerticalFov(float verticalFov, float sensorHeightMm);

/// Converts a vertical field of view to the horizontal one it implies at a given
/// aspect ratio.
///
/// Useful for culling and for UI that reports what a camera sees; the projection
/// does not need it.
/// @param verticalFov vertical field of view in radians, in (0, pi).
/// @param aspect viewport width divided by height. Must be greater than zero.
/// @returns the horizontal field of view in radians, or 0 for an out-of-range
///          input.
[[nodiscard]] HP_API float horizontalFovFromVertical(float verticalFov, float aspect);

/// Converts EV100 to the linear multiplier a shader applies to radiance.
/// @param ev100 exposure value at ISO 100.
/// @returns the linear multiplier. Always positive.
[[nodiscard]] HP_API float exposureMultiplierFromEv100(float ev100);

/// Builds the projection matrix for a camera.
///
/// **Left-handed, +Z into the screen**, which is Diligent's camera-space
/// convention and not ours to reopen — its projection helpers, its
/// `SetNearFarClipPlanes` and its sample shaders all assume it, and picking the
/// other handedness means fighting every one of them for no gain.
///
/// **Reverse-Z**, so the near plane maps to 1 and the far plane to 0. See
/// `kReverseZ` for what else must agree.
///
/// @param camera the lens. `nearPlane` and `farPlane` must be positive with
///        near strictly less than far; `verticalFov` must be in (0, pi) for a
///        perspective camera and `orthographicSize` positive for an
///        orthographic one.
/// @param aspect viewport width divided by height (130.2). Must be greater than
///        zero. **Derived from the viewport by the caller, never stored on the
///        camera**, because a stored aspect goes stale on the first resize.
/// @param clip the device's clip-space convention, from
///        `RenderLayer::clipSpace()`. Read from the device rather than assumed:
///        it is the difference between a matrix that is right on Vulkan and
///        mirrored on OpenGL.
/// @returns the projection matrix, or the identity when a parameter is out of
///          range — which renders nothing recognisable rather than propagating a
///          NaN through the transform chain, where it would present as
///          disappearing geometry somewhere else entirely.
[[nodiscard]] HP_API float4x4 projectionMatrix(const Camera& camera, float aspect, ClipSpace clip);

} // namespace hp
