// The rock cube's material shader (T0158, T0159) — **game content, not engine
// code**.
//
// A `.slang` module the sample delivers through the VFS, named by
// `rock.hpmat`'s `shader` GUID and compiled into the surface pipeline as
// `HpMaterialModule.generated` (T0142.15, D28). It defines
// `struct HpMaterial : IHpMaterial` and overrides **two** methods; everything
// else — base colour, ORM, the normal map, the lighting — comes from the
// interface defaults, which *are* the standard material.
//
// The engine is unchanged. That is the point: D26 exists so that a surface
// technique is expressible as content, and this is the first thing outside
// `tests/` to take it up on that.
//
// ---
//
// ## 1. The reference plane (158.1) — `surfaceCoordinates`
//
// Parallax occlusion mapping displaces **inward only**. The engine's march
// anchors height 1 at the polygon plane, so every texel sits at or below the
// face: the polygon is the *ceiling* of the relief, and the surface reads as
// something seen through a pane rather than as rock. That is the technique's
// nature, not a defect — no setting puts geometry outside the mesh.
//
// What can change is *which height sits on the plane*. Put the mid grey there
// instead of white and half the range appears raised, so the same height map
// reads as slabs standing proud. It costs **nothing**: same loop, same samples,
// same intersection. Only the ray's starting point moves.
//
// **It is a parameter, not a literal, since T0160** — declared in the
// `HpMaterialParams` block below and valued by `rock.hpmat`, so an artist
// changes how this rock reads without touching a shader, and doing so rebuilds
// no pipeline. It was the first knob in this repository to have nowhere to
// live, and it is T0160's acceptance test for that reason.
//
// **Verified rather than asserted:** at `referencePlane == 1` this reproduces
// the engine's own march exactly — luminance variation 26.7202 either way, the
// number the standard material produces on this scene.
//
// Above the plane the effect is a **lie, and a bigger one the further it goes**:
// a raised texel should have been hit by the view ray *before* it reached the
// surface, through a side that does not exist, so raised regions swim at
// grazing angles where sunken ones stay put. Half-out is convincing; fully-out
// is a different artefact. A real silhouette needs geometry (T0146.7), and
// T0156.6 closed the pixel-shader alternatives.
//
// ## 2. Self-shadowing (158.2, landed by T0159) — `surface`
//
// A second, shorter march from the displaced texel **toward the light**: where
// the height field rises above the ray, the texel is occluded and darkens.
// This is what turns motion parallax into *shading* parallax — crevices go
// dark on the side away from the light — and it is the depth cue T0158
// measured as the missing one.
//
// **This could not be written before T0159, and the history matters.** The
// hooks were non-mutating, so the tangent frame built here in
// `surfaceCoordinates` could not be carried to `surface()`; and by `surface()`
// time `In.UV0` is the *displaced* coordinate, whose screen-space derivatives
// are discontinuous — a frame rebuilt from them gave the shadow ray ~0.006 UV
// of lateral reach against the ~0.14 the geometry needs, so the term was zero
// everywhere while reading as correct in the source. T0159 made the hooks
// `[mutating]` (state now carries across, members start zero-initialised) and
// amended D27 (a light is now reachable: `g_Frame.Lights[]` is in scope for a
// module, deliberately). The march below is the one written on T0158; what it
// lacked was a frame and a light, and now it has both.
//
// **One march, against the dominant light — chosen by intensity, never by
// index.** `Lights[0]` was the first version and it was wrong in a way worth
// recording: two directional lights carry the same (zero) sort key through
// `selectLightsFor`'s *unstable* sort, so which one lands at index 0 is
// unspecified — and on this machine it was the fill, which lies almost exactly
// in the front face's plane, so the march's horizon guard tripped on 67% of
// the cube and the shadow term measured 0.02 luminance. Found by rendering
// the guard paths as colours, the same way T0158 found the collapsed frame.
// Per-light shadowing proper is a *lighting-stage* decision (which lights
// deserve a march, whether the fill goes unshadowed) and belongs to
// T0145/158.3, where the loop that knows the lights lives; a sample picking
// its one light by brightest intensity is honest game content.
//
// `struct HpMaterial : IHpMaterial` overrides the coordinate hook and the
// whole-output hook, and keeps state between them — the first material to do
// either.

