# `HpMaterial`

*Generated from `engine/shaders/HpMaterial.slang` — do not edit.*

The contract a game's shader compiles against (T0141.6, D27; Slang since
T0142.13, D28).

**This is the only engine header a game shader includes**, and that is a
decision with a cost, recorded as D27. A game shader may not include
`PBR_Shading.fxh` or any other DiligentFX header: doing so would make
DiligentFX's internals part of this engine's public contract, so every
upstream rename would silently break every shader in every shipped game. We
include DiligentFX; they include us. A rename upstream is then ported once,
here, behind a header whose shape does not move.

#### The authoring shape, stated once and correctly

A game writes a **module that implements `IHpMaterial`** and overrides only
what it wants (D28, T0142.15). `override` is mandatory, so nothing the engine
does is replaced by accident:

    struct HpMaterial : IHpMaterial
    {
        override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
        {
            return float4(1.0, 0.0, 0.0, 1.0);
        }
    }

**The module includes nothing.** The engine includes *it*, into
`HpSurface.slang` after this contract and after DiligentFX's getters, through
a generated one-line include — which is what lets an override call
`GetBaseColor` without knowing where it comes from, and what lets the content
hasher recurse into the module's own text (T0142.7).

##### What this file used to say, and why it stopped being true

It documented Godot's shape literally — *"a developer writes one function"*,
`void HpSurface(in HpSurfaceInput In, inout HpSurfaceOutput Out)`, forward
declared at the bottom of this file so that a shader which forgot to define
it failed at link with a clear missing-symbol error.

**A game cannot define that function**, and has not been able to since
T0142.15 put the module inside `HpSurface.slang`: the engine's own definition
is already in scope when the module is included, so a second body is
`error[E30201]: function 'HpSurface' already has a body` — measured on the
pinned `slangc 2026.14.1`, not reasoned about after the fact.

D27's function-style hook is not gone; it moved. It is
`IHpMaterial.surface()`, whose default calls the engine's own `HpSurface` —
so a material wanting the whole-output hook overrides *that*, and the free
function is what it now honestly is: the engine's internal default, private
to `HpSurface.slang`. The declaration that used to sit at the bottom of this
file went with the claim it supported.


#### `HpSurfaceInput` is a promise

**Adding a field later is free; removing one breaks every game that used it.**
So this list is decided deliberately rather than grown by accident, and it
holds exactly what the engine genuinely computes today.

**Nothing is exposed before the system behind it exists.** A `Visibility`
field that returns 1.0 because T0093 has not been built is worse than no
field: a developer writes a shader against it, their effect silently does
nothing, and there is no error anywhere. That is the mistake
`Camera::cullingMask` spent three tickets being, arriving in a *public*
contract where the cost is every game that trusted it.

So each system's own ticket adds its own field when it lands, which is one
line and additive by construction:

| Field | Arrives with |
|---|---|
| `ShadowFactor` | T0086 — shadows |
| `Visibility` | T0093 — vision-based visibility |
| `AmbientOcclusionIBL` | T0087 — environment lighting |
| `Velocity` | T0111 — motion vectors |
| `Time` | **the field exists and is never written** (T0159.5). `PBRFrameAttribs::Time` is there; `hp::Time` has the clock; `SceneRenderer` does not connect them |

#### The struct is a contract, not a wire format

A field here does **not** imply an interpolator. Vertex-to-pixel interpolators
are a limited hardware resource, and paying for every field on every draw
whether a shader reads it or not would land hardest on exactly the dense
geometry that can least afford it. `ViewDir` is computed in the pixel shader
from the camera position and the world position rather than interpolated; more
fields will follow that pattern as they are added.

3 declaration(s), 15 member(s), all documented.

## `HP_UNSHADED`

```hlsl
#define HP_UNSHADED 0
```

#### Unshaded

**A game material can opt out of lighting entirely**, which is Godot's
`render_mode unshaded;` and is requested often enough to be part of the
contract rather than a workaround. `Out.BaseColor` is then written straight
to the target with no BRDF, no light loop and no IBL — UI, sprites,
holograms, debug overlays and most VFX want exactly this.

