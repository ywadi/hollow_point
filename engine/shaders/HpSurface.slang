// hp-shader-doc: private
//
// The engine's own **vertex and pixel** shaders (T0141/T0146, D26; a `.slang`
// file since T0142, D28).
//
// **This file is why D26 exists.** DiligentFX's `RenderPBR.psh` is private and
// has no hook before texture sampling, so parallax, height mapping and triplanar
// are unreachable through it at any setting. Rather than patch a vendored
// submodule, the engine owns this half and calls DiligentFX's *public* shading
// library for the lighting -- surface properties in, radiance out.
//
// ## Two stages, one file, one compile (T0146.5)
//
// The vertex `main` used to be `RenderPBR.vsh`, theirs, compiled by a second
// request. It is `vsMain` below now, and the split is the same one the pixel
// half already made: the orchestration is ours, the transform itself is their
// **public** `GLTF_TransformVertex` from `VertexProcessing.fxh`. What that buys
// is `IHpMaterial.vertex()` -- wind, sway, billboarding, breathing, morph
// offsets -- which no setting on their shader could ever have reached.
//
// Both entry points go onto **one** slang compile request (`compileSlangProgram`),
// so a pipeline parses and preprocesses this file once rather than twice, and
// each entry point still yields its own SPIR-V module carrying only what that
// stage reaches.
//
// ## One source, one compiler
//
// This file is compiled by **slang** to SPIR-V (D28), and by nothing else.
// It used to be constrained to the HLSL-compatible subset because the OpenGL
// backend compiled the same bytes through Diligent's HLSL path -- slang's
// HLSL and GLSL outputs rename every resource and GL binds by name -- so no
// `interface`, no generics and no `override` could appear here. D29 removed
// that backend (T0144), and `IHpMaterial` below (T0142.2) is what the
// constraint was blocking: the contract as a Slang interface whose default
// implementations *are* the standard material.
//
// The split, and it is the same one Godot and Unreal draw:
//
//   surface stage (ours)  ->  read textures, displace UVs, blend layers
//   lighting     (theirs) ->  BRDF, punctual lights, IBL, shadows
//
// **The subset is D24's, not a simplification.** DiligentFX's own material
// shader is 656 lines because it covers clearcoat, sheen, anisotropy,
// iridescence, transmission and volume. D24 keeps every one of those off, so
// reimplementing them here would be carrying code for features the engine
// deliberately does not enable. When one is turned on, it is turned on here.
//
// ---
//
// **Considered and rejected: shadowing `PBR_Textures.fxh`.** The engine's source
// factory resolves our files before DiligentFX's, so shipping a file with that
// name would make *their* `RenderPBR.psh` include *ours*, and the parallax hook
// could go there with no PSO work and no shader of our own. It was rejected
// because it is the worse coupling, not the better one: we would be
// reimplementing a **private** header against a caller we do not control, and a
// semantic change upstream would land as a subtly wrong image rather than a
// compile error. Owning our own `main` couples us to their *public* library
// instead, which is the thing they have promised to keep stable.

// `HLSLDefinitions.fxh` is deliberately NOT included here, although slang
// needs it first: **every compiler that consumes this file prepends it
// itself**. Diligent's do so natively; the engine's slang path mirrors them
// (`SlangCompiler.cpp`). Including it here instead was tried and broke the GL
// backend: the HLSL2GLSL converter inlines `#include` directives textually,
// before any preprocessing, so the include guard never ran and every function
// in the file was defined twice.
#include "BasicStructures.fxh"
#include "PBR_Structures.fxh"
// **Their transform, called from our vertex main** (T0146.1) -- the vertex
// half's equivalent of the `PBR_Shading.fxh` line below, and public like it.
// `GLTF_TransformVertex` is the node-matrix multiply plus the normal's
// inverse-transpose; reimplementing it would be reimplementing a determinant.
#include "VertexProcessing.fxh"
// **Their lighting, called from our main** — this include is D26 in one line.
#include "PBR_Shading.fxh"
// **A private header, and taking it is deliberate rather than an oversight.**
// `PBRFrameAttribs` and `PBRPrimitiveAttribs` are the *layouts of the constant
// buffers `PBR_Renderer` fills*, and this engine already uses those buffers --
// `SceneRenderer.cpp` includes this very file on the C++ side to write them. So
// this is the coupling we already have, not a new one, and D26 accepted it
// explicitly: keeping their struct layouts is what keeps their lighting
// functions callable with no translation.
//
// The distinction that matters is layout versus behaviour. A layout change
// breaks the C++ that writes the buffer *and* the shader that reads it, in the
// same build, loudly. Depending on a private *function* would be the thing to
// avoid, and this shader does not.
#include "RenderPBR_Structures.fxh"
#include "HpMaterial.slang"

// The per-pipeline interface structs, generated by `PBR_Renderer` and supplied
// through the source factory `SurfacePipeline` builds. Included **by name**, the
// same way `RenderPBR.psh` does -- which is what keeps our two stages agreeing
// about `VSOutput` without either half restating it, and what makes a
// vertex-layout change a compile error here rather than garbage in the
// interpolators.
//
// `VSInputStruct.generated` arrives with `[[vk::location(n)]]` on every field
// (`SurfacePipeline::addVkInputLocations`): slang numbers vertex inputs
// sequentially while the input layout numbers them by semantic index, and
// without the annotation the tangent stream feeds the wrong location silently.
#include "VSInputStruct.generated"
#include "VSOutputStruct.generated"
#include "PSOutputStruct.generated"

cbuffer cbFrameAttribs
{
    PBRFrameAttribs g_Frame;
}

// **Not the array form** (`PRIMITIVE_ARRAY_SIZE > 0`), which `RenderPBR.vsh`
// carries for their multi-draw path and this engine has never configured. The
// guard is here rather than absent because turning `PrimitiveArraySize` on
// without touching this file would read primitive 0's transform for every draw
// -- geometry piled at one place, no error anywhere.
#if PRIMITIVE_ARRAY_SIZE > 0
#   error "HpSurface.slang has no PRIMITIVE_ARRAY_SIZE path; see SurfacePipeline::configure"
#endif

cbuffer cbPrimitiveAttribs
{
    PBRPrimitiveAttribs g_Primitive;
}

cbuffer cbMaterialAttribs
{
    PBRMaterialShaderInfo g_Material;
}

// ---------------------------------------------------------------------------
// The vertex stage's own inputs (T0146.1), mirrored from `RenderPBR.vsh`.
//
// **Mirrored rather than included**, because the file they live in is their
// vertex `main` and taking it would be taking the thing this ticket replaces.
// Everything below is a decode of a *packed* vertex attribute or the joint
// palette; the transform itself is still theirs (`GLTF_TransformVertex`).
// ---------------------------------------------------------------------------

#ifndef MAX_JOINT_COUNT
#   define MAX_JOINT_COUNT 64
#endif

#if MAX_JOINT_COUNT > 0 && USE_JOINTS
// **Compiles, cannot render** (T0146, and stated plainly rather than claimed
// otherwise). `SceneRenderer` never sets `PSO_FLAG_USE_JOINTS` -- the vertex
// flags come from the mesh's attributes and joints are not among the cases it
// maps -- so no permutation the engine builds reaches this block today. It is
// here so that T0041 inherits the palette lookup against *our* vertex shader
// rather than having to relocate it afterwards, which is the sequencing
// argument on T0146's own ticket.
#if JOINTS_BUFFER_MODE == JOINTS_BUFFER_MODE_UNIFORM

struct HpSkinningData
{
#   if COMPUTE_MOTION_VECTORS
        float4x4 Joints[MAX_JOINT_COUNT * 2];
#   else
        float4x4 Joints[MAX_JOINT_COUNT];
#   endif
};

cbuffer cbJointTransforms
{
    HpSkinningData g_Skin;
}

float4x4 HpGetJointMatrix(int JointIndex, int FirstJoint)
{
    return g_Skin.Joints[JointIndex];
}

#elif JOINTS_BUFFER_MODE == JOINTS_BUFFER_MODE_STRUCTURED

StructuredBuffer<float4x4> g_JointTransforms;

float4x4 HpGetJointMatrix(int JointIndex, int FirstJoint)
{
    return g_JointTransforms[JointIndex + FirstJoint];
}

#endif
#endif // MAX_JOINT_COUNT > 0 && USE_JOINTS

float4 HpGetVertexColor(float3 Color)
{
    return float4(Color, 1.0);
}

float4 HpGetVertexColor(float4 Color)
{
    return Color;
}

#if PACK_VERTEX_NORMALS
/// Reverse of `PBR_Renderer::PackVertexNormal()`. Off in this engine
/// (`PackVertexNormals` is left at its default), mirrored so a future decision
/// to pack does not have to reopen this file.
float3 HpGetVertexNormal(in uint PackedNormal)
{
    float3 Normal = float3(0.0, 0.0, 0.0);
    if (PackedNormal != 0xFFFFFFFFu)
    {
        Normal.x = float( PackedNormal         & 0xFFFFu) / 32767.0 - 1.0;
        Normal.y = float((PackedNormal >> 16u) & 0x7FFFu) / 16383.0 - 1.0;
        Normal.z = sqrt(max(1.0 - dot(Normal.xy, Normal.xy), 0.0));
        Normal.z *= (PackedNormal & 0x80000000u) != 0u ? -1.0 : 1.0;
    }
    return Normal;
}
#else
float3 HpGetVertexNormal(in float3 Normal)
{
    return Normal;
}
#endif

#if VERTEX_POS_PACK_MODE == VERTEX_POS_PACK_MODE_64_BIT
/// Reverse of `PBR_Renderer::PackVertexPos64()`. Off in this engine; see
/// `HpGetVertexNormal`.
float3 HpGetVertexPosition(VSInput VSIn)
{
    uint2 PackedPos = VSIn.Pos;

    float U21Scale = 1.0 / 2097151.0; // 2^21 - 1
    uint  U21Mask  = 2097151u;

    float3 Pos;
    Pos.x = float(PackedPos.x & U21Mask) * U21Scale;
    Pos.y = float((PackedPos.x >> 21u) | ((PackedPos.y & 1023u) << 11u)) * U21Scale;
    Pos.z = float(PackedPos.y >> 10u) * U21Scale;

    GLTFNodeShaderTransforms PrimTransforms = g_Primitive.Transforms;
    float3 PosScale = float3(PrimTransforms.PosScaleX, PrimTransforms.PosScaleY,
                             PrimTransforms.PosScaleZ);
    float3 PosBias  = float3(PrimTransforms.PosBiasX,  PrimTransforms.PosBiasY,
                             PrimTransforms.PosBiasZ);
    return Pos * PosScale + PosBias;
}
#else
float3 HpGetVertexPosition(VSInput VSIn)
{
    return VSIn.Pos;
}
#endif

// **Their texture sampling, included rather than rewritten (D26, revised).**
//
// `PBR_Textures.fxh` declares every material texture and provides a getter per
// slot -- `GetBaseColor`, `GetPhysicalDesc`, `GetMicroNormal`, and eleven more
// covering clearcoat, sheen, anisotropy, iridescence, transmission and volume.
// That is 934 lines of atlas handling, UV-set selection, wrap modes, mip bias
// and colour conversion, already correct on every backend.
//
// **The surface stage does not require owning the sampling**, which is the
// correction that matters here. It requires owning `main` -- the ability to
// change the UV *before* the sample and the surface properties *after* it. Every
// getter below takes `VSOutput` **by value**, so parallax (141.7) and triplanar
// (141.8) displace a local copy's texture coordinates and call theirs with it.
// The hook D26 exists for survives intact.
//
// This header is private, and that is a real cost accepted deliberately: a
// signature change upstream breaks this build loudly, which is the failure mode
// worth having. The alternative was reimplementing it -- which is what this file
// did until the owner asked why, and the honest answer was that including it had
// never been evaluated.
#include "PBR_Textures.fxh"

// Thin-film interference (T0143). `RenderPBR.psh` includes it under the same
// guard; `EvalIridescence` is what turns a thickness in nanometres into the
// Fresnel the base layer's F0 is bent toward.
#if ENABLE_IRIDESCENCE
#include "Iridescence.fxh"
#endif

// ---------------------------------------------------------------------------
// The engine's screen intermediates (T0147).
//
// **Snapshots, not the live attachments**, and everything about how they may
// be used follows from that one word. `SceneRenderer` copies the colour and
// depth targets between the opaque pass and the blend pass; a material samples
// the copies, never the surface it is writing into. `HpMaterial.slang`'s
// "Screen resources" section is the authoring documentation and carries the
// validity rules; what is here is the declaration and the four helpers.
//
// Declared unconditionally rather than behind a permutation bit, because there
// is nothing to gate: slang strips a resource nothing samples, so a standard
// material's bytecode names neither and the base signature's slots cost it
// nothing at draw time -- the same superset pattern `g_HeightMap` and
// DiligentFX's seventeen already use.
//
// **No samplers of their own** (T0147, D35): sample through the palette,
// `HpSamplerLinearClamp` for colour and `HpSamplerPointClamp` for depth.
// Clamped rather than wrapped is load-bearing -- a screen-space read that
// leaves the frame must not fetch the opposite edge, which is the artefact a
// wrap sampler puts along every border of a refraction.
// ---------------------------------------------------------------------------

/// The scene's colour as of the end of the opaque pass. **Linear**, and
/// pre-tonemap when T0096's HDR chain lands. See `HpSceneColour`.
Texture2D g_SceneColour;

/// The scene's depth as of the end of the opaque pass, raw device depth under
/// the engine's reverse-Z convention (near = 1, far = 0). See `HpSceneDepth`.
Texture2D g_SceneDepth;

/// The **render target's** size in pixels in `xy` and its reciprocal in `zw`.
///
/// The target's, not the viewport's: `SV_POSITION` is in target pixels and the
/// snapshots are target-sized, so this is the divisor that turns one into the
/// other. They differ under a letterboxing aspect policy (T0081), which is
/// exactly when using `f4ViewportSize` here would sample the wrong texel.
///
/// `CameraAttribs::f4ExtraData` is Diligent's own "any application-specific
/// data" slot; `SceneRenderer` writes element 0 every frame, and the camera
/// block is value-initialised first, so this is zero rather than undefined in
/// a caller that supplies no target.
#define HP_TARGET_SIZE (g_Frame.Camera.f4ExtraData[0])

