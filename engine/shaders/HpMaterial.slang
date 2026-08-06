// hp-shader-doc: public
//
// The contract a game's shader compiles against (T0141.6, D27; Slang since
// T0142.13, D28).
//
// **A game's module includes nothing and may reach anything** (D27, as
// amended 2026-08-06). The module is compiled *inside* `HpSurface.slang`, so
// everything that file can see is in scope for an override: this contract,
// DiligentFX's getters (`GetBaseColor`, `PerturbNormal`, ...), the frame,
// primitive and material constant buffers (`g_Frame`, `g_Primitive`,
// `g_Material` -- including `g_Frame.Lights[]` under `PBR_MAX_LIGHTS`), the
// permutation macros, and the engine's own helpers (`HpParallaxMarch`,
// `g_HeightMap`). **That is deliberate, with no warning and no version
// check**: this engine is permanently on Diligent (D24, D29), and a shipped
// game never meets a newer engine under D12's lockstep, so an upstream rename
// is development-time friction caught by a loud compile failure -- magenta
// plus one logged error -- not breakage in the field.
//
// The trade to understand before reaching past this file: `HpSurfaceInput`
// below is a **promise** that holds across engine updates; everything else is
// whatever DiligentFX and the engine currently ship, and it may be renamed by
// an upgrade. Reaching for an internal is also a *signal* -- it means this
// contract is missing something, and the capability matrix
// (`13-shader-capability-matrix.md`) is where that gap should be recorded so
// it becomes a widening rather than a workaround every game repeats.
//
// ## The authoring shape, stated once and correctly
//
// A game writes a **module that implements `IHpMaterial`** and overrides only
// what it wants (D28, T0142.15). `override` is mandatory, so nothing the engine
// does is replaced by accident:
//
//     struct HpMaterial : IHpMaterial
//     {
//         override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
//         {
//             return float4(1.0, 0.0, 0.0, 1.0);
//         }
//     }
//
// **The module includes nothing.** The engine includes *it*, into
// `HpSurface.slang` after this contract and after DiligentFX's getters, through
// a generated one-line include — which is what lets an override call
// `GetBaseColor` without knowing where it comes from, and what lets the content
// hasher recurse into the module's own text (T0142.7).
//
// ### What this file used to say, and why it stopped being true
//
// It documented Godot's shape literally — *"a developer writes one function"*,
// `void HpSurface(in HpSurfaceInput In, inout HpSurfaceOutput Out)`, forward
// declared at the bottom of this file so that a shader which forgot to define
// it failed at link with a clear missing-symbol error.
//
// **A game cannot define that function**, and has not been able to since
// T0142.15 put the module inside `HpSurface.slang`: the engine's own definition
// is already in scope when the module is included, so a second body is
// `error[E30201]: function 'HpSurface' already has a body` — measured on the
// pinned `slangc 2026.14.1`, not reasoned about after the fact.
//
// D27's function-style hook is not gone; it moved. It is
// `IHpMaterial.surface()`, whose default calls the engine's own `HpSurface` —
// so a material wanting the whole-output hook overrides *that*, and the free
// function is what it now honestly is: the engine's internal default, private
// to `HpSurface.slang`. The declaration that used to sit at the bottom of this
// file went with the claim it supported.
//
// ---
//
// ## `HpSurfaceInput` is a promise
//
// **Adding a field later is free; removing one breaks every game that used it.**
// So this list is decided deliberately rather than grown by accident, and it
// holds exactly what the engine genuinely computes today.
//
// **Nothing is exposed before the system behind it exists.** A `Visibility`
// field that returns 1.0 because T0093 has not been built is worse than no
// field: a developer writes a shader against it, their effect silently does
// nothing, and there is no error anywhere. That is the mistake
// `Camera::cullingMask` spent three tickets being, arriving in a *public*
// contract where the cost is every game that trusted it.
//
// So each system's own ticket adds its own field when it lands, which is one
// line and additive by construction:
//
// | Field | Arrives with |
// |---|---|
// | `ShadowFactor` | T0086 — shadows |
// | `Visibility` | T0093 — vision-based visibility |
// | `AmbientOcclusionIBL` | T0087 — environment lighting |
// | `Velocity` | T0111 — motion vectors |
//
// `Time` sat in that table — the field existed in `PBRFrameAttribs` and was
// never written — until T0159.5 connected the clock and added it below.
//
// ## The struct is a contract, not a wire format
//
// A field here does **not** imply an interpolator. Vertex-to-pixel interpolators
// are a limited hardware resource, and paying for every field on every draw
// whether a shader reads it or not would land hardest on exactly the dense
// geometry that can least afford it. `ViewDir` is computed in the pixel shader
// from the camera position and the world position rather than interpolated; more
// fields will follow that pattern as they are added.