// **The shader's own parameter, declared here and valued in `rock.hpmat`**
// (T0160). This block used to be a `static const float kReferencePlane = 0.5;`
// inside the struct below, with a comment explaining that it was a literal
// "because that vocabulary is the engine's and this knob is this shader's" —
// which was true of the engine's *material* vocabulary and stopped being the
// end of the story when a module gained one of its own.
//
// **Outside the `#if` below, and that is a rule rather than a tidiness
// choice.** Everything else in this file lives inside
// `#if HP_USE_HEIGHT_MAP && USE_TEXCOORD0`, so it exists in some permutations
// and not others. A declaration that came and went with the macro set would
// make "what does this shader expose" a question with several answers, and
// neither the `.hpmat` nor an inspector row can be permutation-dependent.
// Unused permutations strip the block from their bytecode and pay nothing.
cbuffer HpMaterialParams
{
    /// Where the height field sits relative to the polygon.
    ///
    /// 1 is the engine's behaviour — white on the surface, everything beneath.
    /// 0.5 puts half the range above, so the same height map reads as slabs
    /// standing proud rather than as relief hanging under a pane. It costs
    /// nothing: same loop, same samples, same intersection — only the ray's
    /// starting point moves.
    [HpRange(0.0, 1.0)]
    [HpTooltip("Which height sits on the polygon plane. 1 is fully sunken, 0.5 puts half the relief above the face.")]
    float referencePlane;

    /// How far the top of the cube leans, in metres (T0146.3).
    ///
    /// **Zero is off, and off is exactly the pre-T0146 geometry** — the hook
    /// still runs, adds zero and the transform chain is untouched. Changing
    /// this rebuilds no pipeline, which is the T0160 property being shown
    /// again one stage up.
    [HpRange(0.0, 0.5)]
    [HpTooltip("How far the top of the cube leans, in metres. 0 turns the sway off.")]
    float swayAmplitude;

    /// Radians per second of the lean's oscillation. 1.4 is a ~4.5 second
    /// period, slow enough to read as weight rather than as a wobble.
    [HpRange(0.0, 8.0)]
    [HpTooltip("Radians per second of the sway. 1.4 is roughly a 4.5-second period.")]
    float swayRate;
}