// hp-shader-doc: export
/// Samples the scene-colour snapshot.
///
/// @param screenUv normalised target coordinates -- `HpSurfaceInput::ScreenUV`,
///        or that plus a distortion offset, which is the whole of refraction.
/// @returns linear RGB in `rgb`, and whatever the target's alpha holds in `a`.
float4 HpSceneColour(float2 screenUv)
{
    return g_SceneColour.SampleLevel(HpSamplerLinearClamp, screenUv, 0.0);
}

// hp-shader-doc: export
/// Samples the scene-depth snapshot, **raw**.
///
/// @param screenUv normalised target coordinates.
/// @returns device depth: 1 at the near plane and 0 at the far one (reverse-Z,
///          T0130). A pixel nothing was drawn into reads the clear value, 0.
///          Comparing two of these is valid; subtracting them is not linear in
///          distance, which is what `HpSceneViewDepth` is for.
float HpSceneDepth(float2 screenUv)
{
    return g_SceneDepth.SampleLevel(HpSamplerPointClamp, screenUv, 0.0).r;
}

// hp-shader-doc: export
/// Turns a device depth into a distance along the view axis, in metres.
///
/// Built from `mProjInv` rather than from the near and far planes, so it is
/// correct whichever way round the depth range runs and needs no constant of
/// its own. **A far-plane pixel returns the far plane's distance**, which for
/// a cleared background is the largest number in the scene -- fade against it
/// deliberately, or mask it with `HpSceneDepth(uv) > 0`.
///
/// **Positive, growing with distance, on both sides of T0165's handedness
/// change.** The engine's camera space is right-handed -- the camera looks
/// down its own −Z (`hp::kRightHandedCameraSpace`) -- so unprojecting gives a
/// *negative* view-space Z for everything in front of the lens, and the
/// distance is its negation. This is the one place a shader hardcodes the
/// camera's handedness; the C++ constant is the authority and
/// `screen_inputs_test` is what pins the sign, because a silently negated
/// depth does not look like a depth bug: a `saturate` clamps it to zero and a
/// contact fade simply stops fading.
///
/// @param deviceDepth a depth as `HpSceneDepth` or `In.ScreenPos.z` gives it.
/// @returns view-space distance in world units. Positive in front of the
///          camera.
float HpViewDepth(float deviceDepth)
{
    float4 unprojected = mul(float4(0.0, 0.0, deviceDepth, 1.0), g_Frame.Camera.mProjInv);
    return -(unprojected.z / unprojected.w);
}

// hp-shader-doc: export
/// The scene's linear depth at a screen coordinate, in metres.
///
/// @param screenUv normalised target coordinates.
/// @returns view-space distance to whatever the opaque pass left there.
float HpSceneViewDepth(float2 screenUv)
{
    return HpViewDepth(HpSceneDepth(screenUv));
}

#if HP_USE_HEIGHT_MAP
// The engine's own height slot (T0141.7), added to the signature by
// `SurfacePipeline::CreateCustomSignature` -- not one of DiligentFX's
// seventeen, so their getters never see it and T0143 can enable every one of
// theirs without colliding. Compiled only into permutations whose material
// carries a height map; the signature declares it for all of them, which is
// the same superset pattern the seventeen already use.
Texture2DArray g_HeightMap;
SamplerState   g_HeightMap_sampler;

/// 141.7's steep march with occlusion interpolation, frame-agnostic (T0156).
///
/// Takes the view vector already rotated into the projection's tangent frame
/// -- `xy` along the UV axes, `z` along the surface normal, all toward the
/// viewer -- plus the **pre-march** UV gradients for `SampleGrad`. The frame
/// construction stays with the callers because it is the only part that
/// differs: UV-mapped parallax builds one from screen derivatives
/// (`HpParallaxUv`), while triplanar's three frames are world axes, so its
/// swizzles mirror the projection swizzles and cost nothing.
float2 HpParallaxMarch(float2 uv, float3 viewTS, float2 duv1, float2 duv2)
{
    if (viewTS.z <= 1e-4)
    {
        // Grazing or degenerate: the offset diverges as 1/z, and past the
        // horizon there is no meaningful march. Flat is the stable answer.
        return uv;
    }

    // More layers at grazing angles, where each texel of offset crosses more
    // relief; fewer head-on, where the offset is tiny anyway.
    const float numLayers = lerp(32.0, 8.0, saturate(viewTS.z));
    const float layerDepth = 1.0 / numLayers;
    // Full-scale offset. White (height 1) is the surface, black is
    // `heightScale` UV units deep -- the convention the material documents.
    const float2 fullOffset = viewTS.xy / viewTS.z * g_Material.Basic.CustomData.x;
    const float2 deltaUv = fullOffset / numLayers;

    // **`SampleGrad` with the pre-march gradients, and it is required, not a
    // refinement.** The march's UVs diverge between neighbouring pixels, so
    // implicit derivatives inside the loop are undefined; the artefact is a
    // seam of wrong mips exactly where the effect is strongest.
    float currentDepth = 0.0;
    float2 currentUv = uv;
    float depthAtUv = 1.0 - g_HeightMap.SampleGrad(g_HeightMap_sampler,
                                                   float3(currentUv, 0.0), duv1, duv2).r;
    for (int i = 0; i < 32; ++i)
    {
        if (currentDepth >= depthAtUv)
        {
            break;
        }
        currentUv -= deltaUv;
        currentDepth += layerDepth;
        depthAtUv = 1.0 - g_HeightMap.SampleGrad(g_HeightMap_sampler,
                                                 float3(currentUv, 0.0), duv1, duv2).r;
    }

    // Occlusion refinement: intersect the ray with the height field between
    // the last two samples instead of snapping to a layer, which is what
    // separates parallax *occlusion* mapping from stair-stepped steep
    // parallax.
    float2 previousUv = currentUv + deltaUv;
    float depthAfter = depthAtUv - currentDepth;
    float depthBefore = (1.0 - g_HeightMap.SampleGrad(g_HeightMap_sampler,
                                                      float3(previousUv, 0.0), duv1, duv2).r) -
                        (currentDepth - layerDepth);
    float weight = saturate(depthAfter / max(depthAfter - depthBefore, 1e-6));
    return lerp(currentUv, previousUv, weight);
}

#if USE_TEXCOORD0
/// Parallax occlusion (T0141.7): the owner's "screen displacement", and the
/// technique D26 exists to make expressible. Marches the view ray through the
/// height field and returns the UV where it lands; every texture then samples
/// the surface the eye actually hits, so relief looks carved rather than
/// painted. Silhouettes stay flat -- that is POM's honest limitation, recorded
/// on the ticket, and changing an outline is tessellation (T0146.7).
float2 HpParallaxUv(float2 uv, HpSurfaceInput In)
{
    // The tangent frame from screen-space derivatives -- the same trick
    // DiligentFX's `PerturbNormal` uses for normal mapping, and for the same
    // reason: it needs no vertex tangents, so parallax works on any mesh that
    // has texture coordinates at all.
    float3 dp1 = ddx(In.WorldPos);
    float3 dp2 = ddy(In.WorldPos);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);

    // **Oriented toward the viewer explicitly, not by `SV_IsFrontFace`.**
    // Parallax only makes sense on the side being looked at, and the contract
    // normal legitimately points away from the camera when a double-sided
    // surface is viewed from behind, so the normal is forced toward the
    // viewer here rather than trusting a facing bit this helper is not
    // handed. (An earlier version of this comment cited T0141.12's inverted
    // winding diagnosis; T0152 corrected that -- hardware facing equals glTF
    // facing -- and this orientation is right for its own reasons.)
    float3 N = normalize(In.Normal);
    if (dot(N, In.ViewDir) < 0.0)
    {
        N = -N;
    }

    float3 dp2perp = cross(dp2, N);
    float3 dp1perp = cross(N, dp1);
    float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    // **The chirality correction, and it is not optional** (T0165).
    //
    // The construction above is the *adjugate* of the UV Jacobian, not its
    // inverse -- it omits the `1 / det(dUV)` factor, which the normalisation
    // below then discards the magnitude of. What survives is a **sign**, and
    // the identity is exact: `T_above == dot(cross(dp1, dp2), N) * T_exact`,
    // where `T_exact` is the solution of `dp = T du + B dv`. So the frame is
    // correct only where `cross(ddx(P), ddy(P))` and `N` agree.
    //
    // They agreed under the old left-handed camera and they do not now. The
    // screen basis is `(ddx, ddy) = (+X_cam, -Y_cam)`, so `cross(ddx, ddy)` is
    // `-Z_cam` whichever way the camera looks -- but `N` here is forced toward
    // the viewer, and *toward the viewer* moved from `-Z_cam` to `+Z_cam` with
    // the handedness. Without this line T and B both come out anti-parallel to
    // +u and +v, which reads as relief carved inside-out and a normal map with
    // x and y negated: wrong, plausible, and invisible to a coverage test.
    //
    // Mirrored UV islands are **not** what this handles -- the adjugate gets a
    // negative `det(dUV)` right on its own, checked both ways.
    float chirality = dot(cross(dp1, dp2), N) >= 0.0 ? 1.0 : -1.0;
    T *= chirality;
    B *= chirality;

    float invMax = rsqrt(max(max(dot(T, T), dot(B, B)), 1e-12));
    T *= invMax;
    B *= invMax;

    float3 viewTS = float3(dot(T, In.ViewDir), dot(B, In.ViewDir), dot(N, In.ViewDir));
    return HpParallaxMarch(uv, viewTS, duv1, duv2);
}
#endif
#endif

#if HP_TRIPLANAR
// ---------------------------------------------------------------------------
// Triplanar projection (T0141.8): textures from world space, no UVs required.
//
// **This is the one place the surface stage samples material textures
// itself**, and it is D26's per-function override argued rather than assumed:
// DiligentFX's getters are UV-set-bound -- `SampleTexture` reads
// `SelectUV(VSOut, ...)` behind `#if USE_TEXCOORD0 || USE_TEXCOORD1` -- so a
// technique whose whole point is needing no texture coordinates cannot reach
// through them. The factor, sRGB and vertex-colour composition below mirrors
// their getters line for line, so a triplanar material and a UV-mapped one
// disagree only about *where* a texel comes from.
// ---------------------------------------------------------------------------

/// The three projections and their blend weights, computed once per fragment.
struct HpTriplanar
{
    float2 uvX;
    float2 uvY;
    float2 uvZ;
    float3 weights;
    // The **pre-march** UV gradients per projection (T0156). Sampling uses
    // these explicitly rather than the implicit derivative, for two reasons
    // that are really one: the marched coordinates under parallax break the
    // implicit derivative -- the same reason the march itself uses
    // `SampleGrad` (141.7), applied per axis -- and skipping a zero-weight
    // projection's tap is only legal when no implicit derivative is taken
    // inside the branch. The no-parallax path uses the same gradients so
    // the two permutations sample identically, which is what makes
    // "zero height scale equals no height map" hold to the bit.
    float2 duvXdx;
    float2 duvXdy;
    float2 duvYdx;
    float2 duvYdy;
    float2 duvZdx;
    float2 duvZdy;
};

/// Builds the basis. Callers go through `HpTriplanarBasis`, which memoises
/// this under parallax -- the marches below are the expensive part and every
/// channel getter asks for the same answer.
HpTriplanar HpTriplanarComputeBasis(HpSurfaceInput In)
{
    HpTriplanar t;
    // Sharpened blend: linear weights leave wide smeared bands at 45 degrees.
    // The exponent is fixed rather than authorable until something needs it.
    float3 n = normalize(In.Normal);
    float3 w = pow(abs(n), float3(4.0, 4.0, 4.0));
    t.weights = w / max(w.x + w.y + w.z, 1e-5);

    // **Weights below 1% are dropped and the rest renormalised** (156.2).
    // `pow(abs(n), 4)` drives the off-axis weights to ~0 on most pixels --
    // flat ground is one live axis, a slope is two -- and both the parallax
    // march and the texture taps skip a dead axis outright, so the floor is
    // where the cost saving comes from. The visible step when an axis
    // crosses it is bounded by the floor itself, one part in a hundred of
    // the blend; the 156.4 characterisation looked for the resulting
    // contour on a terrain whose slope sweeps through the threshold and
    // found none visible.
    const float kMinWeight = 0.01;
    t.weights.x = t.weights.x < kMinWeight ? 0.0 : t.weights.x;
    t.weights.y = t.weights.y < kMinWeight ? 0.0 : t.weights.y;
    t.weights.z = t.weights.z < kMinWeight ? 0.0 : t.weights.z;
    t.weights /= max(t.weights.x + t.weights.y + t.weights.z, 1e-5);

    // Tiles per metre (`Material::triplanarScale`, in `CustomData.y`).
    float scale = g_Material.Basic.CustomData.y;
    t.uvX = In.WorldPos.zy * scale;
    t.uvY = In.WorldPos.xz * scale;
    t.uvZ = In.WorldPos.xy * scale;

    // Gradients: the projections are linear in world position, so each is
    // exactly the world-position derivative, swizzled and scaled the same
    // way the UVs are.
    float3 dpx = ddx(In.WorldPos) * scale;
    float3 dpy = ddy(In.WorldPos) * scale;
    t.duvXdx = dpx.zy;
    t.duvXdy = dpy.zy;
    t.duvYdx = dpx.xz;
    t.duvYdy = dpy.xz;
    t.duvZdx = dpx.xy;
    t.duvZdy = dpy.xy;

#if HP_USE_HEIGHT_MAP
    // -----------------------------------------------------------------------
    // Parallax under triplanar (T0156): 141.7's march, run per projection.
    //
    // **The expensive half of 141.7 does not apply here.** The projections
    // are world-axis-aligned, so each tangent frame is a *constant* -- for
    // the Z projection, tangent = world X, bitangent = world Y, normal =
    // world Z -- and rotating the view vector into it is the same swizzle
    // the UVs use above. No derivative frame, no orthogonalisation.
    //
    // `abs()` on the normal component, matching the `abs(n)` the weights
    // use: a projection serves both facings of its plane with the same UVs,
    // so the march's "toward the viewer" normal is the axis signed toward
    // the camera, and that sign only reaches `viewTS.z`.
    //
    // Each live projection marches independently, in its own frame. Where
    // two axes blend, their displaced answers disagree -- characterised on
    // T0156.4 before this was accepted: the band is narrow under the
    // 4th-power weights and the disagreement reads as softened relief, not
    // a seam.
    // -----------------------------------------------------------------------
    float3 V = In.ViewDir;
    if (t.weights.x > 0.0)
    {
        t.uvX = HpParallaxMarch(t.uvX, float3(V.zy, abs(V.x)), t.duvXdx, t.duvXdy);
    }
    if (t.weights.y > 0.0)
    {
        t.uvY = HpParallaxMarch(t.uvY, float3(V.xz, abs(V.y)), t.duvYdx, t.duvYdy);
    }
    if (t.weights.z > 0.0)
    {
        t.uvZ = HpParallaxMarch(t.uvZ, float3(V.xy, abs(V.z)), t.duvZdx, t.duvZdy);
    }
#endif
    return t;
}

