// The sample's glass pane (T0147.5) — a game shader that reads the frame.
//
// **This is the first material in this repository that samples something the
// engine rendered rather than something an artist authored.** Everything the
// rock cube does — parallax, self-shadowing, sway — is computed from its own
// textures and its own geometry. This one asks what is *behind* it, which is the
// capability T0147 landed and the one the whole screen-space family was blocked
// on: refraction, heat haze, frosted glass, soft particles, fog-of-war.
//
// It uses both intermediates, on purpose, because they answer different
// questions and a demonstration of one is not a demonstration of the other:
//
//   * `HpSceneColour(uv)` — *what* is behind. Displacing the coordinate is the
//     whole of refraction; this pane displaces it along a travelling wave, so
//     the rock behind it ripples.
//   * `HpSceneViewDepth(uv)` — *how far* behind. The pane fades out where the
//     cube comes close to it, which is the soft-particle read (T0106.5) in the
//     one place a person can see it happening.
//
// ## The one rule, and it is enforced rather than documented
//
// **`glass.hpmat` says `alphaMode: Blend`, and it has to** (D37). The
// intermediates are snapshots taken between the opaque pass and the blend pass,
// so an opaque material reading them would see the *previous* frame — which
// works perfectly from the second frame of a static scene and is therefore
// exactly the bug that ships. Change that line to `Opaque` and this shader does
// not compile into a pipeline at all: the console names this file and the rule,
// and the pane renders the missing-material checkerboard.
//
// ## What is honest about it and what is not
//
// **The refraction is real and the reflection is a stand-in.** A pane of glass
// at a grazing angle reflects rather than transmits, and reflecting *what*
// requires an environment this engine does not have yet (T0087). So the Fresnel
// term below brightens toward a constant instead of sampling a cubemap — the
// right shape with a placeholder for the missing input, and the line to change
// when IBL lands.
//
// **A refractive surface does not see other blended surfaces**, including
// itself. There is one pane here so it does not come up; with two, the nearer
// one would not appear in the further one's refraction. That is the limitation
// every engine ships and D37 records it rather than solving it.

cbuffer HpMaterialParams
{
    /// How far the refraction displaces the screen coordinate, in screen
    /// widths at full wave amplitude. 0.02 is a pane of ordinary glass; 0.1 is
    /// a funhouse.
    [HpRange(0.0, 0.15)]
    [HpTooltip("Screen-space refraction offset, in fractions of the frame.")]
    float refraction;

    /// Ripples per metre of world height. The wave runs vertically because the
    /// pane hangs vertically; a water surface would run it in x and z.
    [HpRange(0.0, 12.0)]
    [HpTooltip("Ripples per metre of world height.")]
    float rippleFrequency;

    /// Radians per second the wave travels at. Zero freezes it, which is what
    /// a still-frame test wants.
    [HpRange(0.0, 8.0)]
    [HpTooltip("How fast the ripple travels, in radians per second.")]
    float rippleSpeed;

    /// How far behind the pane a surface has to be before the glass reads at
    /// full strength, in metres. Below it the pane fades toward the
    /// undistorted image, so the glass softens where it meets geometry instead
    /// of ending in a hard line.
    [HpRange(0.05, 5.0)]
    [HpTooltip("Metres of clearance over which the glass fades in behind it.")]
    float contactRange;

    /// What the glass does to the light passing through it. Slightly cool and
    /// slightly dark, because glass is not a window.
    [HpColor]
    [HpTooltip("Transmission tint.")]
    float3 tint;

    /// How bright the grazing-angle term gets. Standing in for a reflection
    /// until T0087 gives the engine something to reflect.
    [HpRange(0.0, 1.0)]
    [HpTooltip("Strength of the grazing-angle brightening.")]
    float sheen;
}

struct HpMaterial : IHpMaterial
{
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        // The travelling wave, in **world** space rather than in UV space, so
        // the ripple is a property of where the pane is and not of how its
        // mesh happens to be unwrapped. `In.Time` is the frame's clock
        // (T0159.5) and is zero in a caller that passes none — which is what
        // makes a single-frame render of this material reproducible.
        float wave = sin(In.WorldPos.y * rippleFrequency + In.Time * rippleSpeed);

        // **Refraction is one coordinate displacement.** Everything else in
        // this file is decoration.
        float2 uv = In.ScreenUV + float2(wave * refraction, 0.0);
        float3 behind = HpSceneColour(uv).rgb;

        // Grazing angles: the Fresnel shape, with a constant where a
        // reflection belongs. `In.Normal` is the geometric normal before any
        // map, which is what a flat pane wants.
        float facing = saturate(dot(normalize(In.Normal), In.ViewDir));
        float fresnel = pow(1.0 - facing, 5.0);

        float3 glass = behind * tint + fresnel * sheen;

        // **The depth read, and it decides the alpha rather than the colour.**
        // Alpha 0 leaves the frame exactly as the opaque pass left it, so the
        // pane disappears where the cube presses against it and there is no
        // hard intersection line. Premultiplied blending is why the colour is
        // scaled here: `SurfacePipeline` sets `SrcBlend = ONE`, and the
        // engine's own footer multiplies for a material that does not, so this
        // returns straight alpha and lets it.
        float clearance = HpSceneViewDepth(In.ScreenUV) - HpViewDepth(In.ScreenPos.z);
        float contact = saturate(clearance / contactRange);

        return float4(glass, contact);
    }

    /// Glass transmits; it is not a diffuse surface with a light on it. Lighting
    /// it would darken the transmitted image by the scene's illumination, which
    /// is exactly backwards.
    override bool unshaded()
    {
        return true;
    }
}
