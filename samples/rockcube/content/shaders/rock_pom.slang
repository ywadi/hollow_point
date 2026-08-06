// The rock cube's material shader (T0158) — **game content, not engine code**.
//
// A `.slang` module the sample delivers through the VFS, named by
// `rock.hpmat`'s `shader` GUID and compiled into the surface pipeline as
// `HpMaterialModule.generated` (T0142.15, D28). It defines
// `struct HpMaterial : IHpMaterial` and overrides **one** method; everything
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
// **Verified rather than asserted:** at `kReferencePlane == 1` this reproduces
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
// ## 2. Self-shadowing — attempted, and it does not fit here
//
// See T0158's findings. `IHpMaterial`'s methods are non-mutating, so a
// material cannot carry the tangent frame from `surfaceCoordinates` to
// `surface`; and by the time `surface` runs, `In.UV0` is the *displaced*
// coordinate, whose screen-space derivatives are discontinuous, so the frame
// cannot be rebuilt there either. Measured: a frame rebuilt in `surface`
// gives the shadow ray ~0.006 UV of lateral reach -- about three texels -- so
// it never meets an occluder and the term is zero everywhere while looking
// entirely plausible in the source.
//
// That is a gap in the contract rather than a defeat, and it is the argument
// for self-shadowing living in the engine's surface stage, where both the
// smooth UV and the light are in scope.
//
// `struct HpMaterial : IHpMaterial` overrides one method: the coordinate hook.

struct HpMaterial : IHpMaterial
{
#if HP_USE_HEIGHT_MAP && USE_TEXCOORD0
    /// Where the height field sits relative to the polygon.
    ///
    /// 1 is the engine's behaviour — white on the surface, everything beneath.
    /// 0.5 puts half the range above. A literal rather than a `.hpmat`
    /// parameter because that vocabulary is the engine's and this knob is this
    /// shader's; a game shader carrying its own constants is the ordinary case.
    static const float kReferencePlane = 0.5;

    /// Steps in the shadow march. Fewer than the view march, by design.
    static const int kShadowSteps = 8;

    /// How hard the shadow bites. 1 lets a full occluder reach black.
    static const float kShadowStrength = 1.0;

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

    override VSOutput surfaceCoordinates(VSOutput VSOut, HpSurfaceInput In)
    {
        float2 uv = VSOut.UV0;

        float3 T, B, N;
        float2 duv1, duv2;
        tangentFrame(In, uv, T, B, N, duv1, duv2);

        float3 viewTS = float3(dot(T, In.ViewDir), dot(B, In.ViewDir), dot(N, In.ViewDir));
        if (viewTS.z <= 1e-4)
        {
            // Grazing or degenerate: the offset diverges as 1/z, and past the
            // horizon there is no meaningful march. Flat is the stable answer.
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
        // step inward exactly as before. At `kReferencePlane == 1` both
        // adjustments are identically zero.
        const float above = 1.0 - kReferencePlane;

        float currentDepth = -above;
        float2 currentUv = uv + fullOffset * above;
        float depthAtUv = kReferencePlane - heightAt(currentUv, duv1, duv2);
        for (int i = 0; i < 32; ++i)
        {
            if (currentDepth >= depthAtUv)
            {
                break;
            }
            currentUv -= deltaUv;
            currentDepth += layerDepth;
            depthAtUv = kReferencePlane - heightAt(currentUv, duv1, duv2);
        }

        // Occlusion refinement: intersect the ray with the height field between
        // the last two samples rather than snapping to a layer, which is what
        // separates parallax *occlusion* mapping from stair-stepped steep
        // parallax.
        float2 previousUv = currentUv + deltaUv;
        float depthAfter = depthAtUv - currentDepth;
        float depthBefore = (kReferencePlane - heightAt(previousUv, duv1, duv2)) -
                            (currentDepth - layerDepth);
        float weight = saturate(depthAfter / max(depthAfter - depthBefore, 1e-6));

        VSOut.UV0 = lerp(currentUv, previousUv, weight);
        return VSOut;
    }
#endif
}