#ifndef _HP_MATERIAL_SLANG_
#define _HP_MATERIAL_SLANG_

// ## Unshaded
//
// **A game material can opt out of lighting entirely**, which is Godot's
// `render_mode unshaded;` and is requested often enough to be part of the
// contract rather than a workaround. `Out.BaseColor` is then written straight
// to the target with no BRDF, no light loop and no IBL — UI, sprites,
// holograms, debug overlays and most VFX want exactly this.
//
// **There are two doors to it and they are not interchangeable**, which is
// worth knowing before reaching for either:
//
//   * `IHpMaterial.unshaded()`, overridden to `true` by a module — the
//     *authored* half (T0142.16). Exact in effect, and **not folded at the
//     SPIR-V level**: measured at 19336 bytes against the shaded 19348, at
//     optimisation level none and default alike, so the lighting is still
//     emitted and the branch is real. T0151's link-time constants are the
//     mechanism that would make the elimination provable.
//   * `Material::unlit` in a `.hpmat` (T0060.1) — the *data* half, which rides
//     the `HP_UNSHADED` permutation bit below, so the lighting is excluded at
//     preprocess time and genuinely not paid for.
//
// The macro is that permutation bit. `SurfacePipeline::build` always defines it
// from the PSO key; the default here exists so this contract still reads on its
// own.
#ifndef HP_UNSHADED
#   define HP_UNSHADED 0
#endif

// ---------------------------------------------------------------------------
// ## A module's own parameters and textures (T0160)
//
// **This is the half of the material model that is not the engine's.** Every
// other parameter a surface has is a field of `hp::Material` — named by the
// engine, meaning the same thing everywhere — and that vocabulary cannot grow
// to fit a technique the engine has never heard of. Before this existed, the
// first material shader written in this repository hard-coded its one knob as a
// compile-time literal and said so in its own comment, because there was
// nowhere to put it.
//
// A module declares a **reserved constant buffer** and reads it like any other:
//
//     cbuffer HpMaterialParams
//     {
//         [HpRange(0.0, 1.0)]
//         [HpTooltip("Where the height field sits relative to the polygon.")]
//         float referencePlane;
//
//         [HpColor]
//         float3 tint;
//     }
//
// and the `.hpmat` beside it carries the values, by name:
//
//     params:
//       - name: referencePlane
//         value: 0.75
//
// **The buffer's name is the engine's and the field names are yours.** A
// pipeline resource signature is built once, before any module exists, and
// Diligent matches a shader's resources to it by name — so the *buffer* has to
// be called something the engine already knows. Fields are not resources, so
// they cost the signature nothing and a module names and orders them freely.
//
// ### Three rules, each of them a measurement
//
// **Declare the block unconditionally, at module top level.** A declaration
// inside a permutation `#if` makes "what does this module declare" a question
// with one answer per macro set, and neither a `.hpmat` nor an inspector row
// can be permutation-dependent. `rock_pom.slang` is the worked example: its
// whole body is inside `#if HP_USE_HEIGHT_MAP && USE_TEXCOORD0` and its
// parameter block sits outside it.
//
// **Values are data, not permutations.** Changing one rebuilds no pipeline and
// invalidates no cook. Module identity already keys the pipeline cache, so
// parameters add no permutation axis at all — which is the reason to prefer a
// parameter over a `#define` whenever the choice exists.
//
// **The engine writes only what it can carry**: `float`, `float2`, `float3`,
// `float4`, `int` and `bool`. A matrix, an array or a nested struct in the
// block is reported by name at compile time and left at whatever the shader
// initialises it to — never silently mis-sized. The whole block is capped at
// **256 bytes**, and a module that overruns it is refused by name rather than
// truncated.
//
// ### Textures
//
// Four slots, named by the engine for the same reason the buffer is:
// `HpTexture0` … `HpTexture3`, each with an immutable `HpTexture0_sampler` and
// so on. They are declared below, so a module **uses them without declaring
// anything**, and the `.hpmat` binds them by the same names:
//
//     textures:
//       - name: HpTexture0
//         texture: 1570000000000031
//
// An unused slot is stripped from the compiled shader, so a module pays only
// for the slots it samples. An unbound slot samples **white**, matching what an
// unset fixed texture slot does; a GUID that does not resolve samples the
// missing-asset checkerboard, matching what a broken one does.
//
// **The slot names are the engine's and the author's own names are not
// reachable today**, which is a real cost recorded rather than hidden: naming
// them would need either a second descriptor set per module or a rename pass
// that has to know the names *before* the compile that discovers them. The
// capability matrix carries it as the named widening.
// ---------------------------------------------------------------------------

