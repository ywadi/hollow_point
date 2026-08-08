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
// ## Two stages, one module (T0146)
//
// A module implements `IHpMaterial`, and since T0146 that interface spans the
// **vertex** stage as well as the pixel one: `vertex()` displaces geometry,
// everything else shades it. Both entry points of the engine's shader compile
// the same module in the same compile, so one file is the whole material.
//
// The vertex hook's own section is further down, beside `HpVertexInput`; the
// two facts to carry into it are that it works in **object space** (D36) and
// that its members do **not** reach the pixel stage — `HpVertexOutput::Custom0`
// … `Custom3` are the channel that does.
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
// **T0147 added `ScreenUV` and did *not* add `Visibility`, and the restraint is
// the point.** A game can build the fog-of-war dim today, out of a texture its
// own layer renders and feeds by name (see the game-fed section below) — that
// is a *mechanism*, and it is honest about what produces the bytes. A
// `Visibility` field would be the engine promising a number no engine system
// computes, which is exactly the failure this table exists to prevent. It
// arrives when T0093's system does, and then it is one line.
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
//   yours, fed by the `.hpmat` or by the game (see below). Resources the
//   *engine* feeds — the frame, the lights, the height map, the screen
//   intermediates (T0147) and later the shadow resources (T0086) — are the
//   engine's names, reached as documented above. A texture neither the
//   `.hpmat` nor the game names samples **white**; a GUID that does not
//   resolve samples the missing-asset checkerboard.
// - **Buffers are declared the same way but no material stage can feed one
//   yet** — a structured buffer in a surface module is refused by name until
//   the stage whose point they are (T0150 compute) lands. The mechanism is
//   already stage-neutral; that data path is not built.
//
// ### A texture the *game* produced, not the `.hpmat` (T0147.4, T0094)
//
// **Same declaration, second source.** A `.hpmat` binds a texture *asset*; a
// game layer binds a texture it *rendered* — a fog-of-war visibility field, a
// flow simulation, a minimap — through the engine, by the same name your
// module declares:
//
//     // gameplay C++, once the layer has drawn into its own target
//     layer.setGameTexture("visibility", myTarget);
//
//     // the module, unchanged from any other declared texture
//     Texture2DArray visibility;
//     ...
//     float seen = visibility.SampleLevel(HpSamplerLinearClamp,
//                                         float3(In.ScreenUV, 0.0), 0.0).r;
//
// The resolution order is **`.hpmat` first, then the game feed, then white**:
// a material that names an asset for that slot keeps it, because authored
// content is the more specific statement. Nothing is refused and nothing
// warns — a name no one feeds is white, exactly as it was before, so a module
// can declare a slot the game has not started feeding yet.
//
// ---
//
// ## Screen resources: what the frame looks like behind this fragment (T0147)
//
// **Two engine-fed textures, and one rule that governs both.** They are what
// refraction, glass, heat haze, frosted glass, soft particles and fog-of-war
// are made of, and Godot's `hint_screen_texture` / `hint_depth_texture` are
// the same thing under other names.
//
//     float4 behind = HpSceneColour(In.ScreenUV + offset);  // linear RGB
//     float  sceneZ = HpSceneViewDepth(In.ScreenUV);        // metres
//     float  fade   = saturate((sceneZ - HpViewDepth(In.ScreenPos.z)) / 0.5);
//
// | Helper | What it returns |
// |---|---|
// | `HpSceneColour(uv)` | the scene's colour, **linear**, pre-tonemap (T0096) |
// | `HpSceneDepth(uv)` | raw device depth — near is **1**, far is **0** (reverse-Z, T0130) |
// | `HpSceneViewDepth(uv)` | that depth as a distance along the view axis, in metres |
// | `HpViewDepth(z)` | the same conversion for any device depth, including your own `In.ScreenPos.z` |
//
// The textures themselves are `g_SceneColour` and `g_SceneDepth`, sampled
// through the palette (`HpSamplerLinearClamp`, `HpSamplerPointClamp`); the
// helpers exist so that a shader author never has to remember which sampler
// or which depth convention.
//
// ### The rule: **only a material with `alphaMode: Blend` may read them**
//
// **And that is enforced, not documented.** These are *snapshots*, copied out
// of the frame between the opaque pass and the blend pass — so an opaque
// material reading them would read the previous frame, or an uninitialised
// texture on the first one, which is the exact class of bug that works on one
// driver and not the next. A module that samples either from a pipeline whose
// alpha mode is not `Blend` **fails to build**: the engine logs one line
// naming the module and the rule, and the surface renders the
// missing-material checkerboard.
//
// ### What they contain, stated precisely
//
// The frame **as of the end of the opaque pass**: every `Opaque` and `Mask`
// surface, and the clear colour where nothing was drawn. In particular:
//
// - **No blended geometry at all** — including blended surfaces drawn *before*
//   yours. A pane of glass behind another pane does not appear in the second
//   one's refraction. That is the limitation every engine ships (Godot's
//   screen texture has it too); it is documented, not solved, because solving
//   it means a snapshot per transparent draw.
// - **`HpSceneDepth` is 0 where nothing was drawn**, because reverse-Z clears
//   the far plane to 0. `HpSceneViewDepth` therefore returns the far plane's
//   distance there, which is usually what a fade wants and is occasionally a
//   surprise; mask it with `HpSceneDepth(uv) > 0.0` when it matters.
// - **The snapshot costs nothing when nothing reads it.** The copy is issued
//   only when the frame has blended geometry *and* one of its modules reads
//   the screen, so a scene of opaque materials never pays for it.
//
// ### What is deliberately **not** offered
//
// **There is no normal-roughness read, and there will not be one until
// something produces it.** Godot's `hint_normal_roughness_texture` is
// Forward+-only there for the same reason it is absent here: it is a *deferred*
// resource, and this engine is forward-only (D24) with no G-buffer and no depth
// prepass. Synthesising one would mean adding a pass that writes normals and
// roughness for every opaque surface — real bandwidth, paid by every game, for
// a technique none has asked for. **A shader already has its own** surface
// normal and roughness (`HpSurfaceOutput`, and `In.Normal` before the map);
// what a screen-space read adds is the *other* surface's, which is what a
// prepass would have to produce. If one is ever wanted, it arrives with the
// pass that writes it, and it gets its own row in the capability matrix first.
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

