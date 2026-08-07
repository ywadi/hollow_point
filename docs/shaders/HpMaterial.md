# `HpMaterial`

*Generated from `engine/shaders/HpMaterial.slang` — do not edit.*

The contract a game's shader compiles against (T0141.6, D27; Slang since
T0142.13, D28).

**A game's module includes nothing and may reach anything** (D27, as
amended 2026-08-06). The module is compiled *inside* `HpSurface.slang`, so
everything that file can see is in scope for an override: this contract,
DiligentFX's getters (`GetBaseColor`, `PerturbNormal`, ...), the frame,
primitive and material constant buffers (`g_Frame`, `g_Primitive`,
`g_Material` -- including `g_Frame.Lights[]` under `PBR_MAX_LIGHTS`), the
permutation macros, and the engine's own helpers (`HpParallaxMarch`,
`g_HeightMap`). **That is deliberate, with no warning and no version
check**: this engine is permanently on Diligent (D24, D29), and a shipped
game never meets a newer engine under D12's lockstep, so an upstream rename
is development-time friction caught by a loud compile failure -- magenta
plus one logged error -- not breakage in the field.

The trade to understand before reaching past this file: `HpSurfaceInput`
below is a **promise** that holds across engine updates; everything else is
whatever DiligentFX and the engine currently ship, and it may be renamed by
an upgrade. Reaching for an internal is also a *signal* -- it means this
contract is missing something, and the capability matrix
(`13-shader-capability-matrix.md`) is where that gap should be recorded so
it becomes a widening rather than a workaround every game repeats.

#### Two stages, one module (T0146)

A module implements `IHpMaterial`, and since T0146 that interface spans the
**vertex** stage as well as the pixel one: `vertex()` displaces geometry,
everything else shades it. Both entry points of the engine's shader compile
the same module in the same compile, so one file is the whole material.

The vertex hook's own section is further down, beside `HpVertexInput`; the
two facts to carry into it are that it works in **object space** (D36) and
that its members do **not** reach the pixel stage — `HpVertexOutput::Custom0`
… `Custom3` are the channel that does.

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

`Time` sat in that table — the field existed in `PBRFrameAttribs` and was
never written — until T0159.5 connected the clock and added it below.

**T0147 added `ScreenUV` and did *not* add `Visibility`, and the restraint is
the point.** A game can build the fog-of-war dim today, out of a texture its
own layer renders and feeds by name (see the game-fed section below) — that
is a *mechanism*, and it is honest about what produces the bytes. A
`Visibility` field would be the engine promising a number no engine system
computes, which is exactly the failure this table exists to prevent. It
arrives when T0093's system does, and then it is one line.

#### The struct is a contract, not a wire format

A field here does **not** imply an interpolator. Vertex-to-pixel interpolators
are a limited hardware resource, and paying for every field on every draw
whether a shader reads it or not would land hardest on exactly the dense
geometry that can least afford it. `ViewDir` is computed in the pixel shader
from the camera position and the world position rather than interpolated; more
fields will follow that pattern as they are added.


#### A module's own parameters and resources (T0160, T0161, D35)

**Your module declares its own textures under its own names, at whatever
count it wants**, as ordinary globals — no registration, no attributes, no
engine identifiers:

    Texture2DArray detailAlbedo;
    Texture2DArray puddleMask;

    cbuffer HpMaterialParams
    {
        [HpRange(0.0, 1.0)]
        [HpTooltip("How wet the surface reads.")]
        float wetness;

        [HpColor]
        float3 tint;
    }

The engine reflects the compiled module and binds what it finds; the
`.hpmat` beside it supplies values and textures **by your names**:

    params:
      - name: wetness
        value: 0.75
    textures:
      - name: detailAlbedo
        texture: 1570000000000031

The conventions, each of which the engine enforces loudly rather than
trusting:

- **`HpMaterialParams` is the one reserved name.** The parameter *block* is
  called that so a `.hpmat`'s values have one place to go; every field name
  inside it is yours. A second constant buffer under another name is
  refused, by name, at pipeline build. The block is capped at **256 bytes**
  and carries `float`/`float2`/`float3`/`float4`/`int`/`bool`; anything
  else is reported by name and left at what the shader initialises it to.