struct HpMaterial : IHpMaterial
{
    /// **The vertex hook** (T0146.3, D36) — and it is here to make one
    /// contrast concrete.
    ///
    /// Everything else in this file is parallax: it displaces where a texel is
    /// *sampled from*, so the rock reads as carved, and the outline of the
    /// cube never moves by so much as a pixel. That is POM's honest limitation
    /// — `HpParallaxUv`'s own comment says so, T0156.6 evaluated the
    /// pixel-shader alternatives and recommended against them, and the
    /// `.hpmat` beside this file records why its `heightScale` had to come
    /// down because of it.
    ///
    /// This moves the geometry. Same object, same frame: relief from the pixel
    /// stage, silhouette from the vertex stage.
    ///
    /// ## Why a lean rather than a push along the normals
    ///
    /// The obvious "breathing" demo displaces each vertex along its normal,
    /// and **it splits this mesh open**. The cube is flat-shaded: every corner
    /// carries three vertices with three different face normals, so pushing
    /// along them sends the three copies to three different places and the
    /// edges gape. That is a property of the mesh, not of the hook, and it is
    /// the first thing anyone writing a vertex shader against hard-edged
    /// geometry hits — so this samples the other technique instead, the one
    /// that keeps the surface watertight: a **lean**, driven by height, which
    /// is wind sway on a trunk with the trunk removed.
    ///
    /// `lean` is quadratic in height because a linear ramp reads as a shear —
    /// the whole body sliding — rather than as something bending where it is
    /// thinnest.
    ///
    /// ## What a field of grass would change
    ///
    /// One term: `+ In.WorldPos.x * phasePerMetre` inside the `sin`, so
    /// neighbouring instances of the same mesh do not sway in lockstep.
    /// `In.WorldPos` is handed over for exactly that, and it is why D36
    /// supplies the world position as an *input* rather than as Godot's
    /// `world_vertex_coords` mode. It is left out **here** on purpose: this
    /// scene has one cube, and a phase that varied with position would put the
    /// sample's rest pose somewhere other than its authored transform at
    /// `Time == 0` — which is what keeps every committed frame in
    /// `rockcube_sample_test.cpp` byte-identical to its pre-T0146 baseline.
    [mutating] override void vertex(HpVertexInput In, inout HpVertexOutput Out)
    {
        // The cube is 2 m on a side, centred on its origin, so object-space Y
        // runs -1 to 1: this is 0 at the base and 1 at the top.
        float height = saturate(In.Position.y * 0.5 + 0.5);
        float lean = height * height;
        // Object space, so the lean travels with the cube's own yaw as it
        // spins rather than being pinned to a world axis. That is the
        // difference D36's decision buys and the reason the triplanar note in
        // `rock.hpmat` exists one stage down.
        Out.Position.x = In.Position.x + swayAmplitude * lean * sin(In.Time * swayRate);
    }

#if HP_USE_HEIGHT_MAP && USE_TEXCOORD0
    /// Steps in the shadow march. Fewer than the view march, by design: a
    /// shadow ray answers *blocked or not*, not *where*.
    static const int kShadowSteps = 8;

    /// How hard the shadow bites. 1 lets a full occluder reach black; 0
    /// disables the march's effect entirely (the gpu test's off-switch).
    static const float kShadowStrength = 1.0;

    /// Sharpens the soft occlusion term. The raw term is a height difference,
    /// a few hundredths for typical relief; this scales it into a visible
    /// penumbra. Tuned by eye against the sample's height map.
    static const float kShadowHardness = 24.0;

    /// Caps the lateral reach per unit of climb. `lightTS.xy / lightTS.z`
    /// diverges as 1/z toward the horizon — watched live on the spinning cube:
    /// the shadows grew hyperbolically and swallowed the face. The cap bounds
    /// the march's window instead; shadows shorten near the horizon rather
    /// than exploding, which is the lie POM always tells at grazing angles,
    /// applied consistently.
    static const float kMaxReachRatio = 8.0;

    /// Where the horizon fade begins, in `lightTS.z`. Below it the term eases
    /// to zero, so a face rotating past the light's horizon dims smoothly
    /// instead of snapping from fully shadowed to unshadowed in one frame —
    /// the terminator pop 158.5 warned about, also watched live.
    static const float kHorizonFade = 0.1;

    /// The bias floor, in height units. The march biases the ray half a step
    /// above the surface, but near the *top* of the height range the steps
    /// shrink with the remaining rise and the bias shrinks with them — and the
    /// surface starts occluding itself, which renders as isolated black
    /// pixels sprinkled over the highest texels. Watched live on the top
    /// face; this floor is what removes it.
    static const float kShadowBias = 0.015;

    /// Below this remaining rise the march is skipped outright: the ray exits
    /// the height range within the bias, so every sample would be a
    /// self-occlusion candidate and none a real one.
    static const float kMinRise = 0.02;

    // --- Per-fragment state, written by `surfaceCoordinates` and read by
    // --- `surface` (T0159.2). Starts zero-initialised: the engine constructs
    // --- the material with `= {}`, so an unmarched fragment reads zeros and
    // --- the `marched` flag below is false rather than garbage.