/// Bounds a numeric parameter, for the inspector.
///
/// `[HpRange(0.0, 1.0)]` — Godot's `hint_range`, as a Slang attribute rather
/// than as syntax the engine would have to invent. Equal bounds mean unbounded.
/// **Presentation only**: nothing clamps the value that reaches the shader, so
/// a hand-edited `.hpmat` outside the range still renders what it says.
[__AttributeUsage(_AttributeTargets.Var)]
struct HpRangeAttribute
{
    /// The lowest value the inspector offers.
    float lo;

    /// The highest. Equal to `lo` means unbounded.
    float hi;
};

/// Marks a `float3`/`float4` parameter as a colour, so an inspector offers a
/// swatch instead of three number boxes. The bytes written are identical.
[__AttributeUsage(_AttributeTargets.Var)]
struct HpColorAttribute
{
};

/// Text shown on hover in the inspector.
///
/// The same slot `hp::PropertyMeta::tooltip` fills for a reflected C++ field —
/// one description, two reflections, which is the unification D28 anticipated.
[__AttributeUsage(_AttributeTargets.Var)]
struct HpTooltipAttribute
{
    /// The text itself.
    String text;
};

// The four module texture slots, declared here so a module uses one without
// declaring anything — which is the same rule the rest of this contract obeys.
//
// `Texture2DArray` because every texture this engine binds is a one-slice
// array, and the samplers are **immutable in the signature**, so a module never
// binds one and cannot get the filtering wrong.
//
// **Declared for every pipeline and named by almost none of them.** Slang
// strips a resource nothing samples, so the standard material's bytecode is
// unchanged by their presence — the same superset pattern `g_HeightMap` uses on
// the signature side.
Texture2DArray HpTexture0;
SamplerState   HpTexture0_sampler;
Texture2DArray HpTexture1;
SamplerState   HpTexture1_sampler;
Texture2DArray HpTexture2;
SamplerState   HpTexture2_sampler;
Texture2DArray HpTexture3;
SamplerState   HpTexture3_sampler;

/// What the engine hands a surface function.
///
/// Read-only. Everything here is in **world space** unless the field says
/// otherwise, matching the engine's convention everywhere else (`hp/Math.hpp`).
struct HpSurfaceInput
{
    /// The first texture coordinate set, after the material's UV0 transform.
    float2 UV0;

    /// The second texture coordinate set, after the material's UV1 transform.
    /// **Zero when the mesh carries only one set** — not an error, and the same
    /// fallback `SelectUV` makes on the standard path.
    float2 UV1;