// ---------------------------------------------------------------------------
// ## The vertex stage (T0146, D36)
//
// **The engine owns the vertex `main` as well as the pixel one**, and
// `IHpMaterial.vertex()` is the hook. It is Godot's `vertex()`: wind sway,
// foliage bend, billboarding, breathing, jelly, flag flap, morph offsets — the
// workhorse of stylised motion, and until T0146 there was no equivalent here at
// all.
//
// ### The space, decided once (D36)
//
// **Object space in, object space out.** The hook receives the mesh's own
// position, normal and tangent exactly as the vertex buffer holds them, and
// returns positions in that same space; the engine then applies the node
// transform (skinned when the mesh is skinned), the view-projection, and
// everything downstream. There is **no mode switch** — Godot's
// `world_vertex_coords` and `skip_vertex_transform` were both considered and
// rejected, and D36 records why in full. The short form:
//
//   * a world-space *input* is what `world_vertex_coords` is actually wanted
//     for, so `HpVertexInput::WorldPos` and `ObjectToWorld` are handed over
//     read-only and no mode is needed to reach them;
//   * handing the projection to the shader (`skip_vertex_transform`) breaks
//     everything downstream that has to reason about the transform the engine
//     applied — the winding convention (D33), motion vectors (T0111), culling
//     and the shadow pass — for a case nothing here has asked for.
//
// ### What the hook must not do
//
// **Do not invert a triangle.** D33 is binding: `cross(v1-v0, v2-v0)` points
// outward, and the engine rasterises with `FrontCounterClockwise = true`
// (`hp::kFrontFaceCounterClockwise`, T0165). Displacement composes with that
// as long as it does not turn a face inside out; pushing a vertex past its
// neighbours does, and the symptom is a surface that culls away rather than a
// shading error.
//
// ### State does **not** cross the stage boundary
//
// The vertex and pixel stages each construct their own `HpMaterial`, because
// they are separate shader invocations — a member written in `vertex()` is
// **not** visible in `surfaceCoordinates()` or `surface()`, however much the
// cross-hook state of T0159 makes it look like it should be. The channel
// between the stages is `HpVertexOutput::Custom0` … `Custom3`, which are real
// interpolators and arrive as `HpSurfaceInput::Custom0` … `Custom3`.
// ---------------------------------------------------------------------------