#if HP_USE_HEIGHT_MAP
// Per-invocation memo (T0156): `static` at global scope is thread-local in
// Slang as in HLSL, initialised per fragment. Five channel getters ask for
// the basis and the marches make it expensive, so the first caller computes
// and the rest read. Without a height map the basis is a handful of ALU and
// the memo would cost more than it saves.
static bool HpTriplanarBasisReady = false;
static HpTriplanar HpTriplanarBasisMemo;
#endif

HpTriplanar HpTriplanarBasis(HpSurfaceInput In)
{
#if HP_USE_HEIGHT_MAP
    if (!HpTriplanarBasisReady)
    {
        HpTriplanarBasisMemo = HpTriplanarComputeBasis(In);
        HpTriplanarBasisReady = true;
    }
    return HpTriplanarBasisMemo;
#else
    return HpTriplanarComputeBasis(In);
#endif
}

float4 HpTriplanarSample(Texture2DArray tex, SamplerState smp, HpTriplanar t, float mipBias)
{
    // Slice 0: every texture this engine binds is a one-slice array, and the
    // per-slot `TextureSlice` attrib exists for atlases the triplanar path
    // does not serve.
    //
    // `SampleGrad` with the basis's explicit gradients, in every
    // permutation -- see the struct comment. It has no bias argument;
    // scaling both gradients by `exp2(mipBias)` shifts the computed LOD by
    // exactly the bias, so the frame-wide mip bias still applies. This was
    // `SampleBias` until T0156, and the change was measured before being
    // made: swapping the instruction alone moved the frame by under 0.1
    // mean-absolute per channel (mip rounding at tile boundaries) -- and it
    // is what lets zero-height-scale frames match no-height-map frames to
    // the bit, because both permutations then issue identical instructions
    // on identical inputs.
    //
    // A zero-weight projection's tap is skipped outright -- legal precisely
    // because the derivatives are explicit -- which is where 156.2's
    // early-out saves its cost: one live axis pays one march and one tap
    // per texture, not three.
    float gradScale = exp2(mipBias);
    float4 result = float4(0.0, 0.0, 0.0, 0.0);
    if (t.weights.x > 0.0)
    {
        result += tex.SampleGrad(smp, float3(t.uvX, 0.0), t.duvXdx * gradScale,
                                 t.duvXdy * gradScale) *
                  t.weights.x;
    }
    if (t.weights.y > 0.0)
    {
        result += tex.SampleGrad(smp, float3(t.uvY, 0.0), t.duvYdx * gradScale,
                                 t.duvYdy * gradScale) *
                  t.weights.y;
    }
    if (t.weights.z > 0.0)
    {
        result += tex.SampleGrad(smp, float3(t.uvZ, 0.0), t.duvZdx * gradScale,
                                 t.duvZdy * gradScale) *
                  t.weights.z;
    }
    return result;
}
#endif

/// The standard material's surface function -- **the engine's own, and private
/// to this file** (T0142.13).
///
/// D27 described this as the thing a game writes. It is not, and has not been
/// since T0142.15 put a game's module inside this translation unit: a second
/// body here is `error[E30201]: function 'HpSurface' already has a body`,
/// measured on the pinned compiler. D27's function-style hook survives as
/// `IHpMaterial.surface()`, whose default calls this -- so a material that
/// wants the whole-output hook overrides that method, and this stays what it
/// always actually was, the standard material's own body.
///
/// **The engine's own material is written against the same contract a game's
/// is** (D27), which is the check that keeps the contract honest: if
/// `HpSurfaceInput` is missing something, the standard material notices first.
void HpSurface(in HpSurfaceInput In, inout HpSurfaceOutput Out)
{
    // Nothing yet -- everything is pre-filled from the material, so leaving it
    // alone is exactly "the standard PBR material" (141.10).
}

// ---------------------------------------------------------------------------
// The engine's lighting stage (T0145, D30/D31) -- **the loop is ours, the BRDF
// is still theirs.**
//
// This is D26 one level deeper, and it exists for a reason that was *measured*
// rather than assumed. DiligentFX's `ApplyPunctualLight`
// (`PBR_Shading.fxh:601-721`) computes range attenuation, the spot cone, the
// shadow-map lookup, and then calls `SmithGGX_BRDF` **inline** at :690. Their
// coarse seams are genuinely thin -- `GetBaseLayerLighting` is one line,
// `ResolveLighting` four terms -- but there is no point *inside* the per-light
// body where a different shading model could be substituted. The clearcoat
// layer goes through `ApplyDirectionalLightGGX` (:718); the base layer does not.
//
// So the loop body is mirrored here, ~120 lines, and `SmithGGX_BRDF` stays the
// default. That buys both of D30's remaining rungs at once: rung 3 is the
// per-light method below, rung 4 is the whole stage, and neither exists in
// Godot at any price short of a fork (its `light_post()` proposal is open since
// 2019).
//
// **The mirror is a maintained copy, and the maintenance is mechanical.**
// `tests/fast/upstream_drift_test.cpp` holds a pinned copy of every upstream
// function reproduced here and fails with a line diff when a submodule bump
// moves one. The owner accepted that cost explicitly (D30, amended) with the
// steer that the guard be built to *show what moved* rather than merely trip.
//
// **What is not mirrored is refused loudly rather than silently dropped**, the
// same shape as the `PRIMITIVE_ARRAY_SIZE` guard at the top of this file. The
// extended material features accumulate into sheen and clearcoat layers this
// engine has never enabled (D24), and writing those paths blind -- against
// LUTs nothing binds and a `Shading.Clearcoat` nothing fills -- is exactly the
// untestable carrying D24 refused. T0143 turns them on, and this is where it
// writes them.
// ---------------------------------------------------------------------------

// The extended layers (T0143, amending D24) live where T0145's `#error` said
// they would: anisotropy and transmission inside `HpStandardLight` (they
// reshape the *base* response), sheen and clearcoat as layer accumulators in
// the default `lighting()` loop with their resolve in `HpResolveLighting` --
// each block a mirror of `ApplyPunctualLight`'s / `ResolveLighting`'s, pinned
// by the same drift guard as the base layer. Iridescence needs no lighting
// block at all: it bends `Reflectance0` at surface-fill time, exactly as
// upstream bends the base layer's F0. `ENABLE_VOLUME` deliberately has no
// term here -- thickness's only consumer is the image-based refraction, so it
// waits inside the `USE_IBL` seam below (T0087).

#if USE_IBL
#   error "the engine's lighting stage has no image-based path; T0087 fills the seam in HpResolveLighting"
#endif

#if ENABLE_SHADOWS
// **The shape T0086 drops into, and it is empty on purpose.** The shadow lookup
// lives *inside* the loop body being mirrored (`FilterShadowMapFixedPCF`,
// `PBR_Shading.fxh:655`), which is the whole reason D30 sequenced this ticket
// before T0086: building cascades against *their* loop would relocate the
// sampling a second time. The flag is off in `kFeatureMask`, so no permutation
// the engine builds reaches this.
//
// Two things T0086 must do here rather than anywhere else: multiply the shadow
// into `HpLight::Attenuation`'s *sibling*, never into `Attenuation` itself
// (folding them is precisely Godot's ceiling -- see `HpMaterial.slang`), and
// add `ShadowFactor` to `HpLight` at the same time, because a field that
// arrives before its system is the D27 mistake this contract is written to
// avoid.
#   error "T0086 owns the shadow block inside HpGetLight; enabling ENABLE_SHADOWS before it exists would silently drop the lookup"
#endif

// hp-shader-doc: export
/// How many lights reach this fragment (T0145).
///
/// Already clamped to the permutation's `PBR_MAX_LIGHTS`, so a loop over
/// `0 .. HpLightCount()-1` can never read past the array the frame buffer
/// actually reserved.
int HpLightCount()
{
#if defined(PBR_MAX_LIGHTS) && PBR_MAX_LIGHTS > 0
    return min(g_Frame.Renderer.LightCount, PBR_MAX_LIGHTS);
#else
    return 0;
#endif
}

// hp-shader-doc: export
/// One light in the engine's own vocabulary, resolved against a world position
/// (T0145, D31).
///
/// **The mirrored half of `ApplyPunctualLight`** -- range attenuation, the spot
/// cone, and (with T0086) the shadow lookup. Everything a game shader sees of a
/// light comes through here, so `PBRLightAttribs`' packed layout stops at this
/// function: `DirectionX/Y/Z` becomes a `float3`, `Range4` becomes a range in
/// metres, and the spot cone's scale/offset pair becomes two cosines.
///
/// The unpacking above the attenuation is **dead code when a game does not read
/// those fields**, which is the whole bet D31 records as unmeasured: the same
/// values are loaded either way and the repack is register arithmetic slang and
/// the driver can eliminate. T0145.8 measured it and the bet held -- see the
/// ticket.
///
/// @param index the light, `0 .. HpLightCount()-1`. **0 is the dominant light**
///        (`selectLightsFor`, and the contract in `HpMaterial.slang`).
/// @param worldPos the fragment's world position, for range and cone.
/// @returns the light as it arrives at that position.
HpLight HpGetLight(int index, float3 worldPos)
{
    HpLight L;

#if defined(PBR_MAX_LIGHTS) && PBR_MAX_LIGHTS > 0
    PBRLightAttribs Light = g_Frame.Lights[index];

    L.Type = Light.Type;
    L.Index = index;
    L.Position = float3(Light.PosX, Light.PosY, Light.PosZ);
    L.Color = float3(Light.IntensityR, Light.IntensityG, Light.IntensityB);

    // ---- mirrored from `ApplyPunctualLight`, :613-645 ---------------------
    // Reproduced expression for expression, including the order of the two
    // multiplies at the end: `RangeAttenuation * AngularAttenuation` and then
    // `Color * Attenuation` are what makes the standard material's output
    // bit-identical to the pre-T0145 build, and floating-point multiplication
    // is not associative.
    float3 LightDirection = float3(Light.DirectionX, Light.DirectionY, Light.DirectionZ);

    float Attenuation = 1.0;
    if (Light.Type != PBR_LIGHT_TYPE_DIRECTIONAL)
    {
        float3 LightToPoint = worldPos - L.Position;
        float  Distance2    = dot(LightToPoint, LightToPoint);
        LightToPoint /= sqrt(Distance2);
        float RangeAttenuation = 1.0 / Distance2;
        if (Light.Range4 > 0.0)
        {
            // Attenuation = clamp(1 - (Distance/Range)^4, 0, 1) / Distance^2
            RangeAttenuation *= saturate(1.0 - (Distance2 * Distance2) / Light.Range4);
        }

        if (Light.Type == PBR_LIGHT_TYPE_POINT)
        {
            LightDirection = LightToPoint;
        }

        float AngularAttenuation = 1.0;
        if (Light.Type == PBR_LIGHT_TYPE_SPOT)
        {
            float CosAngle = dot(LightToPoint, LightDirection);
            AngularAttenuation = saturate(CosAngle * Light.SpotAngleScale + Light.SpotAngleOffset);
        }

        Attenuation = RangeAttenuation * AngularAttenuation;
    }
    // ---- end of the mirror ------------------------------------------------

    L.Direction = LightDirection;
    // The BRDF's `PointToLight`, which upstream spells `-LightDirection` at
    // every call site. Negation is exact, so this costs nothing and removes
    // the single most common sign error in a lighting hook.
    L.ToLight = -LightDirection;
    L.Attenuation = Attenuation;
    L.Radiance = L.Color * Attenuation;

    // **The unpacking a game reads and the engine does not.** `Range4` is a
    // fourth power because that is how the attenuation curve uses it; nobody
    // authoring a shader thinks in metres-to-the-fourth. Zero stays zero, which
    // is the "unlimited" the C++ side means by it.
    L.Range = sqrt(sqrt(Light.Range4));

    // `SpotAngleScale` is `1 / max(0.001, cos(inner) - cos(outer))`, so it is
    // never zero and this division is always safe (`GLTF_PBR_Renderer.cpp`).
    // Reported as 1 for a light with no cone, rather than as whatever the
    // packing left behind -- a spot cone on a directional light is meaningless
    // and a shader reading it should see the identity, not noise.
    float outerCos = -Light.SpotAngleOffset / Light.SpotAngleScale;
    L.SpotOuterCos = Light.Type == PBR_LIGHT_TYPE_SPOT ? outerCos : 1.0;
    L.SpotInnerCos = Light.Type == PBR_LIGHT_TYPE_SPOT
                         ? (1.0 / Light.SpotAngleScale + outerCos)
                         : 1.0;
#else
    // No lights in this permutation. `HpLightCount()` is 0 so nothing calls
    // this, but the contract's shape must not depend on a macro -- a method
    // that exists in some permutations and not others is exactly the hazard
    // D31 mirrors the structs to avoid.
    L.Type = HpLightDirectional;
    L.Index = index;
    L.Position = float3(0.0, 0.0, 0.0);
    L.Direction = float3(0.0, 0.0, -1.0);
    L.ToLight = float3(0.0, 0.0, 1.0);
    L.Color = float3(0.0, 0.0, 0.0);
    L.Attenuation = 0.0;
    L.Radiance = float3(0.0, 0.0, 0.0);
    L.Range = 0.0;
    L.SpotInnerCos = 1.0;
    L.SpotOuterCos = 1.0;
#endif

    return L;
}

