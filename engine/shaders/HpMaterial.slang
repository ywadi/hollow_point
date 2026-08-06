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
//
// ---
//
// ## A module's own parameters and resources (T0160, T0161, D35)
//
// **Your module declares its own textures under its own names, at whatever
// count it wants**, as ordinary globals — no registration, no attributes, no
// engine identifiers:
//
//     Texture2DArray detailAlbedo;
//     Texture2DArray puddleMask;
//
//     cbuffer HpMaterialParams
//     {
//         [HpRange(0.0, 1.0)]
//         [HpTooltip("How wet the surface reads.")]
//         float wetness;
//
//         [HpColor]
//         float3 tint;
//     }
//
// The engine reflects the compiled module and binds what it finds; the
// `.hpmat` beside it supplies values and textures **by your names**:
//
//     params:
//       - name: wetness
//         value: 0.75
//     textures:
//       - name: detailAlbedo
//         texture: 1570000000000031
//
// The conventions, each of which the engine enforces loudly rather than
// trusting:
//
// - **`HpMaterialParams` is the one reserved name.** The parameter *block* is
//   called that so a `.hpmat`'s values have one place to go; every field name
//   inside it is yours. A second constant buffer under another name is
//   refused, by name, at pipeline build. The block is capped at **256 bytes**
//   and carries `float`/`float2`/`float3`/`float4`/`int`/`bool`; anything
//   else is reported by name and left at what the shader initialises it to.
// - **Declare textures as `Texture2DArray`.** Every texture this engine binds
//   is a one-slice 2D array; a plain `Texture2D` is refused by name at
//   pipeline build, because past that point the mismatch would be a raw
//   Vulkan validation error in a cooked game.
// - **Sample through the engine's sampler palette** — `HpSamplerLinearWrap`,
//   `HpSamplerLinearClamp`, `HpSamplerPointWrap`, `HpSamplerPointClamp`,
//   `HpSamplerAnisoWrap`, `HpSamplerAnisoClamp`, declared below. Sampler
//   *state* is the engine's vocabulary, and this is the one deliberate limit
//   (D35): the choice of sampler travels inside your compiled code, so a
//   cooked build carries no sampler metadata at all. Declaring your own
//   `SamplerState` is refused by name, with the palette in the log line.
// - **Declare unconditionally, at module top level.** A declaration inside a
//   permutation `#if` makes "what does this module declare" a question with
//   one answer per macro set, and neither a `.hpmat` nor an inspector row can
//   be permutation-dependent. An unused declaration is stripped from the
//   compiled code and costs nothing.
// - **Values are data, not permutations.** Changing a `.hpmat` value rebuilds
//   no pipeline and invalidates no cook.
// - **Two namespaces, one line between them.** Resources you *declare* are
//   yours, fed by the `.hpmat`. Resources the *engine* feeds — the frame,
//   the lights, the height map, and later the screen and shadow resources
//   (T0147, T0086) — are the engine's names, reached as documented above.
//   A texture the `.hpmat` does not name samples **white**; a GUID that does
//   not resolve samples the missing-asset checkerboard.
// - **Buffers are declared the same way but no material stage can feed one
//   yet** — a structured buffer in a surface module is refused by name until
//   the stages whose point they are (T0147 screen resources, T0150 compute)
//   land. The mechanism is already stage-neutral; the data paths are not
//   built.
//
// `HpTexture0` … `HpTexture3`, T0160's engine-named slots, still compile and
// still bind from a `.hpmat` that names them — see their declarations below —
// but they are **deprecated**: name your textures yourself.

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
// ## A module's own parameters and resources (T0160, T0161)
//
// The authoring guide for this section is the file preamble above — declare
// textures under your own names, put parameters in `cbuffer HpMaterialParams`,
// sample through the palette declared below, and let the `.hpmat` bind
// everything by name. What follows here is the vocabulary itself: the hint
// attributes, the sampler palette, and the deprecated T0160 slots.
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

// --- The sampler palette (T0161.2, D35) ------------------------------------
//
// Immutable in the engine's resource signature, so a module names one and the
// state — filtering, wrap, anisotropy — is the engine's, fixed at signature
// creation. That is the one deliberate limit of the resource model: sampler
// state is the single thing SPIR-V cannot carry, and an engine-owned palette
// means a cooked build never has to carry it either. Declaring your own
// `SamplerState` is refused by name at pipeline build, with this palette in
// the log line.

/// Bilinear filtering with mip-mapping, coordinates wrapping — the default
/// choice for anything that tiles across a surface.
SamplerState HpSamplerLinearWrap;

/// Bilinear filtering, coordinates clamped to the edge texel. What a LUT, a
/// ramp or an atlased texture wants — wrapping one bleeds the far edge in.
SamplerState HpSamplerLinearClamp;

/// Nearest-texel sampling, wrapping. Pixel art, ID maps, and any texture
/// whose texels are data rather than an image to be filtered.
SamplerState HpSamplerPointWrap;

/// Nearest-texel sampling, clamped. The data-texture counterpart of
/// `HpSamplerLinearClamp`.
SamplerState HpSamplerPointClamp;

/// 8x anisotropic filtering, wrapping. For surface detail read at grazing
/// angles — floors, terrain, anything large and walked across. The level is
/// the engine's (D35); an author-chosen level is the recorded escape, waiting
/// on a real request.
SamplerState HpSamplerAnisoWrap;

/// 8x anisotropic filtering, clamped. See `HpSamplerAnisoWrap`.
SamplerState HpSamplerAnisoClamp;

// --- T0160's engine-named slots, deprecated (T0161.6) -----------------------
//
// Kept compiling so a shipped v2 `.hpmat` and the module beside it keep
// rendering identically: a module that samples `HpTexture0` still gets it,
// bound by a `textures:` entry naming `HpTexture0` — through exactly the same
// reflection walk as an author-named texture, because that is all these are
// now: author-visible names that happen to be spelled the engine's way.

/// Deprecated (T0161): declare your own `Texture2DArray yourName;` instead.
/// Binds from a `.hpmat` `textures:` entry naming `HpTexture0`, as T0160
/// shipped it.
Texture2DArray HpTexture0;

/// Deprecated (T0161); see `HpTexture0`.
Texture2DArray HpTexture1;

/// Deprecated (T0161); see `HpTexture0`.
Texture2DArray HpTexture2;

/// Deprecated (T0161); see `HpTexture0`.
Texture2DArray HpTexture3;

/// Deprecated (T0161): the slot samplers are the palette's `LinearWrap` now,
/// which is bit-identical state to what the slots always had. Sample through
/// the palette by name in new code.
#define HpTexture0_sampler HpSamplerLinearWrap

/// Deprecated (T0161); see `HpTexture0_sampler`.
#define HpTexture1_sampler HpSamplerLinearWrap

/// Deprecated (T0161); see `HpTexture0_sampler`.
#define HpTexture2_sampler HpSamplerLinearWrap

/// Deprecated (T0161); see `HpTexture0_sampler`.
#define HpTexture3_sampler HpSamplerLinearWrap

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