/// What the engine hands the vertex hook (T0146, D36).
///
/// Read-only. **Everything here is in object space unless the field says
/// otherwise** — the opposite default from `HpSurfaceInput`, and it says so on
/// every field because that is exactly the confusion D36 exists to settle.
struct HpVertexInput
{
    /// The vertex's position, in **object space**, as the mesh holds it.
    float3 Position;

    /// The vertex's normal, in **object space**. `(0, 0, 1)` when the mesh
    /// carries no normals — the same stand-in the engine's own transform uses,
    /// never undefined.
    float3 Normal;

    /// The vertex's tangent, in **object space**. Zero when the mesh carries
    /// none.
    ///
    /// `float3` rather than `float4`: glTF's handedness `w` does not reach this
    /// engine. **Not because Diligent's loader cannot carry it** — that claim
    /// was here and was wrong (T0166.4). `ModelCreateInfo::VertexAttributes` is
    /// settable and the three-component tangent is the *default* this engine
    /// takes by passing null. What actually stops it is one layer in:
    /// `PBR_Renderer::GetVSInputStructAndLayout` generates the `VSInput` struct
    /// from a `constexpr` tangent attribute hardcoded to three components
    /// (`PBR_Renderer.cpp:1660`), and checks the input layout matches it.
    /// **T0168 owns closing it**; see `HpSurfaceInput::Tangent` for what a
    /// shader should do meanwhile.
    float3 Tangent;

    /// The first texture coordinate set, as the mesh holds it. Zero when the
    /// mesh carries none.
    float2 UV0;

    /// The second texture coordinate set. Zero when the mesh carries none.
    float2 UV1;

    /// Per-vertex colour. White when the mesh carries none, so a hook may
    /// multiply by it unconditionally — the same promise the pixel side makes.
    float4 VertexColor;

    /// **World space**: where this vertex sits *before* the hook displaces it.
    ///
    /// This is what Godot's `world_vertex_coords` is really wanted for — a
    /// wind field, a distance fade or a per-instance phase offset all vary with
    /// where the surface *is*, not with how the mesh was authored. Supplied as
    /// an input rather than as a mode (D36), and it is exactly
    /// `mul(float4(Position, 1), ObjectToWorld)` — so a hook that ignores it
    /// pays nothing, because the engine computes that product anyway.
    float3 WorldPos;

    /// The object-to-world transform this draw is using, row-major
    /// (`hp/Math.hpp`, and `PackMatrixRowMajor` is set for exactly this).
    /// **Already skinned** when the mesh is skinned, so a hook composes with
    /// skinning rather than fighting it.
    float4x4 ObjectToWorld;

    /// World-space camera position — for billboarding, which is the one vertex
    /// technique that is *about* the camera.
    float3 CameraPos;

    /// Seconds since the application's clock started, scaled by its time scale
    /// (T0159.5). The same value `HpSurfaceInput::Time` carries, from the same
    /// buffer. **Zero in a caller that passes no time**, never undefined.
    float Time;
};

/// What the vertex hook fills in (T0146).
///
/// **Every field is pre-filled from the mesh before the hook runs**, so writing
/// nothing produces exactly today's geometry and writing one field changes
/// exactly that field — the same `inout` shape as `HpSurfaceOutput`, for the
/// same reason.
struct HpVertexOutput
{
    /// The displaced position, in **object space**. The engine transforms this.
    float3 Position;

    /// The displaced normal, in **object space**. The engine applies the node
    /// transform's inverse-transpose to it, exactly as it does to the mesh's
    /// own — so a hook that bends a normal writes the bent object-space normal
    /// and nothing else.
    ///
    /// **Not renormalised by the engine before the transform**, matching what
    /// the untouched path does; the transform normalises after.
    float3 Normal;

    /// The displaced tangent, in **object space**. Ignored by permutations
    /// whose mesh carries no tangents.
    float3 Tangent;

    /// The first texture coordinate set the pixel stage will see. Writing it is
    /// per-vertex UV animation — cheaper than the per-fragment equivalent and
    /// the right tool when the motion is affine.
    float2 UV0;

    /// The second texture coordinate set; see `UV0`.
    float2 UV1;

    /// Per-vertex colour the pixel stage will see. A vertex-stage mask (wind
    /// strength, a wetness gradient) that the pixel stage reads back through
    /// `HpSurfaceInput::VertexColor` costs no extra interpolator, because this
    /// one already exists — worth preferring over `Custom0` when it fits.
    float4 VertexColor;