**There are two doors to it and they are not interchangeable**, which is
worth knowing before reaching for either:

  * `IHpMaterial.unshaded()`, overridden to `true` by a module — the
    *authored* half (T0142.16). Exact in effect, and **not folded at the
    SPIR-V level**: measured at 19336 bytes against the shaded 19348, at
    optimisation level none and default alike, so the lighting is still
    emitted and the branch is real. T0151's link-time constants are the
    mechanism that would make the elimination provable.
  * `Material::unlit` in a `.hpmat` (T0060.1) — the *data* half, which rides
    the `HP_UNSHADED` permutation bit below, so the lighting is excluded at
    preprocess time and genuinely not paid for.

The macro is that permutation bit. `SurfacePipeline::build` always defines it
from the PSO key; the default here exists so this contract still reads on its
own.

## `HpSurfaceInput`

```hlsl
struct HpSurfaceInput
```

What the engine hands a surface function.

Read-only. Everything here is in **world space** unless the field says
otherwise, matching the engine's convention everywhere else (`hp/Math.hpp`).

### `HpSurfaceInput::UV0`

```hlsl
float2 UV0;
```

The first texture coordinate set, after the material's UV0 transform.

### `HpSurfaceInput::UV1`

```hlsl
float2 UV1;
```

The second texture coordinate set, after the material's UV1 transform.
**Zero when the mesh carries only one set** — not an error, and the same
fallback `SelectUV` makes on the standard path.

### `HpSurfaceInput::WorldPos`

```hlsl
float3 WorldPos;
```

World-space position of this fragment. The basis for triplanar
projection (T0141.8) and for anything that varies with where a surface
is rather than how it is unwrapped.

### `HpSurfaceInput::Normal`

```hlsl
float3 Normal;
```

World-space geometric normal, **before** any normal map is applied.
A surface function that writes `Out.Normal` replaces it; one that does
not gets this.

### `HpSurfaceInput::Tangent`

```hlsl
float4 Tangent;
```

World-space tangent and the handedness of the bitangent in `w`.

**Zero ALWAYS, not only when the mesh lacks tangents** (T0159.4). The
assignment in `HpSurface.slang` is unconditional, even though
`RenderPBR.vsh` writes a world-space tangent whenever the mesh carries
one. This sentence said "zero when the mesh has no tangents" until an
audit measured otherwise. Normal mapping and parallax both
need this, so a surface function that uses either must tolerate its
absence rather than producing a black surface — the engine cannot invent
tangents a mesh does not carry.

### `HpSurfaceInput::ViewDir`

```hlsl
float3 ViewDir;
```

Normalised direction from the fragment **to the camera**. Computed here
rather than interpolated; see the header comment.

### `HpSurfaceInput::VertexColor`

```hlsl
float4 VertexColor;
```

Per-vertex colour, interpolated. White when the mesh carries none, so a
shader may multiply by it unconditionally.

### `HpSurfaceInput::ScreenPos`

```hlsl
float4 ScreenPos;
```

Position in the render target, in pixels, with depth in `z` and `1/w`
in `w` — the raw `SV_POSITION` a pixel shader receives.

### `HpSurfaceInput::CameraPos`

```hlsl
float3 CameraPos;
```

World-space camera position. Present because a surface function that
needs distance-based behaviour should not have to reconstruct it.

## `HpSurfaceOutput`

```hlsl
struct HpSurfaceOutput
```

What a surface function fills in.

**Every field is initialised from the material before the surface function
runs**, so writing nothing produces the standard material and writing one
field changes exactly that field. That is what makes a three-line shader
useful, and it is why this is `inout` rather than `out`.

### `HpSurfaceOutput::BaseColor`

```hlsl
float4 BaseColor;
```

Linear RGB and alpha. Alpha is compared against the material's cutoff
when its alpha mode is `Mask`, and blended when it is `Blend`.

### `HpSurfaceOutput::Normal`

```hlsl
float3 Normal;
```

World-space normal. Pre-filled with the geometric normal, and with the
material's normal map applied when it has one — so a shader that wants
the standard normal mapping gets it by leaving this alone.

### `HpSurfaceOutput::Metallic`

```hlsl
float Metallic;
```

0 = dielectric, 1 = metal.

### `HpSurfaceOutput::Roughness`

```hlsl
float Roughness;
```

0 = mirror, 1 = fully rough.

### `HpSurfaceOutput::Emissive`

```hlsl
float3 Emissive;
```

Linear RGB added on top of shading. **Not** clamped to 1: values above it
are meaningful once T0096's tonemapping exists.

### `HpSurfaceOutput::Occlusion`

```hlsl
float Occlusion;
```

How much ambient light reaches this fragment, 0 to 1.