    /// The tangent frame, cached because `surface()` cannot rebuild it: by
    /// then the UV is displaced and its derivatives are discontinuous.
    float3 frameT;
    float3 frameB;
    float3 frameN;

    /// The **pre-march** UV gradients, for `SampleGrad` in the shadow march —
    /// required for the same reason the view march needs them.
    float2 gradX;
    float2 gradY;

    /// Whether the view march ran. False at grazing angles (the same horizon
    /// guard the view march uses), and the shadow march skips with it — the
    /// stable answer where both rays degenerate, so terminators do not
    /// shimmer (158.5).
    bool marched;

    /// Samples the height field with explicit gradients.
    ///
    /// **`SampleGrad`, not `Sample`, and it is required rather than a
    /// refinement**: a marched UV diverges between neighbouring pixels, so
    /// implicit derivatives inside a loop are undefined, and the artefact is a
    /// seam of wrong mips exactly where the effect is strongest.
    float heightAt(float2 uv, float2 duv1, float2 duv2)
    {
        return g_HeightMap.SampleGrad(g_HeightMap_sampler, float3(uv, 0.0), duv1, duv2).r;
    }

    /// The tangent frame, from screen-space derivatives.
    ///
    /// The same construction the engine's `HpParallaxUv` uses, for the same
    /// reason: it needs no vertex tangents, so this works on any mesh that has
    /// texture coordinates at all. `N` is oriented toward the viewer explicitly
    /// rather than by `SV_IsFrontFace`, because in this engine hardware facing
    /// and geometric facing disagree and the flipped contract normal can point
    /// away from the camera on a perfectly front-facing surface.
    void tangentFrame(HpSurfaceInput In, float2 uv, out float3 T, out float3 B, out float3 N,
                      out float2 duv1, out float2 duv2)
    {
        float3 dp1 = ddx(In.WorldPos);
        float3 dp2 = ddy(In.WorldPos);
        duv1 = ddx(uv);
        duv2 = ddy(uv);

        N = normalize(In.Normal);
        if (dot(N, In.ViewDir) < 0.0)
        {
            N = -N;
        }

        float3 dp2perp = cross(dp2, N);
        float3 dp1perp = cross(N, dp1);
        T = dp2perp * duv1.x + dp1perp * duv2.x;
        B = dp2perp * duv1.y + dp1perp * duv2.y;
        float invMax = rsqrt(max(max(dot(T, T), dot(B, B)), 1e-12));
        T *= invMax;
        B *= invMax;
    }