    /// Four `float4` interpolators the vertex stage writes and the pixel stage
    /// reads back as `HpSurfaceInput::Custom0` … `Custom3` (T0146.4).
    ///
    /// **Godot's `varying`, as a fixed count rather than a declaration**, and
    /// the count is fixed for a reason worth knowing: `VSOutput` is generated
    /// per permutation by `PBR_Renderer`, and what a module declares is known
    /// only *after* the compile that would have to be told about it — the same
    /// circularity D35 records against a rename pass. Four slots is the
    /// number, sized against the 2026-08-06 capability audit.
    ///
    /// **They exist only in a custom-material permutation**, so a standard
    /// material pays nothing at all; a module pays for all four whether it
    /// writes them or not. Unwritten slots arrive as zero, not as noise.
    /// Widening or making the count dynamic is additive and belongs with
    /// T0151's variant work.
    float4 Custom0;

    /// See `Custom0`.
    float4 Custom1;

    /// See `Custom0`.
    float4 Custom2;

    /// See `Custom0`.
    float4 Custom3;
};

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
    /// **`w` is always +1, and it is not data — do not read it** (T0166.4).
    /// glTF authors the bitangent's handedness as ±1 in the tangent's fourth
    /// component, and it is genuinely used: the first production asset this
    /// project imported carries `w = -1` on 1,316 vertices. This engine drops
    /// it, and the reason is `PBR_Renderer.cpp:1660` rather than anything
    /// about the format — **T0168** owns widening it, with that line named.
    ///
    /// **What a shader should do instead**, and it is not a workaround:
    /// reconstruct the frame from the UV parameterisation, dividing by the
    /// *signed* determinant of `(ddx(uv), ddy(uv))`. That determinant is where
    /// a mirrored shell actually announces itself — `w` is a per-vertex copy of
    /// the same fact — and it needs no vertex attribute at all. `HpParallaxUv`
    /// does exactly that (T0166.2), and `tests/gpu/tangent_frame_test.cpp` is
    /// the two-shell case that proves it on a mirrored chart.
    ///
    /// Until then `bitangent = cross(Normal, Tangent.xyz)` here, so a mesh
    /// whose UVs are mirrored gets that bitangent flipped **if a shader builds
    /// its frame from this field** — recorded so the symptom (normal-mapped
    /// lighting inverted on mirrored islands, from vertex tangents only) finds
    /// its cause.
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

    /// `ScreenPos.xy` normalised to 0..1 across the **render target** (T0147).
    ///
    /// The coordinate the engine's screen intermediates are addressed by —
    /// `HpSceneColour(In.ScreenUV)` and `HpSceneDepth(In.ScreenUV)` — and the
    /// one Godot calls `SCREEN_UV`. Origin is the top-left texel, matching the
    /// textures it indexes.
    ///
    /// **The target's size, not the viewport's**, and the two differ under a
    /// letterboxing aspect policy (T0081). That is deliberate: the snapshots
    /// are target-sized, so this samples the right texel in the letterboxed
    /// case, where dividing by the viewport would not. A shader wanting a
    /// *viewport*-relative coordinate — a vignette, a screen-space gradient
    /// that should ignore the bars — should build it from
    /// `g_Frame.Camera.f4ViewportSize` instead.
    float2 ScreenUV;

    /// World-space camera position. Present because a surface function that
    /// needs distance-based behaviour should not have to reconstruct it.
    float3 CameraPos;

    /// Seconds since the application's clock started, scaled by its time
    /// scale — the value the frame loop hands `SceneRenderer` (T0159.5).
    /// Frame-wide and identical for every fragment; what scrolling UVs,
    /// flowmaps and pulsing emissive animate on. **Zero in a caller that
    /// passes no time** (a bare `render()` call), never undefined.
    float Time;

    /// The interpolated `HpVertexOutput::Custom0` (T0146.4) — the channel
    /// from the vertex hook to this one.
    ///
    /// **Zero for a material with no vertex module**, and zero for a slot the
    /// vertex hook did not write, so a shader may read it unconditionally.
    /// Interpolated with perspective correction like every other varying; a
    /// value that must not be interpolated belongs in a constant buffer.
    float4 Custom0;

    /// See `Custom0`.
    float4 Custom1;

    /// See `Custom0`.
    float4 Custom2;

    /// See `Custom0`.
    float4 Custom3;
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

    // -----------------------------------------------------------------------
    // The extended material layers (T0143, amending D24). **Every field below
    // exists in every permutation** (D31): a feature that is off reads as its
    // zero, never as a missing member, so a material module compiles against
    // one struct shape everywhere. Writing one from `surface()` is the
    // "modify for custom too" half of T0143 — the value written here is the
    // value lit.
    //
    // What each *does* without an environment is uneven, and stated per
    // field rather than implied: clearcoat, sheen, anisotropy and iridescence
    // shape the punctual response and are visible under a lamp; transmission
    // removes diffuse and drives blend-pass alpha; **thickness is carried but
    // shades nothing until T0087's environment exists** — its refraction is
    // an image-based term.
    // -----------------------------------------------------------------------

    /// Clearcoat layer strength, 0 = no coat, 1 = full lacquer layer. The
    /// coat is a second specular lobe over everything, and it *dims* what is
    /// underneath by its Fresnel — glTF's `KHR_materials_clearcoat`.
    float Clearcoat;

    /// The coat's own perceptual roughness, independent of the base layer's.
    float ClearcoatRoughness;

    /// World-space normal of the coat. Pre-filled with the surface's
    /// geometric normal — and with the clearcoat normal map applied when the
    /// material has one — so orange-peel lacquer is one map away.
    float3 ClearcoatNormal;

    /// Sheen colour, linear RGB. Black = no sheen. The retro-reflective rim
    /// of velvet and cloth — `KHR_materials_sheen`.
    float3 SheenColor;

    /// Sheen roughness, 0..1.
    float SheenRoughness;

    /// Anisotropy strength, 0 = isotropic, 1 = maximally stretched highlight
    /// — `KHR_materials_anisotropy`. Brushed metal is this.
    float Anisotropy;

    /// The direction the highlight stretches along, in tangent space,
    /// **after** the material's authored rotation. (1, 0) = along the mesh
    /// tangent. Meshes using anisotropy should carry TANGENT attributes, per
    /// the glTF extension; without them the frame falls back to world X.
    float2 AnisotropyDirection;

    /// Thin-film iridescence factor, 0..1 — `KHR_materials_iridescence`.
    /// Soap bubbles and oil slicks; it bends the base layer's F0, so it is
    /// visible in the punctual specular even without an environment.
    float Iridescence;

    /// The thin film's thickness in **nanometres**. 0 disables the effect
    /// (mirroring upstream: a zero-thickness film forces the factor to 0).
    float IridescenceThickness;

    /// Transmission factor, 0..1 — `KHR_materials_transmission`. Removes the
    /// diffuse response and, on a blended material, drives how much of the
    /// scene behind shows through (`alpha = 1 - Transmission`).
    float Transmission;

    /// Volume thickness in metres — `KHR_materials_volume`. **Carried and
    /// debug-visible, but shades nothing yet**: its consumer is the
    /// image-based refraction that arrives with T0087.
    float Thickness;
};