// hp-shader-doc: export
/// The engine's standard response to one light -- **the default a rung-3
/// override starts from** (T0145).
///
/// The mirrored second half of `ApplyPunctualLight` (:665-693): DiligentFX's
/// `SmithGGX_BRDF`, called with the surface's own reflectance. Lambertian
/// diffuse, Smith-GGX specular, Schlick Fresnel -- the same functions the
/// engine has always used, now reached through a seam instead of past one.
///
/// **Returns the factors unmultiplied**, because that is what makes an
/// override a line rather than a rewrite: the caller computes
/// `(Diffuse + Specular) * Intensity * NdotL`, and a cel shader changes only
/// `NdotL`.
///
/// @param Light the light, from `HpGetLight`.
/// @param Surface the shaded surface.
/// @returns the standard physically-based response.
HpLightResponse HpStandardLight(HpLight Light, HpShadedSurface Surface)
{
    // The mirror's one conversion in the other direction: the engine's
    // flattened surface back into the four primitive fields their BRDF takes.
    // Field copies, so the values reaching `SmithGGX_BRDF` are the same bits
    // `GetSurfaceReflectanceMR` produced.
    SurfaceReflectanceInfo Srf;
    Srf.PerceptualRoughness = Surface.Roughness;
    Srf.Reflectance0 = Surface.Reflectance0;
    Srf.Reflectance90 = Surface.Reflectance90;
    Srf.DiffuseColor = Surface.DiffuseColor;

    float3 diffuse;
    float3 specular;
    float NdotL;
#if ENABLE_ANISOTROPY
    // ---- mirrored from `ApplyPunctualLight`, :674-687 (T0143) -------------
    // The anisotropic GGX replaces the isotropic one for the *base* lobe;
    // the frame and the two alphas were derived at surface-fill time, from
    // whatever the surface stage finally said.
    SmithGGX_BRDF_Anisotropic(Light.ToLight, Surface.Normal, Surface.View,
                              Surface.AnisotropyTangent, Surface.AnisotropyBitangent, Srf,
                              Surface.AlphaRoughnessT, Surface.AlphaRoughnessB, diffuse,
                              specular, NdotL);
#else
    SmithGGX_BRDF(Light.ToLight, Surface.Normal, Surface.View, Srf, diffuse, specular, NdotL);
#endif

#if ENABLE_TRANSMISSION
    // ---- mirrored from :694-698 -------------------------------------------
    // A transmissive surface's diffuse is what passes *through* rather than
    // scattering back; the punctual path removes it here and the blend pass
    // composites the scene behind at `alpha = 1 - Transmission`.
    diffuse *= 1.0 - Surface.Transmission;
#endif

    HpLightResponse R;
    R.Diffuse = diffuse;
    R.Specular = specular;
    R.Intensity = Light.Radiance;
    R.NdotL = NdotL;
    return R;
}

// hp-shader-doc: export
/// Turns accumulated light into the fragment's colour (T0145, layers T0143).
///
/// The mirror of `ResolveLighting` (`PBR_Shading.fxh:847-876`), layer for
/// layer: base punctual plus emissive, sheen added on top, and the clearcoat
/// applied **over everything** -- the coat's Fresnel dims base, emissive and
/// sheen alike before its own lobe is added, which is what a lacquer layer
/// physically does. **The seam T0087 fills is here**: each layer's
/// image-based term joins its punctual one, scaled by the frame's `IBLScale`
/// and the surface's `Occlusion` -- which is why `HpShadedSurface` carries
/// `Occlusion` although no punctual term reads it, and why `Thickness` has no
/// consumer until then.
///
/// @param Surface the shaded surface.
/// @param punctual the sum of every light's base-layer contribution.
/// @param sheenPunctual the sheen layer's sum. Ignored unless the permutation
///        carries `ENABLE_SHEEN`.
/// @param clearcoatPunctual the coat layer's sum, **before** the coat factor
///        (mirroring `SurfaceLightingInfo`). Ignored unless the permutation
///        carries `ENABLE_CLEAR_COAT`.
/// @returns linear RGB, pre-tonemap.
float3 HpResolveLighting(HpShadedSurface Surface, float3 punctual, float3 sheenPunctual,
                         float3 clearcoatPunctual)
{
    float3 Color = punctual + Surface.Emissive;

#if ENABLE_SHEEN
    // `GetSheenLighting` (:826): the punctual sum plus an IBL term that is
    // identically zero without an environment.
    Color += sheenPunctual;
#endif

#if ENABLE_CLEAR_COAT
    {
        // ---- mirrored from `ResolveLighting`, :858-872 --------------------
        float ccNdotV = dot(Surface.ClearcoatNormal, Surface.View);

        // Do not allow the clear coat normal to face away from the view
        // direction -- upstream's own guard against the artefacts of
        // DiligentFX issue #27.
        ccNdotV = max(ccNdotV, 0.1);

        float ClearcoatFresnel = SchlickReflection(ccNdotV, Surface.ClearcoatReflectance0, 1.0);
        // `GetClearcoatLighting` (:840) is the punctual sum times the coat
        // factor; the (1 - F*factor) dim is what the coat takes *from*
        // everything underneath.
        Color = Color * (1.0 - Surface.Clearcoat * ClearcoatFresnel) +
                clearcoatPunctual * Surface.Clearcoat;
    }
#endif

    return Color;
}

// hp-shader-doc: export
/// The base-layer-only resolve -- T0145's original two-argument contract,
/// kept callable so a rung-4 override written before T0143 compiles and
/// behaves exactly as it did.
///
/// A material whose permutation carries sheen or clearcoat and whose
/// `lighting()` override calls *this* form gets no layer resolve -- the same
/// "you own the whole stage now" trade every rung-4 override makes. The
/// default `lighting()` calls the layered form above.
///
/// @param Surface the shaded surface; only `Emissive` is read today.
/// @param punctual the sum of every light's contribution.
/// @returns linear RGB, pre-tonemap.
float3 HpResolveLighting(HpShadedSurface Surface, float3 punctual)
{
    // `GetBaseLayerIBL` is `(DiffuseIBL + SpecularIBL) * IBLScale * Occlusion`
    // and both terms are identically zero while `USE_IBL` is off -- so the
    // whole product is +0 and adding it is a no-op the compiler removes. It is
    // written out rather than implied because T0087's change is then one line
    // in one place, and because the `#if USE_IBL` guard above makes turning IBL
    // on without writing it impossible rather than merely unlikely.
    return punctual + Surface.Emissive;
}

// ---------------------------------------------------------------------------
// `IHpMaterial` (T0142.2, D28) -- the contract, in a language that can
// express it.
//
// **The default implementations ARE the standard material.** That is the
// entire design: a material conforms to this interface and overrides only
// what it wants, `override` is mandatory on anything that replaces a default
// (omitting it is a compile error, so engine behaviour is never shadowed by
// accident), and generics specialise statically so none of this costs a
// dynamic dispatch. D27's promise carries over unchanged: **adding a method
// with a default is free; removing one breaks every shipped game.** So does
// `HpMaterial.slang`'s rule that nothing is exposed before the system behind it
// exists.
//
// The methods take the raw `VSOutput` alongside `HpSurfaceInput` because
// DiligentFX's getters consume it -- and because that is the parallax and
// triplanar hook (141.7/141.8): an override builds a UV-displaced copy and
// hands it to the same getters. `HpSurfaceInput` remains the stable,
// documented vocabulary; `VSOutput` is the per-permutation wire format, and a
// material that touches it accepts that its fields come and go with the mesh.
//
// **Every hook is `[mutating]`, so a material can keep per-fragment state
// across them** (T0159.2). A tangent frame built in `surfaceCoordinates` can
// be cached in a member and read back in `surface()` -- which is what parallax
// self-shadowing needs, and what T0158 measured could not be rebuilt after the
// march moved the UV. Three facts, verified on the pinned `slangc 2026.14.1`
// rather than assumed:
//
//   * a **non-mutating** `override` still satisfies a `[mutating]`
//     requirement, so every existing module compiles unchanged;
//   * a default implementation on a `[mutating]` requirement is legal, so the
//     standard material pays nothing;
//   * state written through one hook is visible in the next -- the emitted
//     code threads one `inout this` through the specialised calls.
//
// Member state starts **zero-initialised**, not undefined: `main` constructs
// the material with `= {}`, which also keeps slang's uninitialized-variable
// diagnostic out of every stateful module's compile log.
//
// **A game delivers a conformance as content** (T0142.15): a `.slang` module
// reaching the compiler through the VFS (T0142.14), named by
// `Material::shader`, included below as `HpMaterialModule.generated` and
// defining `struct HpMaterial : IHpMaterial`. The engine's own
// `HpStandardMaterial` is the empty conformance beside it, and doubles as the
// reference implementation (142.3).
//
// *(Corrected 2026-08-06 by T0141.14. This paragraph said the opposite —
// "not yet a game-deliverable authoring surface ... until that lands" — which
// stopped being true when 142.15 landed and 142.13 rewrote the contract file
// around it. It is corrected here rather than only in the ticket because
// this text is the generated shader reference's prose: `docs/shaders/` would
// have published it to every agent writing a material.)*
// ---------------------------------------------------------------------------
// hp-shader-doc: export
interface IHpMaterial
{
    /// **The vertex hook** (T0146, D36) — Godot's `vertex()`, and the one
    /// method here that runs in a different shader stage from the rest.
    ///
    /// Object space in, object space out; `Out` arrives pre-filled from the
    /// mesh, so writing nothing is exactly today's geometry and writing
    /// `Out.Position` is a displacement. `HpMaterial.slang` documents the
    /// space decision, what the hook must not do to a triangle's winding
    /// (D33), and the four `Custom` interpolators that carry a value from here
    /// to the pixel stage.
    ///
    /// **Members do not survive the stage boundary.** The vertex and pixel
    /// stages construct separate `HpMaterial` values because they are separate
    /// invocations — this is the one place T0159's cross-hook state does *not*
    /// apply, and `Out.Custom0` … `Custom3` are the channel that does.
    ///
    /// `[mutating]` for symmetry with every other hook: a vertex hook may keep
    /// state across nothing today, and requiring the attribute now means the
    /// method never has to change signature to gain it.
    [mutating] void vertex(HpVertexInput In, inout HpVertexOutput Out)
    {
        // Nothing: `Out` is the mesh's own data, so the default is the
        // transform chain exactly as it was before T0146 existed.
    }

    /// The surface-stage coordinate hook (T0141.7, 141.8): whatever this
    /// returns is the `VSOutput` every getter below samples with, so
    /// displacing its texture coordinates re-aims every map at once.
    /// Default: parallax occlusion over the material's height map when it
    /// carries one, untouched otherwise. Runs before any texture is read --
    /// this *is* the "hook before sampling" D26 was decided to create.
    ///
    /// Under triplanar this hook is a no-op, deliberately: the coordinates a
    /// triplanar material samples are the three world-space projections,
    /// which do not live in `VSOutput`, so parallax runs inside the
    /// triplanar basis instead -- per projection, in that projection's
    /// axis-aligned frame (T0156). A method that transforms `VSOutput`
    /// cannot express a multi-projection technique; the seam that will is
    /// T0153.1's per-tap sampling override, decided there.
    [mutating] VSOutput surfaceCoordinates(VSOutput VSOut, HpSurfaceInput In)
    {
#if HP_USE_HEIGHT_MAP && USE_TEXCOORD0 && !HP_TRIPLANAR
        VSOut.UV0 = HpParallaxUv(VSOut.UV0, In);
#endif
        return VSOut;
    }