    [mutating] override VSOutput surfaceCoordinates(VSOutput VSOut, HpSurfaceInput In)
    {
        float2 uv = VSOut.UV0;

        float3 T, B, N;
        float2 duv1, duv2;
        tangentFrame(In, uv, T, B, N, duv1, duv2);

        // Cached for `surface()` either way -- the members cost nothing and
        // the shadow march guards on `marched`, not on the frame.
        frameT = T;
        frameB = B;
        frameN = N;
        gradX = duv1;
        gradY = duv2;

        float3 viewTS = float3(dot(T, In.ViewDir), dot(B, In.ViewDir), dot(N, In.ViewDir));
        if (viewTS.z <= 1e-4)
        {
            // Grazing or degenerate: the offset diverges as 1/z, and past the
            // horizon there is no meaningful march. Flat is the stable answer,
            // and `marched` stays false so the shadow march skips too.
            return VSOut;
        }

        // More layers at grazing angles, where each texel of offset crosses
        // more relief; fewer head-on, where the offset is tiny anyway.
        const float numLayers = lerp(32.0, 8.0, saturate(viewTS.z));
        const float layerDepth = 1.0 / numLayers;
        // `heightScale` still comes from the `.hpmat`, through the channel
        // DiligentFX reserves per material: the shader owns the reference
        // plane, the asset owns the depth.
        const float2 fullOffset = viewTS.xy / viewTS.z * g_Material.Basic.CustomData.x;
        const float2 deltaUv = fullOffset / numLayers;

        // **The reference plane, in one subtraction.** Depth is measured from
        // it rather than from white, so heights above it are *negative* — in
        // front of the polygon. To meet them the ray must start there,
        // `(1 - reference)` of the range out along the view vector, and then
        // step inward exactly as before. At `referencePlane == 1` both
        // adjustments are identically zero.
        const float above = 1.0 - referencePlane;

        float currentDepth = -above;
        float2 currentUv = uv + fullOffset * above;
        float depthAtUv = referencePlane - heightAt(currentUv, duv1, duv2);
        for (int i = 0; i < 32; ++i)
        {
            if (currentDepth >= depthAtUv)
            {
                break;
            }
            currentUv -= deltaUv;
            currentDepth += layerDepth;
            depthAtUv = referencePlane - heightAt(currentUv, duv1, duv2);
        }

        // Occlusion refinement: intersect the ray with the height field between
        // the last two samples rather than snapping to a layer, which is what
        // separates parallax *occlusion* mapping from stair-stepped steep
        // parallax.
        float2 previousUv = currentUv + deltaUv;
        float depthAfter = depthAtUv - currentDepth;
        float depthBefore = (referencePlane - heightAt(previousUv, duv1, duv2)) -
                            (currentDepth - layerDepth);
        float weight = saturate(depthAfter / max(depthAfter - depthBefore, 1e-6));

        VSOut.UV0 = lerp(currentUv, previousUv, weight);
        marched = true;
        return VSOut;
    }