// ---------------------------------------------------------------------------
// ## The lighting stage (T0145, D30, D31)
//
// **The surface stage says what a surface *is*; this one says how light
// responds to it.** Everything above decides albedo, roughness, normals — and
// then the engine applies a fixed physically-based response. Two methods on
// `IHpMaterial` replace that response, wholly or per light:
//
// | Rung | Method | You own | The engine still owns |
// |---|---|---|---|
// | 3 | `light()` | the response to **one** light | the loop, range attenuation, the spot cone, which lights are here |
// | 4 | `lighting()` | the **whole** stage — loop, accumulation, resolve | the pass, its targets, the light *data* |
//
// **This is Godot's `light()`, with three things Godot does not have** (checked
// against 4.7.1-stable, D30 carries the sources): the light's **type** is
// visible, so omni and spot are distinguishable where Godot's `light()` can
// only ask "is it directional"; attenuation arrives **unfolded** from the
// shadow term, where Godot's `ATTENUATION` has shadow multiplied in with no way
// back (godot-proposals #15040, open); and rung 4 exists at all, where Godot's
// equivalent (`light_post()`, proposal #484) has been open since 2019.
//
// ### Cel shading, the whole of it
//
//     struct HpMaterial : IHpMaterial
//     {
//         [mutating] override HpLightResponse light(HpLight L, HpShadedSurface S,
//                                                   HpSurfaceInput In)
//         {
//             HpLightResponse R = HpStandardLight(L, S);
//             // The one line the whole rung exists for: quantise the falloff.
//             R.NdotL = floor(R.NdotL * 3.0) / 3.0;
//             return R;
//         }
//     }
//
// `HpStandardLight` is the engine's own default — the physically-based response
// — so an override starts from it and changes one term rather than restating a
// BRDF. Everything the engine multiplies is a **field**, which is what makes the
// override a line instead of a rewrite:
//
//     Punctual += (R.Diffuse + R.Specular) * R.Intensity * R.NdotL
//
// A **ramp** shader writes `R.Diffuse = ramp.SampleLevel(HpSamplerLinearClamp,
// float2(R.NdotL, 0.5), 0).rgb` over its own declared texture and sets
// `R.NdotL = 1.0`. A **custom BRDF** writes `R.Diffuse` and `R.Specular`
// outright. **Hatching** reads `In.ScreenUV` and `R.NdotL` together.
//
// ### Whose lights, and in what order — decided, not left to the sort (T0145)
//
// **`Light.Index == 0` is the dominant light**, and that is a promise rather
// than an accident. Until T0145 the engine's per-object selection ranked lights
// by squared distance, every directional light carried the key 0, and
// `std::sort` is not stable — so which directional light landed at index 0 was
// *unspecified*, and on the machine it was measured on it was the dim fill
// (T0158/T0159 found this the expensive way). The order is now total:
//
//   * **nearest first** — directional lights carry distance 0, so they precede
//     every punctual light that is not exactly on top of the object;
//   * **brightest first** among lights at equal distance, by Rec. 709 luminance
//     of colour × intensity;
//   * **entity order** last, so two identical lights still have a fixed order
//     and a frame is reproducible.
//
// So `Index == 0` is "the sun, or the brightest of several suns" for any object
// a directional light reaches. A technique that wants to spend something on one
// light — a parallax shadow march, a shaped specular — spends it there.
//
// ### What a hook must not assume
//
// - **`Attenuation` is geometry only** — range falloff times the spot cone.
//   The shadow term is *not* folded in, deliberately (that is the Godot ceiling
//   above), and it does not exist yet at all: `ShadowFactor` arrives with T0086
//   as a field of its own, and no field arrives before its system (D27).
// - **A light whose attenuation is zero is never handed over.** The engine
//   skips it, as DiligentFX's loop does, so a hook never runs for a light
//   outside its own range or outside its cone.
// - **Visibility is not illumination** (T0093). A dark room inside a vision
//   cone is *visible and dark*, so a visibility term multiplies the **resolved**
//   colour — rung 4, or `surface()` — never a light's contribution.
//
// ### The three primitives a whole-stage override is built from
//
// | Helper | What it gives you |
// |---|---|
// | `HpLightCount()` | how many lights reach this fragment, already clamped |
// | `HpGetLight(i, worldPos)` | light `i` as an `HpLight`, attenuation and cone resolved |
// | `HpStandardLight(L, S)` | the engine's own physically-based response |
// | `HpResolveLighting(S, punctual)` | punctual + emissive (+ ambient, with T0087) |
//
// The default `lighting()` is exactly those four in a loop, so an override that
// wants "the standard loop, one term changed" copies five lines rather than
// reimplementing a BRDF.
//
// ### Godot's lighting render modes, and where each one went (D38)
//
// Written down because the obvious move was to mirror them, and mostly the
// answer is that the ladder already expresses the thing:
//
// | Godot | Here |
// |---|---|
// | `unshaded` | **exists, two doors** — `unlit` in the `.hpmat` (a permutation bit, so the lighting is genuinely not compiled) or `IHpMaterial.unshaded()` |
// | `diffuse_lambert`, `specular_disabled`, … | **write it.** `R.Diffuse = S.DiffuseColor / 3.14159;` is lambert, `R.Specular = float3(0, 0, 0);` is specular off. A mode enum would be a second mechanism for one domain |
// | `ambient_light_disabled` | **nothing to disable yet.** Ambient arrives with T0087, inside `HpResolveLighting`; a material that does not want it overrides `lighting()` and returns the punctual sum |
// | `shadows_disabled` | **with T0086.** The shadow factor arrives *beside* `Attenuation`, not folded into it — so ignoring the field is how a material declines shadows, in one line |
// | `vertex_lighting` | **rejected** (D38) |
// ---------------------------------------------------------------------------