    /// `UV0` as the mesh delivered it, **before** the coordinate hook ran
    /// (T0159.3). After a parallax march `UV0` is the *displaced* coordinate,
    /// whose screen-space derivatives are discontinuous — a frame built from
    /// them collapses (measured: ~0.006 UV of reach against the 0.14 needed,
    /// T0158). This is the smooth coordinate whose `ddx`/`ddy` stay valid, for
    /// any technique that needs continuous derivatives after displacement.
    /// Identical to `UV0` when the hook displaced nothing.
    float2 UV0Base;

    /// World-space position of this fragment. The basis for triplanar
    /// projection (T0141.8) and for anything that varies with where a surface
    /// is rather than how it is unwrapped.
    float3 WorldPos;

    /// World-space geometric normal, **before** any normal map is applied.
    /// A surface function that writes `Out.Normal` replaces it; one that does
    /// not gets this.
    float3 Normal;

    /// World-space tangent, unit length, with `w` reserved for the bitangent's
    /// handedness. **Zero when the mesh carries no tangents** — real since
    /// T0159.4, which ended two years of this field being assigned zero
    /// unconditionally. A surface function must tolerate the zero case rather
    /// than producing a black surface: the engine cannot invent tangents a
    /// mesh does not carry, and screen-space derivatives (what `HpParallaxUv`
    /// and DiligentFX's `PerturbNormal` build their frames from) are the
    /// fallback that needs no vertex data.
    ///
    /// **`w` is always +1, by decision rather than measurement** (T0159.4):
    /// glTF authors handedness as ±1 in the tangent's fourth component, and
    /// Diligent's vertex path carries `float3` — the loader drops `w` before
    /// the engine ever sees it. So `bitangent = cross(Normal, Tangent.xyz)`
    /// here, and a mesh whose UVs are mirrored (authored `w = -1`) will have
    /// its bitangent flipped. If that ever matters in practice, the fix is
    /// widening the wire format upstream, not guessing here — recorded so the
    /// symptom (normal-mapped lighting inverted on mirrored islands, from
    /// vertex tangents only) finds its cause.
    float4 Tangent;

    /// Normalised direction from the fragment **to the camera**. Computed here
    /// rather than interpolated; see the header comment.
    float3 ViewDir;

    /// Per-vertex colour, interpolated. White when the mesh carries none, so a
    /// shader may multiply by it unconditionally.
    float4 VertexColor;

    /// Position in the render target, in pixels, with depth in `z` and `1/w`
    /// in `w` — the raw `SV_POSITION` a pixel shader receives.
    float4 ScreenPos;

    /// World-space camera position. Present because a surface function that
    /// needs distance-based behaviour should not have to reconstruct it.
    float3 CameraPos;

    /// Seconds since the application's clock started, scaled by its time
    /// scale — the value the frame loop hands `SceneRenderer` (T0159.5).
    /// Frame-wide and identical for every fragment; what scrolling UVs,
    /// flowmaps and pulsing emissive animate on. **Zero in a caller that
    /// passes no time** (a bare `render()` call), never undefined.
    float Time;
};

/// What a surface function fills in.
///
/// **Every field is initialised from the material before the surface function
/// runs**, so writing nothing produces the standard material and writing one
/// field changes exactly that field. That is what makes a three-line shader
/// useful, and it is why this is `inout` rather than `out`.
struct HpSurfaceOutput
{
    /// Linear RGB and alpha. Alpha is compared against the material's cutoff
    /// when its alpha mode is `Mask`, and blended when it is `Blend`.
    float4 BaseColor;

    /// World-space normal. Pre-filled with the geometric normal, and with the
    /// material's normal map applied when it has one — so a shader that wants
    /// the standard normal mapping gets it by leaving this alone.
    float3 Normal;

    /// 0 = dielectric, 1 = metal.
    float Metallic;

    /// 0 = mirror, 1 = fully rough.
    float Roughness;

    /// Linear RGB added on top of shading. **Not** clamped to 1: values above it
    /// are meaningful once T0096's tonemapping exists.
    float3 Emissive;

    /// How much ambient light reaches this fragment, 0 to 1.
    float Occlusion;
};

#endif // _HP_MATERIAL_SLANG_