    /// The shadow march (158.2): from the displaced texel toward the light,
    /// darkening where the height field occludes it. Tatarchuk's approximate
    /// soft shadows: every sample above the ray contributes, weighted toward
    /// blockers near the surface, and the maximum wins — a penumbra rather
    /// than a hard edge, from the same eight samples.
    [mutating] override void surface(HpSurfaceInput In, inout HpSurfaceOutput Out)
    {
#if defined(PBR_MAX_LIGHTS) && PBR_MAX_LIGHTS > 0
        if (!marched || g_Frame.Renderer.LightCount < 1)
        {
            return;
        }

        // The dominant light only, picked by intensity — see the header for
        // why index 0 is not it, and why one light is this sample's call to
        // make rather than the engine's. `IntensityRGB` arrives premultiplied
        // by the light's colour, so its luminance is the ranking.
        int lightCount = min(g_Frame.Renderer.LightCount, PBR_MAX_LIGHTS);
        int dominant = 0;
        float bestLuminance = -1.0;
        for (int i = 0; i < lightCount; ++i)
        {
            float luminance = dot(float3(g_Frame.Lights[i].IntensityR,
                                         g_Frame.Lights[i].IntensityG,
                                         g_Frame.Lights[i].IntensityB),
                                  float3(0.2126, 0.7152, 0.0722));
            if (luminance > bestLuminance)
            {
                bestLuminance = luminance;
                dominant = i;
            }
        }
        // `Direction` is the light's direction of travel, so toward the light
        // is its negation.
        PBRLightAttribs light = g_Frame.Lights[dominant];
        float3 toLight = -normalize(float3(light.DirectionX, light.DirectionY,
                                           light.DirectionZ));

        float3 lightTS = float3(dot(frameT, toLight), dot(frameB, toLight),
                                dot(frameN, toLight));
        if (lightTS.z <= 1e-4)
        {
            // The light is at or below the surface's horizon. N·L shading
            // already darkens this case; a marched term would only shimmer
            // (158.5's guard, same threshold as the view ray's).
            return;
        }

        // Where the ray starts: on the height field at the displaced texel.
        // `In.UV0` *is* that texel — the engine refreshes the contract's UV
        // from the coordinate hook's return — and the cached gradients keep
        // the sample's mip stable. One extra sample, clearer than caching the
        // march's interpolated depth.
        float hitDepth = referencePlane - heightAt(In.UV0, gradX, gradY);

        // The ray climbs from the hit depth to the top of the height range
        // (depth `-(1 - reference)`), in the light's tangent-space direction.
        const float above = 1.0 - referencePlane;
        float totalRise = hitDepth + above;
        if (totalRise <= kMinRise)
        {
            return; // at (or within the bias of) the top; nothing real occludes
        }

        // The reach ratio, clamped — see `kMaxReachRatio`. The direction in
        // UV space is preserved; only how far the ray travels per unit of
        // climb is bounded.
        float2 reach = lightTS.xy / lightTS.z;
        float reachLength = length(reach);
        if (reachLength > kMaxReachRatio)
        {
            reach *= kMaxReachRatio / reachLength;
        }

        const float stepDepth = totalRise / float(kShadowSteps);
        const float2 stepUv = reach * g_Material.Basic.CustomData.x * stepDepth;

        // Bias before the first sample, or the surface occludes itself and
        // the field speckles: half a step, floored by `kShadowBias` for the
        // texels near the top whose steps have shrunk to nothing.
        const float bias = max(stepDepth * 0.5, kShadowBias);
        float rayDepth = hitDepth - bias;
        float2 rayUv = In.UV0 + stepUv * 0.5;
        float shadow = 0.0;
        for (int i = 0; i < kShadowSteps; ++i)
        {
            float fieldDepth = referencePlane - heightAt(rayUv, gradX, gradY);
            if (fieldDepth < rayDepth)
            {
                // The field rises above the ray: occluded. Blockers close to
                // the surface get full weight, distant ones fade — the soft
                // falloff that separates a penumbra from a stencil.
                float falloff = 1.0 - float(i) / float(kShadowSteps);
                shadow = max(shadow, (rayDepth - fieldDepth) * falloff);
            }
            rayDepth -= stepDepth;
            rayUv += stepUv;
        }

        // **The bite is capped by the marched light's share of the incident
        // light.** Multiplying base colour darkens *every* light's
        // contribution, but only the dominant light was marched — at full
        // strength that blacked out faces the fill still reaches, watched
        // live as the cube rotated. The share is estimated with the same
        // N·L·intensity products the lighting will compute, so a face the
        // key barely grazes keeps the fill's portion of its light. The
        // per-light answer this approximates is T0145/158.3's.
        float dominantIncident = 0.0;
        float totalIncident = 0.0;
        for (int j = 0; j < lightCount; ++j)
        {
            float3 towardJ = -normalize(float3(g_Frame.Lights[j].DirectionX,
                                               g_Frame.Lights[j].DirectionY,
                                               g_Frame.Lights[j].DirectionZ));
            float incident = dot(float3(g_Frame.Lights[j].IntensityR,
                                        g_Frame.Lights[j].IntensityG,
                                        g_Frame.Lights[j].IntensityB),
                                 float3(0.2126, 0.7152, 0.0722)) *
                             saturate(dot(Out.Normal, towardJ));
            totalIncident += incident;
            if (j == dominant)
            {
                dominantIncident = incident;
            }
        }
        float share = totalIncident > 1e-6 ? dominantIncident / totalIncident : 0.0;

        // The horizon fade — smooth toward `lightTS.z == 0`, so the
        // terminator moves rather than pops (158.5).
        float fade = smoothstep(0.0, kHorizonFade, lightTS.z);

        // Applied to base colour: with no ambient or IBL in this engine yet
        // (T0087), diffuse is the only large term, and `Occlusion` would not
        // reach the punctual lights at all. Dielectric specular (F0 0.04)
        // stays — visible as a faint sheen inside shadowed crevices, which is
        // physically defensible and cheap.
        float term = 1.0 -
                     saturate(shadow * kShadowHardness) * kShadowStrength * share * fade;
        Out.BaseColor.rgb *= term;
#endif
    }
#endif
}