/// A light that travels one way and never falls off — the sun. `HpLight::Type`.
static const int HpLightDirectional = 1;

/// A light radiating from a point in every direction. `HpLight::Type`.
static const int HpLightPoint = 2;

/// A point light restricted to a cone. `HpLight::Type`.
static const int HpLightSpot = 3;

/// One light, as it arrives at one fragment (**D31**).
///
/// **The engine's own type, mirrored from DiligentFX's rather than re-exported,
/// and the difference is the whole of D31.** Their `PBRLightAttribs` carries
/// `DirectionX`/`DirectionY`/`DirectionZ` and `IntensityR`/`G`/`B` as separate
/// floats — constant-buffer packing artifacts — and its optional members come
/// and go with `#if`s, so a game shader reading one would compile on some
/// materials and not others. This restates what the ladder needs as `float3`s
/// with every field always present, converted inside the engine's own shader at
/// the point of use. A game shader never names a Diligent struct.
struct HpLight
{
    /// `HpLightDirectional`, `HpLightPoint` or `HpLightSpot`.
    ///
    /// **Godot's `light()` cannot ask this** — it exposes only
    /// `LIGHT_IS_DIRECTIONAL`, so omni and spot are indistinguishable there.
    int Type;

    /// Where this light sits in the frame's list. **0 is the dominant light**
    /// — see the ordering contract above. Ranges over `0 .. HpLightCount()-1`.
    int Index;