    /// Base colour, linear RGB + alpha, after every texture and factor.
    /// Default: DiligentFX's getter -- sRGB decode, UV selection and the
    /// vertex-colour multiply included; returns the factor for an empty slot.
    [mutating] float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
#if HP_TRIPLANAR
        float4 baseColor = float4(1.0, 1.0, 1.0, 1.0);
#if USE_COLOR_MAP
        baseColor = HpTriplanarSample(g_BaseColorMap, g_BaseColorMap_sampler,
                                      HpTriplanarBasis(In), g_Frame.Renderer.MipBias);
        baseColor = float4(TO_LINEAR(baseColor.rgb), baseColor.a);
#endif
        // `In.VertexColor` is white when the mesh carries none (the contract
        // promises so), which makes this the same multiply their getter does.
        baseColor *= In.VertexColor;
        return baseColor * g_Material.Basic.BaseColorFactor;
#else
        return GetBaseColor(VSOut, g_Material, g_Frame.Renderer.MipBias,
                            g_Material.Basic.BaseColorFactor);
#endif
    }

    /// Metallic in `x`, roughness in `y`. Default: glTF's packing -- roughness
    /// in green and metallic in blue -- times the material factors.
    /// `GetPhysicalDesc` returns all ones for an empty slot, so the factors
    /// survive untextured materials unchanged.
    [mutating] float2 metallicRoughness(VSOutput VSOut, HpSurfaceInput In)
    {
#if HP_TRIPLANAR && USE_PHYS_DESC_MAP
        float4 physicalDesc = HpTriplanarSample(g_PhysicalDescriptorMap,
                                                g_PhysicalDescriptorMap_sampler,
                                                HpTriplanarBasis(In), g_Frame.Renderer.MipBias);
#else
        float4 physicalDesc = GetPhysicalDesc(VSOut, g_Material, g_Frame.Renderer.MipBias);
#endif
        return float2(physicalDesc.b * g_Material.Basic.MetallicFactor,
                      physicalDesc.g * g_Material.Basic.RoughnessFactor);
    }

    /// How much ambient light reaches the fragment, 0 to 1.
    [mutating] float occlusion(VSOutput VSOut, HpSurfaceInput In)
    {
#if HP_TRIPLANAR && USE_AO_MAP
        return HpTriplanarSample(g_OcclusionMap, g_OcclusionMap_sampler, HpTriplanarBasis(In),
                                 g_Frame.Renderer.MipBias).r *
               g_Material.Basic.OcclusionFactor;
#elif USE_AO_MAP
        return GetOcclusion(VSOut, g_Material, g_Frame.Renderer.MipBias);
#else
        return g_Material.Basic.OcclusionFactor;
#endif
    }

    /// Emissive radiance, linear RGB, deliberately not clamped to 1 (T0096).
    [mutating] float3 emissive(VSOutput VSOut, HpSurfaceInput In)
    {
#if HP_TRIPLANAR && USE_EMISSIVE_MAP
        float3 emissive = TO_LINEAR(HpTriplanarSample(g_EmissiveMap, g_EmissiveMap_sampler,
                                                      HpTriplanarBasis(In),
                                                      g_Frame.Renderer.MipBias).rgb);
        return emissive * float3(g_Material.Basic.EmissiveFactorR,
                                 g_Material.Basic.EmissiveFactorG,
                                 g_Material.Basic.EmissiveFactorB);
#elif USE_EMISSIVE_MAP
        return GetEmissive(VSOut, g_Material, g_Frame.Renderer.MipBias);
#else
        return float3(g_Material.Basic.EmissiveFactorR, g_Material.Basic.EmissiveFactorG,
                      g_Material.Basic.EmissiveFactorB);
#endif
    }

    /// The world-space normal lighting will use. Default: the material's
    /// normal map through DiligentFX's tangent-frame reconstruction.
    ///
    /// `geometricNormal` arrives **unflipped**: `GetPerturbNormalInfo` applies
    /// the two-sided flip itself from `isFrontFace`, and handing it the
    /// already-flipped normal flips twice -- a double-sided surface lit
    /// correctly from the front and inverted from behind, which looks like a
    /// handedness bug and is not one.
    ///
    /// `isFrontFace` is **glTF facing, not raw `SV_IsFrontFace`** (T0152.5):
    /// under a mirrored node -- a negative-determinant transform -- the
    /// engine has already folded the determinant sign in, so `true` always
    /// means the authored front of the surface and a material never needs to
    /// know whether the node it is drawn under is mirrored.
    [mutating] float3 shadingNormal(VSOutput VSOut, HpSurfaceInput In, float3 geometricNormal,
                         bool isFrontFace)
    {
        PerturbNormalInfo normalInfo = GetPerturbNormalInfo(
            In.WorldPos, geometricNormal, isFrontFace, g_Frame.Camera.fHandness);

#if HP_TRIPLANAR
        // The geometric normal only: reorienting a tangent-space map per
        // projection plane is real work (a whiteout blend with per-plane
        // swizzles) that arrives when something needs it, and a silently
        // wrong basis would be worse than a flat one. `Material::triplanar`
        // documents this.
        return normalInfo.Normal;
#elif USE_TEXCOORD0 || USE_TEXCOORD1
        float2 normalUv = SelectUV(
            VSOut, UnpackPBRMaterialTextureUVSelector(
                       g_Material.Textures[NormalTextureAttribId].PackedProps));
        float3 microNormal = GetMicroNormal(g_Material, normalUv, normalUv, ddx(normalUv),
                                            ddy(normalUv), g_Frame.Renderer.MipBias);
        return PerturbNormal(normalInfo, ddx(normalUv), ddy(normalUv), microNormal,
                             /*HasUV = */ true);
#else
        return normalInfo.Normal;
#endif
    }

    // -----------------------------------------------------------------------
    // The extended layers' channel hooks (T0143). Same shape as every channel
    // above: the default is DiligentFX's getter -- map times factor, zero in a
    // permutation without the feature -- and an override replaces exactly that
    // channel. The engine derives what lighting needs (the anisotropy frame,
    // the coat's F0, iridescence's bend of `Reflectance0`) *after* `surface()`
    // has run, so a value written from any hook is the value lit.
    // -----------------------------------------------------------------------

    /// Clearcoat layer strength. Default: the map times the material factor.
    [mutating] float clearcoat(VSOutput VSOut, HpSurfaceInput In)
    {
        return GetClearcoatFactor(VSOut, g_Material, g_Frame.Renderer.MipBias);
    }

    /// The coat's own perceptual roughness.
    [mutating] float clearcoatRoughness(VSOutput VSOut, HpSurfaceInput In)
    {
        return GetClearcoatRoughness(VSOut, g_Material, g_Frame.Renderer.MipBias);
    }

    /// The coat's world-space normal. Default mirrors `RenderPBR.psh`'s rule
    /// (`ReadClearcoatLayerProperties`): the *geometric* normal unless the
    /// material carries a clearcoat normal map -- the base material's normal
    /// map is deliberately **not** inherited by the coat, per upstream and per
    /// the glTF extension.
    ///
    /// `isFrontFace` is glTF facing, exactly as `shadingNormal` receives it.
    [mutating] float3 clearcoatNormal(VSOutput VSOut, HpSurfaceInput In, float3 geometricNormal,
                                      bool isFrontFace)
    {
        PerturbNormalInfo normalInfo = GetPerturbNormalInfo(
            In.WorldPos, geometricNormal, isFrontFace, g_Frame.Camera.fHandness);
#if ENABLE_CLEAR_COAT && USE_CLEAR_COAT_NORMAL_MAP && (USE_TEXCOORD0 || USE_TEXCOORD1)
        float selector = UnpackPBRMaterialTextureUVSelector(
            g_Material.Textures[ClearCoatNormalTextureAttribId].PackedProps);
        // "If clearcoatNormalTexture is not given, no normal mapping is
        // applied to the clear coat layer, even if normal mapping is applied
        // to the base material" -- upstream's comment, upstream's selector
        // test (`RenderPBR.psh:203-215`).
        if (selector >= 0.0)
        {
            float2 coatUv = SelectUV(VSOut, selector);
            float3 coatNormal = GetClearcoatNormal(g_Material, coatUv, coatUv, ddx(coatUv),
                                                   ddy(coatUv), g_Frame.Renderer.MipBias);
            return PerturbNormal(normalInfo, ddx(coatUv), ddy(coatUv), coatNormal,
                                 /*HasUV = */ true);
        }
#endif
        return normalInfo.Normal;
    }

    /// Sheen colour, linear RGB. Black -- the default in a permutation
    /// without sheen -- is "no sheen".
    [mutating] float3 sheenColor(VSOutput VSOut, HpSurfaceInput In)
    {
        return GetSheenColor(VSOut, g_Material, g_Frame.Renderer.MipBias);
    }

    /// Sheen roughness.
    [mutating] float sheenRoughness(VSOutput VSOut, HpSurfaceInput In)
    {
        return GetSheenRoughness(VSOut, g_Material, g_Frame.Renderer.MipBias);
    }

    /// Anisotropy, **packed as upstream's getter packs it**: `xy` the
    /// tangent-space direction in [-1, 1] *before* the material's authored
    /// rotation, `z` the strength times the material factor. The engine
    /// applies the rotation and splits the pack into
    /// `HpSurfaceOutput::Anisotropy` / `AnisotropyDirection`.
    [mutating] float3 anisotropy(VSOutput VSOut, HpSurfaceInput In)
    {
        return GetAnisotropy(VSOut, g_Material, g_Frame.Renderer.MipBias);
    }

    /// Thin-film iridescence factor.
    [mutating] float iridescence(VSOutput VSOut, HpSurfaceInput In)
    {
        return GetIridescence(VSOut, g_Material, g_Frame.Renderer.MipBias);
    }

    /// Thin-film thickness, nanometres -- the map lerps between the
    /// material's authored minimum and maximum.
    [mutating] float iridescenceThickness(VSOutput VSOut, HpSurfaceInput In)
    {
        return GetIridescenceThickness(VSOut, g_Material, g_Frame.Renderer.MipBias);
    }

    /// Transmission factor.
    [mutating] float transmission(VSOutput VSOut, HpSurfaceInput In)
    {
        return GetTransmission(VSOut, g_Material, g_Frame.Renderer.MipBias);
    }

    /// Volume thickness, metres. Carried to `HpShadedSurface::Thickness`;
    /// shades nothing until T0087's image-based refraction exists.
    [mutating] float thickness(VSOutput VSOut, HpSurfaceInput In)
    {
        return GetVolumeThickness(VSOut, g_Material, g_Frame.Renderer.MipBias);
    }

    /// The whole-output hook, run after every channel above has been
    /// assembled -- everything above is the material's per-channel answers,
    /// this acts on the assembled result.
    ///
    /// **This is where D27's function-style hook lives now** (T0142.13): the
    /// default calls the engine's own `HpSurface`, and a material that wants
    /// the hook overrides this method. A game cannot define `HpSurface`
    /// itself -- the engine's body is already in scope when the module is
    /// included -- so this method is the whole of it.
    [mutating] void surface(HpSurfaceInput In, inout HpSurfaceOutput Out)
    {
        HpSurface(In, Out);
    }

    /// Whether this material skips lighting entirely (T0142.16) -- Godot's
    /// `render_mode unshaded`, as an interface method with a default rather
    /// than a macro, which is D28's shape for every option.
    ///
    /// **Statically specialised, but NOT folded at the SPIR-V level -- and
    /// that is measured, not assumed** (T0142.16). Slang emits a direct call
    /// to the specialised method and a real branch on its result; the
    /// bytecode of an overriding module is within a dozen bytes of the shaded
    /// one, at optimization level none *and* default. The lighting is
    /// expected to fold in the driver's compiler, where the call is trivially
    /// inlined -- but that is expectation, not proof, and T0151's link-time
    /// constants are the mechanism that makes eliminations provable. Until
    /// then: for a *standard* material, `Material::unlit` rides the
    /// `HP_UNSHADED` permutation bit and genuinely excludes the lighting at
    /// preprocess time; this method is the authored equivalent with exact
    /// semantics and unproven-but-likely folding.
    bool unshaded()
    {
        return false;
    }

    /// **The per-light method** — D30's rung 3, and Godot's `light()` with more
    /// than Godot gives it (T0145).
    ///
    /// Called once per light that reaches this fragment, with range
    /// attenuation and the spot cone **already resolved by the engine** — so an
    /// override changes how light is *applied* without reimplementing which
    /// light arrives. Answer with the four factors the engine multiplies:
    ///
    ///     Punctual += (Diffuse + Specular) * Intensity * NdotL
    ///
    /// Default: `HpStandardLight`, which is DiligentFX's `SmithGGX_BRDF` — so
    /// a material that overrides nothing renders bit-identically to the build
    /// before this method existed. Cel shading is that default with `NdotL`
    /// quantised; `HpMaterial.slang` carries the worked example.
    ///
    /// **Zero-attenuation lights never arrive here.** The engine skips a light
    /// outside its own range or cone before calling, mirroring upstream, so an
    /// override never has to test for it.
    ///
    /// **This is the seam a post-process cannot be.** A pass over the summed
    /// image quantises the *total*, so a two-light surface bands wrongly and
    /// per-light rim terms are unrecoverable — which is Godot proposal #484's
    /// exact complaint, and D30's argument for owning the loop.
    [mutating] HpLightResponse light(HpLight Light, HpShadedSurface Surface, HpSurfaceInput In)
    {
        return HpStandardLight(Light, Surface);
    }

    /// **The whole lighting stage** — D30's rung 4, which Godot does not have
    /// at any price short of a fork (T0145).
    ///
    /// Owns the loop, the accumulation and the resolve. The default below *is*
    /// the engine's standard path, written out rather than hidden, so an
    /// override can start by copying it: `HpLightCount()`, `HpGetLight(i, pos)`
    /// and `HpResolveLighting(Surface, punctual)` are the primitives it is
    /// built from, and all three are contract.
    ///
    /// Reach for this rather than `light()` when the *loop* is the technique —
    /// a dominant-light-only look, a per-light budget, an accumulation that is
    /// not a sum. **T0093's visibility term belongs here**, or in `surface()`:
    /// visibility is not illumination, so it multiplies the resolved colour and
    /// never a light's contribution.
    ///
    /// @param Surface the surface every hook above finally described.
    /// @param In the same contract inputs the surface stage saw.
    /// @returns linear RGB, pre-tonemap. Alpha stays the surface stage's.
    [mutating] float3 lighting(HpShadedSurface Surface, HpSurfaceInput In)
    {
        float3 punctual = float3(0.0, 0.0, 0.0);
        // The layer accumulators (T0143) -- `SurfaceLightingInfo`'s Sheen and
        // Clearcoat layers, flattened. Dead locals the compiler removes in a
        // permutation without the features; D31's shape rule is about contract
        // *structs*, and these are neither.
        float3 sheenPunctual = float3(0.0, 0.0, 0.0);
        float3 clearcoatPunctual = float3(0.0, 0.0, 0.0);

        int lightCount = HpLightCount();
        for (int i = 0; i < lightCount; ++i)
        {
            HpLight Light = HpGetLight(i, Surface.Position);
            if (Light.Attenuation <= 0.0)
            {
                // Mirrors `ApplyPunctualLight`'s own early return (:661). Out
                // of range or outside the cone contributes nothing, and
                // skipping keeps a rung-3 override off the fragments where it
                // could only multiply by zero.
                continue;
            }

            HpLightResponse Response = this.light(Light, Surface, In);
            // **The accumulation, in upstream's exact expression order**
            // (`:693`). `(D + S) * I * N` and not `D*I*N + S*I*N`: floating
            // point multiplication does not distribute, and the byte-identical
            // guard on the standard material is what caught the difference.
            float3 base = (Response.Diffuse + Response.Specular) * Response.Intensity *
                          Response.NdotL;

#if ENABLE_SHEEN
            {
                // ---- mirrored from `ApplyPunctualLight`, :702-711 (T0143) --
                // The sheen lobe accumulates into its own layer, and the LUT's
                // albedo scaling takes back from the base what the sheen layer
                // now reflects -- energy conservation, precomputed. The base
                // is scaled *after* the full `(D+S)*I*N` product, upstream's
                // exact order.
                sheenPunctual += ApplyDirectionalLightSheen(Light.Direction, Light.Radiance,
                                                            Surface.SheenColor,
                                                            Surface.SheenRoughness,
                                                            Surface.Normal, Surface.View);

                float MaxFactor = max(max(Surface.SheenColor.r, Surface.SheenColor.g),
                                      Surface.SheenColor.b);
                float SheenNdotL = dot_sat(Surface.Normal, Light.ToLight);
                float AlbedoScaling =
                    min(1.0 - MaxFactor * g_SheenAlbedoScalingLUT
                                              .Sample(g_SheenAlbedoScalingLUT_sampler,
                                                      float2(Surface.NdotV,
                                                             Surface.SheenRoughness))
                                              .r,
                        1.0 - MaxFactor * g_SheenAlbedoScalingLUT
                                              .Sample(g_SheenAlbedoScalingLUT_sampler,
                                                      float2(SheenNdotL,
                                                             Surface.SheenRoughness))
                                              .r);
                base *= AlbedoScaling;
            }
#endif

            punctual += base;

#if ENABLE_CLEAR_COAT
            {
                // ---- mirrored from :716-720 --------------------------------
                // The coat is a second GGX evaluated with its own normal and
                // reflectance; its factor is applied in the resolve, exactly
                // where `GetClearcoatLighting` applies it.
                SurfaceReflectanceInfo ccSrf;
                ccSrf.PerceptualRoughness = Surface.ClearcoatRoughness;
                ccSrf.DiffuseColor = float3(0.0, 0.0, 0.0);
                ccSrf.Reflectance0 = float3(Surface.ClearcoatReflectance0,
                                            Surface.ClearcoatReflectance0,
                                            Surface.ClearcoatReflectance0);
                ccSrf.Reflectance90 = float3(1.0, 1.0, 1.0);
                clearcoatPunctual += ApplyDirectionalLightGGX(Light.Direction, Light.Radiance,
                                                              ccSrf, Surface.ClearcoatNormal,
                                                              Surface.View);
            }
#endif
        }

        return HpResolveLighting(Surface, punctual, sheenPunctual, clearcoatPunctual);
    }
}