- **Declare textures as `Texture2DArray`.** Every texture this engine binds
  is a one-slice 2D array; a plain `Texture2D` is refused by name at
  pipeline build, because past that point the mismatch would be a raw
  Vulkan validation error in a cooked game.
- **Sample through the engine's sampler palette** — `HpSamplerLinearWrap`,
  `HpSamplerLinearClamp`, `HpSamplerPointWrap`, `HpSamplerPointClamp`,
  `HpSamplerAnisoWrap`, `HpSamplerAnisoClamp`, declared below. Sampler
  *state* is the engine's vocabulary, and this is the one deliberate limit
  (D35): the choice of sampler travels inside your compiled code, so a
  cooked build carries no sampler metadata at all. Declaring your own
  `SamplerState` is refused by name, with the palette in the log line.
- **Declare unconditionally, at module top level.** A declaration inside a
  permutation `#if` makes "what does this module declare" a question with
  one answer per macro set, and neither a `.hpmat` nor an inspector row can
  be permutation-dependent. An unused declaration is stripped from the
  compiled code and costs nothing.
- **Values are data, not permutations.** Changing a `.hpmat` value rebuilds
  no pipeline and invalidates no cook.
- **Two namespaces, one line between them.** Resources you *declare* are
  yours, fed by the `.hpmat` or by the game (see below). Resources the
  *engine* feeds — the frame, the lights, the height map, the screen
  intermediates (T0147) and later the shadow resources (T0086) — are the
  engine's names, reached as documented above. A texture neither the
  `.hpmat` nor the game names samples **white**; a GUID that does not
  resolve samples the missing-asset checkerboard.
- **Buffers are declared the same way but no material stage can feed one
  yet** — a structured buffer in a surface module is refused by name until
  the stage whose point they are (T0150 compute) lands. The mechanism is
  already stage-neutral; that data path is not built.

##### A texture the *game* produced, not the `.hpmat` (T0147.4, T0094)

**Same declaration, second source.** A `.hpmat` binds a texture *asset*; a
game layer binds a texture it *rendered* — a fog-of-war visibility field, a
flow simulation, a minimap — through the engine, by the same name your
module declares:

    // gameplay C++, once the layer has drawn into its own target
    layer.setGameTexture("visibility", myTarget);

    // the module, unchanged from any other declared texture
    Texture2DArray visibility;
    ...
    float seen = visibility.SampleLevel(HpSamplerLinearClamp,
                                        float3(In.ScreenUV, 0.0), 0.0).r;

The resolution order is **`.hpmat` first, then the game feed, then white**:
a material that names an asset for that slot keeps it, because authored
content is the more specific statement. Nothing is refused and nothing
warns — a name no one feeds is white, exactly as it was before, so a module
can declare a slot the game has not started feeding yet.


#### Screen resources: what the frame looks like behind this fragment (T0147)

**Two engine-fed textures, and one rule that governs both.** They are what
refraction, glass, heat haze, frosted glass, soft particles and fog-of-war
are made of, and Godot's `hint_screen_texture` / `hint_depth_texture` are
the same thing under other names.

    float4 behind = HpSceneColour(In.ScreenUV + offset);  // linear RGB
    float  sceneZ = HpSceneViewDepth(In.ScreenUV);        // metres
    float  fade   = saturate((sceneZ - HpViewDepth(In.ScreenPos.z)) / 0.5);

| Helper | What it returns |
|---|---|
| `HpSceneColour(uv)` | the scene's colour, **linear**, pre-tonemap (T0096) |
| `HpSceneDepth(uv)` | raw device depth — near is **1**, far is **0** (reverse-Z, T0130) |
| `HpSceneViewDepth(uv)` | that depth as a distance along the view axis, in metres |
| `HpViewDepth(z)` | the same conversion for any device depth, including your own `In.ScreenPos.z` |