    /// World-space position. **Zero for a directional light**, which has none.
    float3 Position;

    /// The unit direction the light **travels**, world space.
    ///
    /// For a directional or spot light this is the lamp's own aim. For a point
    /// light it is resolved per fragment: the direction from the light to *this*
    /// fragment. `ToLight` is its negation and is what shading wants.
    float3 Direction;

    /// The unit direction from this fragment **toward the light** — Godot's
    /// `LIGHT`, and the vector every BRDF takes.
    float3 ToLight;

    /// Linear RGB radiance of the lamp itself, colour times intensity,
    /// **before** attenuation.
    float3 Color;

    /// Range falloff times the spot cone, 0 to 1. **1 for a directional light.**
    ///
    /// **Geometry only.** No shadow term is folded in — that is exactly the
    /// ceiling Godot's `ATTENUATION` hits — and none exists yet; T0086 adds
    /// `ShadowFactor` beside this rather than inside it.
    float Attenuation;

    /// `Color * Attenuation` — the radiance actually reaching this fragment,
    /// and what the standard response multiplies by. Precomputed because the
    /// engine needs it anyway.
    float3 Radiance;

    /// The lamp's range in metres. **0 means unlimited**, and a directional
    /// light is always 0. Past its range a light is not handed over at all.
    float Range;

    /// Cosine of the spot light's inner cone half-angle — inside it the cone is
    /// at full brightness. 1 for anything that is not a spot light.
    float SpotInnerCos;

    /// Cosine of the spot light's outer cone half-angle — outside it the cone
    /// contributes nothing. 1 for anything that is not a spot light.
    float SpotOuterCos;
};

/// The surface a light is being applied to (**D31**).
///
/// The mirror of DiligentFX's `SurfaceShadingInfo` and the
/// `SurfaceReflectanceInfo` inside it, flattened and unconditional. Filled from
/// `HpSurfaceOutput` after every surface hook has run, so this is the surface
/// **as the material finally described it**.
struct HpShadedSurface
{
    /// World-space position of this fragment.
    float3 Position;

    /// Unit direction from this fragment toward the camera.
    float3 View;

    /// The unit shading normal — `HpSurfaceOutput::Normal`, normalised.
    float3 Normal;

    /// `dot(Normal, View)`, clamped away from zero. **Clamped rather than
    /// saturated**: the BRDF divides by it, and an exactly edge-on normal
    /// otherwise produces a ring of NaN pixels.
    float NdotV;

    /// The surface's albedo, linear RGB — `HpSurfaceOutput::BaseColor.rgb`.
    /// What a cel or ramp shader tints; the metal-aware split is below.
    float3 BaseColor;

    /// 0 = dielectric, 1 = metal.
    float Metallic;

    /// Perceptual roughness, 0 = mirror, 1 = fully rough. The same value
    /// `SurfaceReflectanceInfo::PerceptualRoughness` carries.
    float Roughness;

    /// The diffusely reflecting colour: `BaseColor * 0.96 * (1 - Metallic)`.
    /// A metal has none, which is why this is not `BaseColor`.
    float3 DiffuseColor;