/// The engine's standard PBR material: every default, nothing overridden.
///
/// **The emptiness is the claim 142.2 makes** -- the standard material is not
/// special-cased anywhere, it is merely the interface with all its defaults,
/// so a game material that overrides one method differs from it by exactly
/// that method.
struct HpStandardMaterial : IHpMaterial
{
};

// ---------------------------------------------------------------------------
// The custom material module (T0142.15, D28's authoring surface).
//
// `HpMaterialModule.generated` is emitted per pipeline by `SurfacePipeline`:
// a comment for standard pipelines, `#include "<the material's .slang>"` for
// pipelines whose material names a shader asset -- resolved through the VFS
// source (T0142.14), so the module is content like everything else.
//
// The module defines `struct HpMaterial : IHpMaterial` and overrides what it
// wants; `override` is mandatory (omitting it is a compile error), and every
// method it does not override keeps the standard material's behaviour above.
// It is included **here**, after the interface and DiligentFX's getters, so
// an override may call `GetBaseColor` and friends without including anything
// itself -- D27's "we include them, they include us", one level deeper.
//
// The include sits outside the `#if` for the *hasher's* sake and inside it
// for the compiler's: content hashing walks `#include` lines textually,
// before preprocessing, so the file must always resolve -- which is why the
// generated file always exists, even when this macro leaves it unused.
// ---------------------------------------------------------------------------
#if HP_CUSTOM_MATERIAL
#include "HpMaterialModule.generated"
#endif