The textures themselves are `g_SceneColour` and `g_SceneDepth`, sampled
through the palette (`HpSamplerLinearClamp`, `HpSamplerPointClamp`); the
helpers exist so that a shader author never has to remember which sampler
or which depth convention.

##### The rule: **only a material with `alphaMode: Blend` may read them**

**And that is enforced, not documented.** These are *snapshots*, copied out
of the frame between the opaque pass and the blend pass — so an opaque
material reading them would read the previous frame, or an uninitialised
texture on the first one, which is the exact class of bug that works on one
driver and not the next. A module that samples either from a pipeline whose
alpha mode is not `Blend` **fails to build**: the engine logs one line
naming the module and the rule, and the surface renders the
missing-material checkerboard.

##### What they contain, stated precisely

The frame **as of the end of the opaque pass**: every `Opaque` and `Mask`
surface, and the clear colour where nothing was drawn. In particular:

- **No blended geometry at all** — including blended surfaces drawn *before*
  yours. A pane of glass behind another pane does not appear in the second
  one's refraction. That is the limitation every engine ships (Godot's
  screen texture has it too); it is documented, not solved, because solving
  it means a snapshot per transparent draw.
- **`HpSceneDepth` is 0 where nothing was drawn**, because reverse-Z clears
  the far plane to 0. `HpSceneViewDepth` therefore returns the far plane's
  distance there, which is usually what a fade wants and is occasionally a
  surprise; mask it with `HpSceneDepth(uv) > 0.0` when it matters.
- **The snapshot costs nothing when nothing reads it.** The copy is issued
  only when the frame has blended geometry *and* one of its modules reads
  the screen, so a scene of opaque materials never pays for it.

##### What is deliberately **not** offered

**There is no normal-roughness read, and there will not be one until
something produces it.** Godot's `hint_normal_roughness_texture` is
Forward+-only there for the same reason it is absent here: it is a *deferred*
resource, and this engine is forward-only (D24) with no G-buffer and no depth
prepass. Synthesising one would mean adding a pass that writes normals and
roughness for every opaque surface — real bandwidth, paid by every game, for
a technique none has asked for. **A shader already has its own** surface
normal and roughness (`HpSurfaceOutput`, and `In.Normal` before the map);
what a screen-space read adds is the *other* surface's, which is what a
prepass would have to produce. If one is ever wanted, it arrives with the
pass that writes it, and it gets its own row in the capability matrix first.

`HpTexture0` … `HpTexture3`, T0160's engine-named slots, still compile and
still bind from a `.hpmat` that names them — see their declarations below —
but they are **deprecated**: name your textures yourself.

22 declaration(s), 45 member(s), all documented.

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

## `HpRangeAttribute`

```hlsl
struct HpRangeAttribute
```

Bounds a numeric parameter, for the inspector.

`[HpRange(0.0, 1.0)]` — Godot's `hint_range`, as a Slang attribute rather
than as syntax the engine would have to invent. Equal bounds mean unbounded.
**Presentation only**: nothing clamps the value that reaches the shader, so
a hand-edited `.hpmat` outside the range still renders what it says.

### `HpRangeAttribute::lo`

```hlsl
float lo;
```

The lowest value the inspector offers.

### `HpRangeAttribute::hi`

```hlsl
float hi;
```

The highest. Equal to `lo` means unbounded.

## `HpColorAttribute`

```hlsl
struct HpColorAttribute
```

Marks a `float3`/`float4` parameter as a colour, so an inspector offers a
swatch instead of three number boxes. The bytes written are identical.

## `HpTooltipAttribute`

```hlsl
struct HpTooltipAttribute
```

Text shown on hover in the inspector.

The same slot `hp::PropertyMeta::tooltip` fills for a reflected C++ field —
one description, two reflections, which is the unification D28 anticipated.

### `HpTooltipAttribute::text`

```hlsl
String text;
```

The text itself.

## `HpSamplerLinearWrap`

```hlsl
SamplerState HpSamplerLinearWrap;
```

Bilinear filtering with mip-mapping, coordinates wrapping — the default
choice for anything that tiles across a surface.

## `HpSamplerLinearClamp`