    /// Specular reflectance at normal incidence (F0) — 4% for a dielectric,
    /// the base colour for a metal.
    float3 Reflectance0;

    /// Specular reflectance at grazing incidence (F90).
    float3 Reflectance90;

    /// Linear RGB emissive, added by the resolve. Not clamped to 1.
    float3 Emissive;

    /// How much ambient light reaches this fragment, 0 to 1. Applies to the
    /// image-based term when T0087 lands; punctual light does not use it.
    float Occlusion;

    // -----------------------------------------------------------------------
    // The extended layers (T0143), flattened from `SurfaceShadingInfo`'s
    // Clearcoat/Sheen/Anisotropy/Iridescence members exactly as the base
    // layer above is flattened from `BaseLayerShadingInfo` — and, like every
    // field here, **present and zeroed when the feature is off** (D31).
    // Derived values (the anisotropy frame, the coat's F0, iridescence's
    // effect on `Reflectance0`) are computed by the engine *after* the
    // surface stage, from whatever `HpSurfaceOutput` finally said.
    // -----------------------------------------------------------------------

    /// Clearcoat strength — `HpSurfaceOutput::Clearcoat`.
    float Clearcoat;

    /// The coat's perceptual roughness.
    float ClearcoatRoughness;

    /// The coat's unit world-space normal.
    float3 ClearcoatNormal;

    /// The coat's specular reflectance at normal incidence — 0.04, from the
    /// fixed IOR of 1.5 upstream uses; grazing reflectance is 1. Scalar
    /// because the coat is achromatic.
    float ClearcoatReflectance0;

    /// Sheen colour, linear RGB.
    float3 SheenColor;

    /// Sheen roughness.
    float SheenRoughness;

    /// Anisotropy strength, 0 = isotropic.
    float Anisotropy;

    /// The anisotropy frame's world-space tangent — the direction the
    /// highlight stretches along.
    float3 AnisotropyTangent;

    /// The anisotropy frame's world-space bitangent.
    float3 AnisotropyBitangent;

    /// GGX alpha along the tangent: `lerp(roughness², 1, strength²)`.
    float AlphaRoughnessT;

    /// GGX alpha along the bitangent: `roughness²`.
    float AlphaRoughnessB;

    /// Iridescence factor after upstream's zero-thickness kill switch. Its
    /// *effect* is already folded into `Reflectance0` by the engine, exactly
    /// as upstream folds it into the base layer's F0.
    float Iridescence;

    /// Thin-film thickness, nanometres.
    float IridescenceThickness;

    /// Transmission factor. The standard response multiplies diffuse by
    /// `1 - Transmission`, mirroring `ApplyPunctualLight`.
    float Transmission;

    /// Volume thickness, metres. **Read by nothing in the punctual path** —
    /// its consumer is T0087's image-based refraction; see
    /// `HpSurfaceOutput::Thickness`.
    float Thickness;
};

/// What a per-light method answers with (**rung 3**).
///
/// **Every field is a factor the engine multiplies**, and that is deliberate:
///
///     Punctual += (Diffuse + Specular) * Intensity * NdotL
///
/// so a cel shader quantises `NdotL`, a ramp shader replaces `Diffuse` and sets
/// `NdotL` to 1, a coloured-light-response shader rewrites `Intensity`, and a
/// custom BRDF writes `Diffuse` and `Specular` outright. Splitting the factors
/// rather than returning one radiance is what makes each of those one line.
///
/// `HpStandardLight(L, S)` fills it with the engine's physically-based answer,
/// so an override starts from the default instead of restating it.
struct HpLightResponse
{
    /// The diffuse lobe's reflectance, **before** intensity and `NdotL`.
    /// Lambertian in the default: `DiffuseColor / pi` times one minus the
    /// Fresnel term.
    float3 Diffuse;

    /// The specular lobe's reflectance, **before** intensity and `NdotL`.
    /// Smith-GGX in the default.
    float3 Specular;

    /// The radiance reaching this fragment from this light. Defaults to
    /// `Light.Radiance`, which is the lamp's colour times its attenuation.
    float3 Intensity;

    /// The cosine falloff, `saturate(dot(Normal, ToLight))` by default and
    /// **the term cel shading exists to change**. Quantising it here bands each
    /// light separately, which is the thing a post-process pass over the summed
    /// image can never do (Godot proposal #484's exact complaint).
    float NdotL;
};

#endif // _HP_MATERIAL_SLANG_