/// Evaluates one fragment through a material (T0142.2).
///
/// Generic over the material and **statically specialised** -- `main` below
/// instantiates it with `HpStandardMaterial` and slang emits direct calls, no
/// dispatch. This is the body `main` used to be; it moved behind a type
/// parameter so the same code renders any conforming material.
PSOutput evaluateSurface<TMaterial : IHpMaterial>(inout TMaterial material, VSOutput VSOut,
                                                  bool IsFrontFace)
{
    // **The glTF determinant rule, shader half** (T0152.5, D33). A mirrored
    // node -- negative entity scale, or a negative-determinant glTF node
    // transform -- reverses every triangle's screen-space winding, so the
    // facing the rasteriser reports is the *opposite* of the facing glTF
    // defines. Everything downstream wants glTF facing: the two-sided normal
    // flip exists to point authored normals at the viewer, and under a mirror
    // the raw `SV_IsFrontFace` makes it do exactly the opposite -- geometry
    // present (the CPU half in `SceneRenderer` flips the cull face) but lit
    // from the wrong side, `N·L` negative, black. The sign comes from the
    // node matrix already bound in `cbPrimitiveAttribs`, not from a flag the
    // CPU writes, so the two halves cannot drift apart; it is
    // transpose-invariant, so the packing convention is irrelevant.
    bool gltfFrontFace = IsFrontFace;
    if (determinant((float3x3)g_Primitive.Transforms.NodeMatrix) < 0.0)
    {
        gltfFrontFace = !gltfFrontFace;
    }

    // **Every optional field is guarded, and the guards are the same macros
    // `GetVSOutputStruct` generates the struct from.** A mesh with no texture
    // coordinates produces a `VSOutput` with no `UV0` member at all, so reading
    // it unconditionally is a *compile* error in exactly the permutation that
    // lacks it -- which is how this was found, on the untextured quad the lit
    // surface test draws.
    //
    // The contract absorbs that: `HpMaterial.slang` documents UV1 as zero when the
    // mesh carries one set, and vertex colour as white when it carries none, so
    // a game shader may read either unconditionally and never sees the
    // permutation.
    HpSurfaceInput surfaceIn;
#if USE_TEXCOORD0
    surfaceIn.UV0 = VSOut.UV0;
#else
    surfaceIn.UV0 = float2(0.0, 0.0);
#endif
#if USE_TEXCOORD1
    surfaceIn.UV1 = VSOut.UV1;
#else
    surfaceIn.UV1 = float2(0.0, 0.0);
#endif
    // **The undisplaced UV0, preserved** (T0159.3). `UV0` above is refreshed
    // from the coordinate hook's return, so after a parallax march it is the
    // *displaced* coordinate whose screen-space derivatives are discontinuous.
    // This copy is taken before the hook runs and never overwritten -- the
    // smooth, interpolated coordinate a technique that needs continuous
    // derivatives (a shadow march's tangent frame, T0158's measurement) reads
    // instead.
    surfaceIn.UV0Base = surfaceIn.UV0;
    surfaceIn.WorldPos = VSOut.WorldPos.xyz;
    // **The unflipped geometric normal, kept separately.** `GetPerturbNormalInfo`
    // applies the two-sided flip itself, from the `IsFrontFace` it is handed --
    // so giving it the already-flipped `surfaceIn.Normal` would flip twice and
    // hand back the *away-facing* normal on exactly the back faces the flip
    // exists to fix. The bug that produces is a double-sided surface lit
    // correctly from the front and inverted from behind, which looks like a
    // normal-map handedness problem and is not one.
    float3 geometricNormal;
#if USE_VERTEX_NORMALS
    // **Flipped for back faces, and leaving this out cost an afternoon.** A
    // double-sided surface seen from behind has a geometric normal pointing away
    // from the viewer, so every `N·L` comes out negative, clamps to zero, and the
    // surface renders **pure black** -- with the light present, the loop running
    // and the material read correctly. Diagnosed by outputting the light count
    // and `N·L` as colours: light count 1, `N·L` 0.
    //
    // `GetPerturbNormalInfo` does the same thing on DiligentFX's own path, which
    // is why the old renderer lit this quad and the first version of this shader
    // did not.
    geometricNormal = normalize(VSOut.Normal);
    surfaceIn.Normal = geometricNormal * (gltfFrontFace ? 1.0 : -1.0);
#else
    // No normals on the mesh. Facing the camera is the least wrong answer: it
    // keeps the BRDF finite and the surface visible, where a zero normal makes
    // every lighting term collapse to black with no diagnostic.
    //
    // **+Z since T0165**: the camera looks down its own −Z
    // (`hp::kRightHandedCameraSpace`), so +Z is what points back at it. It was
    // −Z while the camera looked +Z, and it is the same sentence either way.
    surfaceIn.Normal = float3(0.0, 0.0, 1.0);
    // Zero, deliberately: `GetPerturbNormalInfo` treats a zero-length normal as
    // "derive one from the position gradients", which is a better answer for a
    // mesh with no normals than the constant above and is the only place that
    // distinction can be made.
    geometricNormal = float3(0.0, 0.0, 0.0);
#endif
#if USE_VERTEX_TANGENTS
    // **The mesh's tangent, at last** (T0159.4). `RenderPBR.vsh` rotates the
    // vertex tangent into world space and normalizes per vertex; interpolation
    // denormalizes slightly, so it is renormalized here. `w` is **always +1**,
    // a decision rather than data: Diligent's vertex path carries `float3`
    // (`VSTangentAttrib` is `VT_FLOAT32, 3`), so glTF's handedness never
    // survives the loader -- see the contract's field comment for what that
    // means to a shader.
    surfaceIn.Tangent = float4(normalize(VSOut.Tangent), 1.0);
#else
    // Zero when the mesh carries no tangents -- the documented fallback a
    // surface function must tolerate, because the engine cannot invent
    // tangents a mesh does not have.
    surfaceIn.Tangent = float4(0.0, 0.0, 0.0, 1.0);
#endif
    // **Computed, not interpolated.** `HpMaterial.slang` argues the case: a field
    // in the contract does not imply an interpolator, and burning one on a value
    // this cheap would be paid by every draw whether a shader reads it or not.
    surfaceIn.CameraPos = g_Frame.Camera.f4Position.xyz;
    surfaceIn.ViewDir = normalize(surfaceIn.CameraPos - surfaceIn.WorldPos);
    // Frame-wide, one cbuffer read -- no interpolator, per the contract's rule.
    surfaceIn.Time = g_Frame.Renderer.Time;
    // **The vertex hook's channel into this stage** (T0146.4). The four slots
    // exist in `VSOutput` only for a custom-material permutation --
    // `SurfacePipeline::build` appends them to the generated struct when a
    // module is compiled in -- so a standard material's interpolator count is
    // untouched and these read the documented zero.
#if HP_CUSTOM_MATERIAL
    surfaceIn.Custom0 = VSOut.HpCustom0;
    surfaceIn.Custom1 = VSOut.HpCustom1;
    surfaceIn.Custom2 = VSOut.HpCustom2;
    surfaceIn.Custom3 = VSOut.HpCustom3;
#else
    surfaceIn.Custom0 = float4(0.0, 0.0, 0.0, 0.0);
    surfaceIn.Custom1 = float4(0.0, 0.0, 0.0, 0.0);
    surfaceIn.Custom2 = float4(0.0, 0.0, 0.0, 0.0);
    surfaceIn.Custom3 = float4(0.0, 0.0, 0.0, 0.0);
#endif
#if USE_VERTEX_COLORS
    surfaceIn.VertexColor = VSOut.Color;
#else
    // White, so a shader may multiply by it unconditionally -- which is exactly
    // what `HpMaterial.slang` promises.
    surfaceIn.VertexColor = float4(1.0, 1.0, 1.0, 1.0);
#endif
    surfaceIn.ScreenPos = VSOut.ClipPos;
    // **The screen coordinate the snapshots are addressed by** (T0147).
    // `ClipPos` is `SV_POSITION`, in *target* pixels; the snapshots are
    // target-sized; so this one multiply is the whole conversion. Computed
    // rather than interpolated, per the contract's rule — it is two
    // instructions off a value the stage already has.
    surfaceIn.ScreenUV = VSOut.ClipPos.xy * HP_TARGET_SIZE.zw;

    // -----------------------------------------------------------------------
    // Every channel through the material's methods (142.2). For
    // `HpStandardMaterial` these are the interface defaults -- DiligentFX's
    // getters, unchanged from when this sequence was written inline -- so a
    // material that overrides nothing produces the standard material, and one
    // that overrides a single method changes exactly that channel.
    //
    // **`VSOut` is passed by value, and that is the hook.** 141.7's parallax
    // and 141.8's triplanar override these methods, build a UV-displaced copy
    // of it, and hand *that* to the same getters. Owning `main` is what makes
    // the surface stage; owning the sampling never was.
    // -----------------------------------------------------------------------

    // **The coordinate hook runs first** (141.7/141.8): the returned copy is
    // what every getter samples, and the contract's UVs are refreshed from it
    // so a game's `HpSurface` sees the coordinates that were actually used.
    VSOut = material.surfaceCoordinates(VSOut, surfaceIn);
#if USE_TEXCOORD0
    surfaceIn.UV0 = VSOut.UV0;
#endif
#if USE_TEXCOORD1
    surfaceIn.UV1 = VSOut.UV1;
#endif

    HpSurfaceOutput surfaceOut;
    surfaceOut.BaseColor = material.baseColor(VSOut, surfaceIn);
    {
        float2 metallicRoughness = material.metallicRoughness(VSOut, surfaceIn);
        surfaceOut.Metallic = metallicRoughness.x;
        surfaceOut.Roughness = metallicRoughness.y;
    }
    surfaceOut.Occlusion = material.occlusion(VSOut, surfaceIn);
    surfaceOut.Emissive = material.emissive(VSOut, surfaceIn);
    surfaceOut.Normal = material.shadingNormal(VSOut, surfaceIn, geometricNormal, gltfFrontFace);

    // The extended layers' channels (T0143). In a permutation without a
    // feature its getter-backed default is a literal zero, so these lines
    // cost the standard material nothing and the fields still exist for
    // `surface()` to write -- D31's shape rule, spent here.
    surfaceOut.Clearcoat = material.clearcoat(VSOut, surfaceIn);
    surfaceOut.ClearcoatRoughness = material.clearcoatRoughness(VSOut, surfaceIn);
    surfaceOut.ClearcoatNormal =
        material.clearcoatNormal(VSOut, surfaceIn, geometricNormal, gltfFrontFace);
    surfaceOut.SheenColor = material.sheenColor(VSOut, surfaceIn);
    surfaceOut.SheenRoughness = material.sheenRoughness(VSOut, surfaceIn);
    {
        // The pack is upstream's (`GetAnisotropy`): direction before the
        // authored rotation in xy, strength in z. The rotation is applied
        // here, mirroring `ReadAnisotropyProperties` (`RenderPBR.psh:262-266`),
        // so `AnisotropyDirection` is final and an override of `surface()`
        // writes a *final* direction too.
        float3 packedAnisotropy = material.anisotropy(VSOut, surfaceIn);
        surfaceOut.Anisotropy = packedAnisotropy.z;
#if ENABLE_ANISOTROPY
        float2 rotationCS =
            float2(cos(g_Material.Anisotropy.Rotation), sin(g_Material.Anisotropy.Rotation));
        surfaceOut.AnisotropyDirection =
            float2(packedAnisotropy.x * rotationCS.x - packedAnisotropy.y * rotationCS.y,
                   packedAnisotropy.x * rotationCS.y + packedAnisotropy.y * rotationCS.x);
#else
        surfaceOut.AnisotropyDirection = float2(1.0, 0.0);
#endif
    }
    surfaceOut.Iridescence = material.iridescence(VSOut, surfaceIn);
    surfaceOut.IridescenceThickness = material.iridescenceThickness(VSOut, surfaceIn);
    surfaceOut.Transmission = material.transmission(VSOut, surfaceIn);
    surfaceOut.Thickness = material.thickness(VSOut, surfaceIn);

    // **The surface stage's whole-output hook.** Everything above is the
    // material's per-channel answers; this is the point parallax, triplanar
    // and layer blending act on the assembled result (141.7, 141.8), and its
    // default routes through D27's `HpSurface` function.
    material.surface(surfaceIn, surfaceOut);

#if PBR_ALPHA_MODE == PBR_ALPHA_MODE_MASK
    // The cutout test, mirroring `RenderPBR.psh` (T0060's `AlphaMode::Mask`).
    // Compile-time on the key's alpha mode, exactly as theirs is -- an opaque
    // pipeline carries no discard at all, which matters because a shader with
    // any `discard` in it disables early depth on most hardware.
    if (surfaceOut.BaseColor.a < g_Material.Basic.AlphaMaskCutoff)
    {
        discard;
    }
#endif

    PSOutput Out;

    // -----------------------------------------------------------------------
    // Debug views (T0141) — one channel on its own, instead of the shaded image.
    //
    // **A surface has half a dozen inputs and one output**, so a wrong input is
    // invisible: a normal map that is never applied, an occlusion channel read
    // from the wrong component and a roughness pinned at 1 all produce an image
    // that still looks like a lit material. This is how each becomes something
    // you can look at.
    //
    // **A runtime branch, unlike `HP_UNSHADED`, and deliberately so.** Unshaded
    // is a *material* property that a game ships, so paying for it in registers
    // on every draw would be wrong. A debug view is an editor control that flips
    // between frames, and making it a PSO permutation would mean a pipeline
    // rebuild — a visible stall — every time someone changed the dropdown. It
    // also multiplies the variant count by nine for something no shipped game
    // ever sets. The cost is one compare against a constant-buffer value, which
    // is uniform across the draw and costs nothing measurable.
    //
    // Values are `hp::SurfaceDebugView`, which restates DiligentFX's
    // `DebugViewType` numbering so the two cannot drift.
    // -----------------------------------------------------------------------
    if (g_Frame.Renderer.DebugView != 0)
    {
        float4 debug = float4(0.0, 0.0, 0.0, 1.0);
        if (g_Frame.Renderer.DebugView == 1) // Texcoord0
        {
            debug.rg = frac(surfaceIn.UV0);
        }
        else if (g_Frame.Renderer.DebugView == 3) // BaseColor
        {
            debug.rgb = surfaceOut.BaseColor.rgb;
        }
        else if (g_Frame.Renderer.DebugView == 5) // Occlusion
        {
            debug.rgb = float3(surfaceOut.Occlusion, surfaceOut.Occlusion, surfaceOut.Occlusion);
        }
        else if (g_Frame.Renderer.DebugView == 6) // Emissive
        {
            debug.rgb = surfaceOut.Emissive;
        }
        else if (g_Frame.Renderer.DebugView == 7) // Metallic
        {
            debug.rgb = float3(surfaceOut.Metallic, surfaceOut.Metallic, surfaceOut.Metallic);
        }
        else if (g_Frame.Renderer.DebugView == 8) // Roughness
        {
            debug.rgb = float3(surfaceOut.Roughness, surfaceOut.Roughness, surfaceOut.Roughness);
        }
        else if (g_Frame.Renderer.DebugView == 12) // MeshNormal
        {
            // The geometric normal, *before* the normal map -- which is the
            // point: on a flat quad this is one constant colour, so a
            // ShadingNormal that looks identical proves the map is not applied.
            debug.rgb = normalize(surfaceIn.Normal) * 0.5 + 0.5;
        }
        else if (g_Frame.Renderer.DebugView == 13) // ShadingNormal
        {
            debug.rgb = normalize(surfaceOut.Normal) * 0.5 + 0.5;
        }
        // The extended layers (T0143), encodings matching upstream's
        // `GetDebugColor` per view. **Every branch reads a field that exists
        // in every permutation** (D31), so a feature that is off debugs as
        // its zero rather than as a missing mode -- which is itself the
        // diagnostic: a channel that should be non-zero and is not, is the
        // occlusion bug this subtask exists to never repeat.
        else if (g_Frame.Renderer.DebugView == 21) // ClearCoatFactor
        {
            debug.rgb = float3(surfaceOut.Clearcoat, surfaceOut.Clearcoat, surfaceOut.Clearcoat);
        }
        else if (g_Frame.Renderer.DebugView == 22) // ClearCoatRoughness
        {
            debug.rgb = float3(surfaceOut.ClearcoatRoughness, surfaceOut.ClearcoatRoughness,
                               surfaceOut.ClearcoatRoughness);
        }
        else if (g_Frame.Renderer.DebugView == 23) // ClearCoatNormal
        {
            debug.rgb = normalize(surfaceOut.ClearcoatNormal) * 0.5 + 0.5;
        }
        else if (g_Frame.Renderer.DebugView == 25) // SheenColor
        {
            debug.rgb = surfaceOut.SheenColor;
        }
        else if (g_Frame.Renderer.DebugView == 26) // SheenRoughness
        {
            debug.rgb = float3(surfaceOut.SheenRoughness, surfaceOut.SheenRoughness,
                               surfaceOut.SheenRoughness);
        }
        else if (g_Frame.Renderer.DebugView == 27) // AnisotropyStrength
        {
            debug.rgb = float3(surfaceOut.Anisotropy, surfaceOut.Anisotropy,
                               surfaceOut.Anisotropy);
        }
        else if (g_Frame.Renderer.DebugView == 28) // AnisotropyDirection
        {
            debug.rgb = float3(surfaceOut.AnisotropyDirection * 0.5 + 0.5, 0.0);
        }
        else if (g_Frame.Renderer.DebugView == 30) // IridescenceFactor
        {
            debug.rgb = float3(surfaceOut.Iridescence, surfaceOut.Iridescence,
                               surfaceOut.Iridescence);
        }
        else if (g_Frame.Renderer.DebugView == 31) // IridescenceThickness
        {
            // Upstream's own normalisation: full white at 1200 nm.
            debug.rgb = float3(surfaceOut.IridescenceThickness, surfaceOut.IridescenceThickness,
                               surfaceOut.IridescenceThickness) /
                        1200.0;
        }
        else if (g_Frame.Renderer.DebugView == 32) // Transmission
        {
            debug.rgb = float3(surfaceOut.Transmission, surfaceOut.Transmission,
                               surfaceOut.Transmission);
        }
        else if (g_Frame.Renderer.DebugView == 33) // Thickness
        {
            debug.rgb = float3(surfaceOut.Thickness, surfaceOut.Thickness, surfaceOut.Thickness);
        }
        Out.Color = debug;
        return Out;
    }

#if HP_UNSHADED
    // Straight out, no BRDF, no light loop, no IBL. **A compile-time branch and
    // not a runtime one** -- an `if` would still pay for the lighting code in
    // registers and in compilation, which is the entire thing unshaded exists to
    // avoid. This is the *data* half (`Material::unlit`, the permutation bit);
    // the *authored* half is `IHpMaterial.unshaded()` below (T0142.16), and
    // both produce this same output.
    Out.Color = surfaceOut.BaseColor;
    Out.Color.rgb += surfaceOut.Emissive;
#else
    if (material.unshaded())
    {
        // The authored half (T0142.16): a module overrode `unshaded()` to
        // true. Statically specialised, so this condition is a constant and
        // the lighting below is dead code the compiler removes -- the branch
        // reads as runtime and costs as compile-time.
        Out.Color = surfaceOut.BaseColor;
        Out.Color.rgb += surfaceOut.Emissive;
        if (g_Material.Basic.AlphaMode == PBR_ALPHA_MODE_BLEND)
        {
            Out.Color.rgb *= Out.Color.a;
        }
        return Out;
    }

    // -----------------------------------------------------------------------
    // **The lighting stage** (T0145, D30/D31) — the engine's own since this
    // ticket, and Diligent's BRDF still underneath it.
    //
    // What used to be here was `ApplyPunctualLight` in a loop and
    // `ResolveLighting` after it — theirs, called directly with their
    // `SurfaceShadingInfo`. It is now the mirror: this block converts the
    // surface stage's answers into `HpShadedSurface`, the engine's own type,
    // and hands it to `material.lighting()`. A material overriding nothing gets
    // the same arithmetic in the same order and the same pixels; one overriding
    // `light()` replaces the shading model for a single light without touching
    // attenuation, the cone or the loop.
    //
    // **The conversion is D31's cost, and it is register arithmetic rather than
    // memory traffic**: the same values are loaded either way, so nothing here
    // reads a byte the old path did not.
    // -----------------------------------------------------------------------
    HpShadedSurface shaded;
    shaded.Position = surfaceIn.WorldPos;
    shaded.View = surfaceIn.ViewDir;
    shaded.Normal = normalize(surfaceOut.Normal);
    // Clamped away from zero: a normal facing exactly edge-on makes the BRDF
    // divide by it, and the result is a ring of NaN pixels that survives into
    // the target as black or white speckle depending on the backend.
    shaded.NdotV = clamp(dot(shaded.Normal, surfaceIn.ViewDir), 1e-4, 1.0);
    shaded.BaseColor = surfaceOut.BaseColor.rgb;
    shaded.Metallic = surfaceOut.Metallic;
    shaded.Roughness = surfaceOut.Roughness;
    {
        // **Theirs, called once, and flattened into the mirror.** The metal
        // split and the 4% dielectric F0 are a formula worth not owning
        // (`GetSurfaceReflectanceMR`), and copying its four fields out is what
        // keeps `HpShadedSurface` free of a Diligent struct without
        // reimplementing one.
        SurfaceReflectanceInfo srf = GetSurfaceReflectanceMR(
            surfaceOut.BaseColor.rgb, surfaceOut.Metallic, surfaceOut.Roughness);
        shaded.DiffuseColor = srf.DiffuseColor;
        shaded.Reflectance0 = srf.Reflectance0;
        shaded.Reflectance90 = srf.Reflectance90;
    }
    shaded.Emissive = surfaceOut.Emissive;
    shaded.Occlusion = surfaceOut.Occlusion;

    // -----------------------------------------------------------------------
    // The extended layers (T0143): copies first, derivations after, each
    // derivation the mirror of its `Read*Properties` in `RenderPBR.psh` --
    // run *here*, after `surface()`, so an overridden channel flows into the
    // derived frame exactly as an authored one does. Every field is written
    // in every permutation (D31); the `#if`s guard only the derivations,
    // whose inputs (`g_Material`'s extended blocks) exist only with the
    // feature.
    // -----------------------------------------------------------------------
    shaded.Clearcoat = surfaceOut.Clearcoat;
    shaded.ClearcoatRoughness = surfaceOut.ClearcoatRoughness;
    shaded.ClearcoatNormal = surfaceOut.ClearcoatNormal;
#if ENABLE_CLEAR_COAT
    {
        // `ReadClearcoatLayerProperties` (:199-200): the coat's reflectance
        // from its roughness at the fixed IOR of 1.5. Flattened to the scalar
        // F0 -- the coat is achromatic and its R90 is 1.
        SurfaceReflectanceInfo ccSrf =
            GetSurfaceReflectanceClearCoat(shaded.ClearcoatRoughness, 1.5);
        shaded.ClearcoatReflectance0 = ccSrf.Reflectance0.x;
    }
#else
    shaded.ClearcoatReflectance0 = 0.0;
#endif
    shaded.SheenColor = surfaceOut.SheenColor;
    shaded.SheenRoughness = surfaceOut.SheenRoughness;
    shaded.Anisotropy = surfaceOut.Anisotropy;
#if ENABLE_ANISOTROPY
    {
        // ---- mirrored from `ReadAnisotropyProperties`, :268-293 -----------
        // The tangent comes from the mesh when it has one; the glTF extension
        // says meshes using anisotropy SHOULD carry TANGENT, and upstream's
        // fallback -- deliberately not screen-space derivatives, which are
        // discontinuous -- is the world X axis.
        float3 anisoBasisTangent = float3(1.0, 0.0, 0.0);
#if USE_VERTEX_TANGENTS
        anisoBasisTangent = surfaceIn.Tangent;
#endif
        float3 anisoBasisBitangent = cross(anisoBasisTangent, shaded.Normal);

        shaded.AnisotropyTangent =
            normalize(mul(float3(surfaceOut.AnisotropyDirection, 0.0),
                          MatrixFromRows(anisoBasisTangent, anisoBasisBitangent, shaded.Normal)));
        shaded.AnisotropyBitangent = cross(shaded.Normal, shaded.AnisotropyTangent);

        // Roughness along the bitangent is the material roughness; tangent
        // roughness grows with the strength.
        shaded.AlphaRoughnessT =
            lerp(shaded.Roughness * shaded.Roughness, 1.0, shaded.Anisotropy * shaded.Anisotropy);
        shaded.AlphaRoughnessB = shaded.Roughness * shaded.Roughness;
    }
#else
    shaded.AnisotropyTangent = float3(1.0, 0.0, 0.0);
    shaded.AnisotropyBitangent = float3(0.0, 1.0, 0.0);
    shaded.AlphaRoughnessT = 0.0;
    shaded.AlphaRoughnessB = 0.0;
#endif
    shaded.IridescenceThickness = surfaceOut.IridescenceThickness;
#if ENABLE_IRIDESCENCE
    {
        // ---- mirrored from `ReadIridescenceProperties` (:236-251) and the
        // F0 lerp in `GetSurfaceShadingInfo` (:339-343) --------------------
        // The film's Fresnel is evaluated against the *unbent* base F0, then
        // the base layer's `Reflectance0` is bent toward it by the factor.
        // A zero-thickness film kills the factor, upstream's own rule.
        float3 iridescenceFresnel =
            EvalIridescence(1.0, g_Material.Iridescence.IOR, shaded.NdotV,
                            shaded.IridescenceThickness, shaded.Reflectance0);
        float3 iridescenceF0 = SchlickToF0(shaded.NdotV, iridescenceFresnel,
                                           float3(1.0, 1.0, 1.0));
        float iridescenceFactor =
            shaded.IridescenceThickness == 0.0 ? 0.0 : surfaceOut.Iridescence;
        shaded.Iridescence = iridescenceFactor;
        shaded.Reflectance0 =
            lerp(shaded.Reflectance0, iridescenceF0,
                 float3(iridescenceFactor, iridescenceFactor, iridescenceFactor));
    }
#else
    shaded.Iridescence = surfaceOut.Iridescence;
#endif
    shaded.Transmission = surfaceOut.Transmission;
    shaded.Thickness = surfaceOut.Thickness;

    Out.Color.rgb = material.lighting(shaded, surfaceIn);
    Out.Color.a = surfaceOut.BaseColor.a;
#if ENABLE_TRANSMISSION
    // Mirrors `RenderPBR.psh:517`: a transmissive surface's coverage *is* its
    // opacity -- what does not scatter back shows the scene behind. Meaningful
    // on a blended material; ignored by an opaque target.
    Out.Color.a = 1.0 - shaded.Transmission;
#endif
#endif

    // Premultiplied alpha for blended surfaces, mirroring `RenderPBR.psh`.
    // `SurfacePipeline` sets `SrcBlend = ONE` for `ALPHA_MODE_BLEND`, which
    // *assumes* the shader has already multiplied -- one without the other
    // over-brightens every transparent surface by exactly its alpha.
    if (g_Material.Basic.AlphaMode == PBR_ALPHA_MODE_BLEND)
    {
        Out.Color.rgb *= Out.Color.a;
    }

    return Out;
}