```hlsl
SamplerState HpSamplerLinearClamp;
```

Bilinear filtering, coordinates clamped to the edge texel. What a LUT, a
ramp or an atlased texture wants — wrapping one bleeds the far edge in.

## `HpSamplerPointWrap`

```hlsl
SamplerState HpSamplerPointWrap;
```

Nearest-texel sampling, wrapping. Pixel art, ID maps, and any texture
whose texels are data rather than an image to be filtered.

## `HpSamplerPointClamp`

```hlsl
SamplerState HpSamplerPointClamp;
```

Nearest-texel sampling, clamped. The data-texture counterpart of
`HpSamplerLinearClamp`.

## `HpSamplerAnisoWrap`

```hlsl
SamplerState HpSamplerAnisoWrap;
```

8x anisotropic filtering, wrapping. For surface detail read at grazing
angles — floors, terrain, anything large and walked across. The level is
the engine's (D35); an author-chosen level is the recorded escape, waiting
on a real request.

## `HpSamplerAnisoClamp`

```hlsl
SamplerState HpSamplerAnisoClamp;
```

8x anisotropic filtering, clamped. See `HpSamplerAnisoWrap`.

## `HpTexture0`

```hlsl
Texture2DArray HpTexture0;
```

Deprecated (T0161): declare your own `Texture2DArray yourName;` instead.
Binds from a `.hpmat` `textures:` entry naming `HpTexture0`, as T0160
shipped it.

## `HpTexture1`

```hlsl
Texture2DArray HpTexture1;
```

Deprecated (T0161); see `HpTexture0`.

## `HpTexture2`

```hlsl
Texture2DArray HpTexture2;
```

Deprecated (T0161); see `HpTexture0`.

## `HpTexture3`

```hlsl
Texture2DArray HpTexture3;
```

Deprecated (T0161); see `HpTexture0`.

## `HpTexture0_sampler`

```hlsl
#define HpTexture0_sampler HpSamplerLinearWrap
```

Deprecated (T0161): the slot samplers are the palette's `LinearWrap` now,
which is bit-identical state to what the slots always had. Sample through
the palette by name in new code.

## `HpTexture1_sampler`

```hlsl
#define HpTexture1_sampler HpSamplerLinearWrap
```

Deprecated (T0161); see `HpTexture0_sampler`.

## `HpTexture2_sampler`

```hlsl
#define HpTexture2_sampler HpSamplerLinearWrap
```

Deprecated (T0161); see `HpTexture0_sampler`.

## `HpTexture3_sampler`

```hlsl
#define HpTexture3_sampler HpSamplerLinearWrap
```

Deprecated (T0161); see `HpTexture0_sampler`.

## `HpVertexInput`

```hlsl
struct HpVertexInput
```

What the engine hands the vertex hook (T0146, D36).

Read-only. **Everything here is in object space unless the field says
otherwise** — the opposite default from `HpSurfaceInput`, and it says so on
every field because that is exactly the confusion D36 exists to settle.

### `HpVertexInput::Position`

```hlsl
float3 Position;
```

The vertex's position, in **object space**, as the mesh holds it.

### `HpVertexInput::Normal`

```hlsl
float3 Normal;
```

The vertex's normal, in **object space**. `(0, 0, 1)` when the mesh
carries no normals — the same stand-in the engine's own transform uses,
never undefined.

### `HpVertexInput::Tangent`

```hlsl
float3 Tangent;
```

The vertex's tangent, in **object space**. Zero when the mesh carries
none.

`float3` rather than `float4`, and that is the wire format rather than a
choice: Diligent's vertex path carries three components, so glTF's
handedness `w` never reaches this engine (T0159.4 records the same fact
on the pixel side, where the field is a `float4` whose `w` is always +1).

### `HpVertexInput::UV0`

```hlsl
float2 UV0;
```

The first texture coordinate set, as the mesh holds it. Zero when the
mesh carries none.

### `HpVertexInput::UV1`

```hlsl
float2 UV1;
```

The second texture coordinate set. Zero when the mesh carries none.

### `HpVertexInput::VertexColor`

