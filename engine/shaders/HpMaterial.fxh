// The contract a game's shader compiles against (T0141.6, D27).
//
// **This is the only engine header a game shader includes**, and that is a
// decision with a cost, recorded as D27. A game shader may not include
// `PBR_Shading.fxh` or any other DiligentFX header: doing so would make
// DiligentFX's internals part of this engine's public contract, so every
// upstream rename would silently break every shader in every shipped game. We
// include DiligentFX; they include us. A rename upstream is then ported once,
// here, behind a header whose shape does not move.
//
// The shape is Godot's. A developer writes one function; the engine owns the
// `main` around it:
//
//   #include "HpMaterial.fxh"
//
//   void HpSurface(in HpSurfaceInput In, inout HpSurfaceOutput Out)
//   {
//       Out.BaseColor = float4(1.0, 0.0, 0.0, 1.0);
//       Out.Roughness = 0.3;
//   }
//
// **Since T0142.2 this contract also exists as a Slang `interface`** —
// `IHpMaterial` in `HpSurface.slang`, whose default implementations are the
// standard material and whose `surface()` default calls the `HpSurface`
// function declared below. The structs in this file are the shared vocabulary
// of both shapes; their field rules apply to the interface's methods
// unchanged.
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
// | `Time` | needs a frame-wide clock field, which `PBRFrameAttribs` has no room for yet |
//
// ## The struct is a contract, not a wire format
//
// A field here does **not** imply an interpolator. Vertex-to-pixel interpolators
// are a limited hardware resource, and paying for every field on every draw
// whether a shader reads it or not would land hardest on exactly the dense
// geometry that can least afford it. `ViewDir` is computed in the pixel shader
// from the camera position and the world position rather than interpolated; more
// fields will follow that pattern as they are added.

#ifndef _HP_MATERIAL_FXH_
#define _HP_MATERIAL_FXH_

// ## Unshaded
//
// **A game shader can opt out of lighting entirely**, which is Godot's
// `render_mode unshaded;` and is requested often enough to be part of the
// contract rather than a workaround:
//
//   #define HP_UNSHADED 1
//   #include "HpMaterial.fxh"
//
// `Out.BaseColor` is then written straight to the target with no BRDF, no light
// loop and no IBL. UI, sprites, holograms, debug overlays and most VFX want
// exactly this.
//
// **It is a compile-time switch and must stay one.** A runtime `if` would still
// pay for the lighting code in registers and in shader compilation, and the
// entire point of unshaded is not paying for it. So it becomes a PSO
// permutation, which is one bit and is why it is worth deciding now rather than
// bolting on: variant count is the thing that grows without limit here.
//
// Distinct from `Material::unlit` (T0060.1), which says the same thing for a
// *standard* material as data. Both end at the same place; one is authored in a
// `.hpmat` and the other in the shader.
#ifndef HP_UNSHADED
#   define HP_UNSHADED 0
#endif

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

    /// World-space position of this fragment. The basis for triplanar
    /// projection (T0141.8) and for anything that varies with where a surface
    /// is rather than how it is unwrapped.
    float3 WorldPos;

    /// World-space geometric normal, **before** any normal map is applied.
    /// A surface function that writes `Out.Normal` replaces it; one that does
    /// not gets this.
    float3 Normal;

    /// World-space tangent and the handedness of the bitangent in `w`.
    ///
    /// **Zero when the mesh has no tangents.** Normal mapping and parallax both
    /// need this, so a surface function that uses either must tolerate its
    /// absence rather than producing a black surface — the engine cannot invent
    /// tangents a mesh does not carry.
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

/// **Implemented by the game, called by the engine.**
///
/// Declared here so that a game shader which forgets to define it fails at
/// **link** with a clear missing-symbol error, rather than compiling into a
/// pipeline that draws nothing and reports success.
void HpSurface(in HpSurfaceInput In, inout HpSurfaceOutput Out);

#endif // _HP_MATERIAL_FXH_