/// Transforms one vertex through a material's vertex hook (T0146.1).
///
/// Generic and statically specialised, exactly like `evaluateSurface` -- the
/// same pattern one stage earlier, so the standard material's emitted code is
/// the transform chain and nothing else.
///
/// **The orchestration mirrors `RenderPBR.vsh` line for line**, and the one
/// thing it adds is the hook between "read the mesh" and "apply the
/// transform". The transform itself is still `GLTF_TransformVertex`, theirs.
void transformVertex<TMaterial : IHpMaterial>(inout TMaterial material, in VSInput VSIn,
                                              out VSOutput VSOut)
{
    // **Copied out of the constant buffer into a local, and their comment says
    // why**: "moving this block into GLTF_TransformVertex() causes huge
    // performance degradation on Vulkan because glslang/SPIRV-Tools are
    // apparently not able to eliminate the copy of g_Transforms structure".
    // Mirrored rather than rediscovered.
    PBRPrimitiveAttribs Primitive = g_Primitive;

    float4x4 Transform = Primitive.Transforms.NodeMatrix;

#if COMPUTE_MOTION_VECTORS
    float4x4 PrevTransform = Primitive.Transforms.PrevNodeMatrix;
#endif

#if MAX_JOINT_COUNT > 0 && USE_JOINTS
    int JointCount = Primitive.Transforms.JointCount;
    int FirstJoint = Primitive.Transforms.FirstJoint;
    if (JointCount > 0)
    {
        float4x4 SkinMat =
            VSIn.Weight0.x * HpGetJointMatrix(int(VSIn.Joint0.x), FirstJoint) +
            VSIn.Weight0.y * HpGetJointMatrix(int(VSIn.Joint0.y), FirstJoint) +
            VSIn.Weight0.z * HpGetJointMatrix(int(VSIn.Joint0.z), FirstJoint) +
            VSIn.Weight0.w * HpGetJointMatrix(int(VSIn.Joint0.w), FirstJoint);
        Transform = mul(SkinMat, Transform);
#       if USE_SKIN_PRE_TRANSFORM
        {
            Transform = mul(Primitive.Transforms.SkinPreTransform, Transform);
        }
#       endif

#       if COMPUTE_MOTION_VECTORS
        {
            float4x4 PrevSkinMat =
                VSIn.Weight0.x * HpGetJointMatrix(JointCount + int(VSIn.Joint0.x), FirstJoint) +
                VSIn.Weight0.y * HpGetJointMatrix(JointCount + int(VSIn.Joint0.y), FirstJoint) +
                VSIn.Weight0.z * HpGetJointMatrix(JointCount + int(VSIn.Joint0.z), FirstJoint) +
                VSIn.Weight0.w * HpGetJointMatrix(JointCount + int(VSIn.Joint0.w), FirstJoint);
            PrevTransform = mul(PrevSkinMat, PrevTransform);
#           if USE_SKIN_PRE_TRANSFORM
            {
                PrevTransform = mul(Primitive.Transforms.PrevSkinPreTransform, PrevTransform);
            }
#           endif
        }
#       endif
    }
#endif

#if USE_VERTEX_NORMALS
    float3 Normal = HpGetVertexNormal(VSIn.Normal);
#else
    // Their stand-in, kept: a mesh with no normals still needs one value in
    // the chain, and the pixel stage has its own documented fallback.
    float3 Normal = float3(0.0, 0.0, 1.0);
#endif

    float3 Pos = HpGetVertexPosition(VSIn);

    // -----------------------------------------------------------------------
    // **The hook** (T0146.2, D36). Object space in, object space out; the
    // engine transforms what comes back.
    //
    // `In.WorldPos` is the *rest* world position, and it is the same product
    // `GLTF_TransformVertex` computes below -- written the same way on purpose,
    // so a material that does not read it leaves the compiler an identical
    // expression to eliminate and a standard material's bytecode is unchanged.
    // -----------------------------------------------------------------------
    HpVertexInput vertexIn;
    vertexIn.Position = Pos;
    vertexIn.Normal = Normal;
#if USE_VERTEX_TANGENTS
    vertexIn.Tangent = VSIn.Tangent;
#else
    vertexIn.Tangent = float3(0.0, 0.0, 0.0);
#endif
#if USE_TEXCOORD0
    vertexIn.UV0 = VSIn.UV0;
#else
    vertexIn.UV0 = float2(0.0, 0.0);
#endif
#if USE_TEXCOORD1
    vertexIn.UV1 = VSIn.UV1;
#else
    vertexIn.UV1 = float2(0.0, 0.0);
#endif
#if USE_VERTEX_COLORS
    vertexIn.VertexColor = HpGetVertexColor(VSIn.Color);
#else
    // White, so a hook may multiply by it unconditionally -- the promise the
    // pixel side already makes for the same field.
    vertexIn.VertexColor = float4(1.0, 1.0, 1.0, 1.0);
#endif
    vertexIn.ObjectToWorld = Transform;
    {
        float4 restLocPos = mul(float4(Pos, 1.0), Transform);
        vertexIn.WorldPos = restLocPos.xyz / restLocPos.w;
    }
    vertexIn.CameraPos = g_Frame.Camera.f4Position.xyz;
    vertexIn.Time = g_Frame.Renderer.Time;

    // Pre-filled from the mesh, so an override writes only what it changes --
    // the same `inout` shape as `HpSurfaceOutput`, for the same reason.
    HpVertexOutput vertexOut;
    vertexOut.Position = vertexIn.Position;
    vertexOut.Normal = vertexIn.Normal;
    vertexOut.Tangent = vertexIn.Tangent;
    vertexOut.UV0 = vertexIn.UV0;
    vertexOut.UV1 = vertexIn.UV1;
    vertexOut.VertexColor = vertexIn.VertexColor;
    vertexOut.Custom0 = float4(0.0, 0.0, 0.0, 0.0);
    vertexOut.Custom1 = float4(0.0, 0.0, 0.0, 0.0);
    vertexOut.Custom2 = float4(0.0, 0.0, 0.0, 0.0);
    vertexOut.Custom3 = float4(0.0, 0.0, 0.0, 0.0);

    material.vertex(vertexIn, vertexOut);

    Pos = vertexOut.Position;
    Normal = vertexOut.Normal;

    GLTF_TransformedVertex TransformedVert = GLTF_TransformVertex(Pos, Normal, Transform);
    VSOut.ClipPos = mul(float4(TransformedVert.WorldPos, 1.0), g_Frame.Camera.mViewProj);

#if COMPUTE_MOTION_VECTORS
    // **The motion-vector seam, named for T0111** (T0146.6, and this is a line
    // and a comment rather than an implementation).
    //
    // The previous clip position is computed from the *displaced* position
    // through the previous transform, which is right for a displacement that
    // is constant in time (a morph pose, a per-instance offset) and **wrong
    // for one that animates** (wind, breathing): those want the hook re-run
    // with the previous frame's `Time`, which needs a previous-frame value in
    // the frame constants that nothing writes yet. T0111 owns that; the field
    // it needs is `PBRFrameAttribs`' second camera block, already written
    // every frame, plus a previous `Time`. Until then the flag is off --
    // `kFeatureMask` in `SceneRenderer.cpp` excludes
    // `PSO_FLAG_COMPUTE_MOTION_VECTORS` -- so this branch compiles in no
    // permutation the engine builds, and the inaccuracy above ships nowhere.
    GLTF_TransformedVertex PrevTransformedVert = GLTF_TransformVertex(Pos, Normal, PrevTransform);
    VSOut.PrevClipPos = mul(float4(PrevTransformedVert.WorldPos, 1.0),
                            g_Frame.PrevCamera.mViewProj);
#endif

    VSOut.WorldPos = TransformedVert.WorldPos;

#if USE_VERTEX_COLORS
    VSOut.Color = vertexOut.VertexColor;
#endif

#if USE_VERTEX_NORMALS
    VSOut.Normal = TransformedVert.Normal;
#endif

#if USE_TEXCOORD0
    VSOut.UV0 = vertexOut.UV0;
#endif

#if USE_TEXCOORD1
    VSOut.UV1 = vertexOut.UV1;
#endif

#if USE_VERTEX_TANGENTS
    VSOut.Tangent = normalize(mul(vertexOut.Tangent,
                                  float3x3(Transform[0].xyz, Transform[1].xyz,
                                           Transform[2].xyz)));
#endif

#if HP_CUSTOM_MATERIAL
    // The four custom interpolators (T0146.4), appended to the generated
    // `VSOutput` by `SurfacePipeline::build` for exactly this permutation.
    VSOut.HpCustom0 = vertexOut.Custom0;
    VSOut.HpCustom1 = vertexOut.Custom1;
    VSOut.HpCustom2 = vertexOut.Custom2;
    VSOut.HpCustom3 = vertexOut.Custom3;
#endif
}

/// The vertex entry point: the material through the generic transform.
///
/// The engine's, since T0146 — `RenderPBR.vsh` was here until then, and the
/// only reason to replace it was the hook `transformVertex` above now carries.
void vsMain(in VSInput VSIn, out VSOutput VSOut)
{
#if HP_CUSTOM_MATERIAL
    HpMaterial material = {};
#else
    HpStandardMaterial material = {};
#endif
    transformVertex(material, VSIn, VSOut);
}

/// The pixel entry point: the standard material through the generic evaluator.
///
/// One line of intent -- everything above is reusable machinery, and this is
/// the only place a concrete material is chosen. Slang specialises
/// `evaluateSurface` for `HpStandardMaterial` statically, so the emitted code
/// is the same direct calls the pre-interface shader made.
///
/// **Named `psMain` since T0146.5**, because the vertex entry point lives in
/// this file now and `main` cannot name both.
PSOutput psMain(in VSOutput VSOut,
                in bool IsFrontFace : SV_IsFrontFace)
{
#if HP_CUSTOM_MATERIAL
    // The module's conformance (T0142.15). The name is the contract: a
    // custom material module defines `HpMaterial`, and this is the one line
    // that instantiates it -- statically specialised like the standard one,
    // so an override costs a direct call, not a dispatch.
    //
    // `= {}` because the hooks are `[mutating]` (T0159.2) and the material is
    // passed `inout`: a stateful module's members start at zero rather than
    // undefined, and slang's uninitialized-variable diagnostic stays out of
    // the compile log it would otherwise enter for every stateful module.
    //
    // **A second, independent value from the one `vsMain` constructed** -- two
    // stages, two invocations, no shared members. `HpMaterial.slang` says so
    // in the vertex hook's own documentation, because the `[mutating]` hooks
    // make it look otherwise.
    HpMaterial material = {};
#else
    HpStandardMaterial material = {};
#endif
    return evaluateSurface(material, VSOut, IsFrontFace);
}