```hlsl
float4 VertexColor;
```

Per-vertex colour. White when the mesh carries none, so a hook may
multiply by it unconditionally — the same promise the pixel side makes.

### `HpVertexInput::WorldPos`

```hlsl
float3 WorldPos;
```

**World space**: where this vertex sits *before* the hook displaces it.

This is what Godot's `world_vertex_coords` is really wanted for — a
wind field, a distance fade or a per-instance phase offset all vary with
where the surface *is*, not with how the mesh was authored. Supplied as
an input rather than as a mode (D36), and it is exactly
`mul(float4(Position, 1), ObjectToWorld)` — so a hook that ignores it
pays nothing, because the engine computes that product anyway.

### `HpVertexInput::ObjectToWorld`

```hlsl
float4x4 ObjectToWorld;
```

The object-to-world transform this draw is using, row-major
(`hp/Math.hpp`, and `PackMatrixRowMajor` is set for exactly this).
**Already skinned** when the mesh is skinned, so a hook composes with
skinning rather than fighting it.

### `HpVertexInput::CameraPos`

```hlsl
float3 CameraPos;
```

World-space camera position — for billboarding, which is the one vertex
technique that is *about* the camera.

### `HpVertexInput::Time`

```hlsl
float Time;
```

Seconds since the application's clock started, scaled by its time scale
(T0159.5). The same value `HpSurfaceInput::Time` carries, from the same
buffer. **Zero in a caller that passes no time**, never undefined.

## `HpVertexOutput`

```hlsl
struct HpVertexOutput
```

What the vertex hook fills in (T0146).

**Every field is pre-filled from the mesh before the hook runs**, so writing
nothing produces exactly today's geometry and writing one field changes
exactly that field — the same `inout` shape as `HpSurfaceOutput`, for the
same reason.

### `HpVertexOutput::Position`

```hlsl
float3 Position;
```

The displaced position, in **object space**. The engine transforms this.

### `HpVertexOutput::Normal`

```hlsl
float3 Normal;
```

The displaced normal, in **object space**. The engine applies the node
transform's inverse-transpose to it, exactly as it does to the mesh's
own — so a hook that bends a normal writes the bent object-space normal
and nothing else.

**Not renormalised by the engine before the transform**, matching what
the untouched path does; the transform normalises after.

### `HpVertexOutput::Tangent`

```hlsl
float3 Tangent;
```

The displaced tangent, in **object space**. Ignored by permutations
whose mesh carries no tangents.

### `HpVertexOutput::UV0`

```hlsl
float2 UV0;
```

The first texture coordinate set the pixel stage will see. Writing it is
per-vertex UV animation — cheaper than the per-fragment equivalent and
the right tool when the motion is affine.

### `HpVertexOutput::UV1`

```hlsl
float2 UV1;
```

The second texture coordinate set; see `UV0`.

### `HpVertexOutput::VertexColor`

```hlsl
float4 VertexColor;
```

Per-vertex colour the pixel stage will see. A vertex-stage mask (wind
strength, a wetness gradient) that the pixel stage reads back through
`HpSurfaceInput::VertexColor` costs no extra interpolator, because this
one already exists — worth preferring over `Custom0` when it fits.

### `HpVertexOutput::Custom0`

```hlsl
float4 Custom0;
```

Four `float4` interpolators the vertex stage writes and the pixel stage
reads back as `HpSurfaceInput::Custom0` … `Custom3` (T0146.4).

**Godot's `varying`, as a fixed count rather than a declaration**, and
the count is fixed for a reason worth knowing: `VSOutput` is generated
per permutation by `PBR_Renderer`, and what a module declares is known
only *after* the compile that would have to be told about it — the same
circularity D35 records against a rename pass. Four slots is the
number, sized against the 2026-08-06 capability audit.

**They exist only in a custom-material permutation**, so a standard
material pays nothing at all; a module pays for all four whether it
writes them or not. Unwritten slots arrive as zero, not as noise.
Widening or making the count dynamic is additive and belongs with
T0151's variant work.

### `HpVertexOutput::Custom1`

```hlsl
float4 Custom1;
```

See `Custom0`.

### `HpVertexOutput::Custom2`

```hlsl
float4 Custom2;
```

See `Custom0`.

### `HpVertexOutput::Custom3`

```hlsl
float4 Custom3;
```

See `Custom0`.

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

### `HpSurfaceInput::UV0Base`

```hlsl
float2 UV0Base;
```

`UV0` as the mesh delivered it, **before** the coordinate hook ran
(T0159.3). After a parallax march `UV0` is the *displaced* coordinate,
whose screen-space derivatives are discontinuous — a frame built from
them collapses (measured: ~0.006 UV of reach against the 0.14 needed,
T0158). This is the smooth coordinate whose `ddx`/`ddy` stay valid, for
any technique that needs continuous derivatives after displacement.
Identical to `UV0` when the hook displaced nothing.

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

World-space tangent, unit length, with `w` reserved for the bitangent's
handedness. **Zero when the mesh carries no tangents** — real since
T0159.4, which ended two years of this field being assigned zero
unconditionally. A surface function must tolerate the zero case rather
than producing a black surface: the engine cannot invent tangents a
mesh does not carry, and screen-space derivatives (what `HpParallaxUv`
and DiligentFX's `PerturbNormal` build their frames from) are the
fallback that needs no vertex data.

**`w` is always +1, by decision rather than measurement** (T0159.4):
glTF authors handedness as ±1 in the tangent's fourth component, and
Diligent's vertex path carries `float3` — the loader drops `w` before
the engine ever sees it. So `bitangent = cross(Normal, Tangent.xyz)`
here, and a mesh whose UVs are mirrored (authored `w = -1`) will have
its bitangent flipped. If that ever matters in practice, the fix is
widening the wire format upstream, not guessing here — recorded so the
symptom (normal-mapped lighting inverted on mirrored islands, from
vertex tangents only) finds its cause.

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

### `HpSurfaceInput::ScreenUV`

```hlsl
float2 ScreenUV;
```

`ScreenPos.xy` normalised to 0..1 across the **render target** (T0147).

The coordinate the engine's screen intermediates are addressed by —
`HpSceneColour(In.ScreenUV)` and `HpSceneDepth(In.ScreenUV)` — and the
one Godot calls `SCREEN_UV`. Origin is the top-left texel, matching the
textures it indexes.

**The target's size, not the viewport's**, and the two differ under a
letterboxing aspect policy (T0081). That is deliberate: the snapshots
are target-sized, so this samples the right texel in the letterboxed
case, where dividing by the viewport would not. A shader wanting a
*viewport*-relative coordinate — a vignette, a screen-space gradient
that should ignore the bars — should build it from
`g_Frame.Camera.f4ViewportSize` instead.

### `HpSurfaceInput::CameraPos`

```hlsl
float3 CameraPos;
```

World-space camera position. Present because a surface function that
needs distance-based behaviour should not have to reconstruct it.

### `HpSurfaceInput::Time`

```hlsl
float Time;
```

Seconds since the application's clock started, scaled by its time
scale — the value the frame loop hands `SceneRenderer` (T0159.5).
Frame-wide and identical for every fragment; what scrolling UVs,
flowmaps and pulsing emissive animate on. **Zero in a caller that
passes no time** (a bare `render()` call), never undefined.

### `HpSurfaceInput::Custom0`

```hlsl
float4 Custom0;
```

The interpolated `HpVertexOutput::Custom0` (T0146.4) — the channel
from the vertex hook to this one.

**Zero for a material with no vertex module**, and zero for a slot the
vertex hook did not write, so a shader may read it unconditionally.
Interpolated with perspective correction like every other varying; a
value that must not be interpolated belongs in a constant buffer.

### `HpSurfaceInput::Custom1`

```hlsl
float4 Custom1;
```

See `Custom0`.

### `HpSurfaceInput::Custom2`

```hlsl
float4 Custom2;
```

See `Custom0`.

### `HpSurfaceInput::Custom3`

```hlsl
float4 Custom3;
```

See `Custom0`.

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
