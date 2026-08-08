# The material file format (`.hpmat`)

A material is a YAML document describing **what one surface looks like**. Like a
scene it is meant to be read, hand-edited and diffed by a person — and, since
T0139, written from nothing by a person or a model.

This document describes the file. For what the parameters *mean* to the
renderer, see [`docs/api/Material.md`](../../docs/api/Material.md), which is
generated from the header and therefore never stale. For why the parameter set
is Diligent's rather than ours, see **D24** and
[`02-decision-log.md`](02-decision-log.md).

---

## A complete example

Every parameter the format has, in one file — this is exactly what the engine
writes:

```yaml
version: 2
material:
  baseColour:
    - 0.25
    - 0.5
    - 0.75
    - 0.5
  emissive:
    - 1
    - 0.5
    - 0
  metallic: 0.125
  roughness: 0.375
  normalScale: 2
  occlusionStrength: 0.25
  alphaMode: Mask
  alphaCutoff: 0.75
  doubleSided: true
  unlit: true
  baseColourTexture: 4f8a12c0d3e5b678
  metallicRoughnessTexture: 91aa2b3c4d5e6f70
  normalTexture: c3d4e5f60718293a
  occlusionTexture: 0102030405060708
  emissiveTexture: a1b2c3d4e5f60718
  uv0:
    scale: [2, 4]
    offset: [0.125, 0.25]
    rotation: 1.5
    wrapU: Mirror
    wrapV: Repeat
  uv1:
    scale: [1, 1]
    offset: [0, 0]
    rotation: 0
    wrapU: Clamp
    wrapV: Clamp
  baseColourUv: 0
  metallicRoughnessUv: 0
  normalUv: 0
  occlusionUv: 1
  emissiveUv: 1
  shader: 1570000000000020
  params:
    - name: referencePlane
      value: 0.5
  textures:
    - name: detailAlbedo
      texture: 1570000000000031
```

And this is a complete, valid material too:

```yaml
version: 2
material:
  baseColour: [1, 0, 0, 1]
  roughness: 0.2
```

Everything absent keeps its default. That is the same leniency scenes have and
it is what makes a three-line material worth writing by hand.

## The two top-level keys

**`version` is the material schema's**, and it is versioned separately from the
scene's. The two change for unrelated reasons, and one number for both would
force a scene migration every time a material gained a parameter.

A document with **no** version is refused rather than guessed at, and one from a
**newer** schema is refused rather than partially loaded — loading the half this
build understands would write the loss back on the next save.

**`material` nests the parameters** rather than putting them at the root. That
costs one indent and buys exactly one thing: `version` can never collide with a
parameter name. A flat document breaks the day somebody adds a `version`
parameter, and it breaks by silently reading the schema number as the parameter.

## Where a material comes from

**Nothing in the reader knows what a parameter is.** Save walks the reflected
properties of `hp::Material` and asks the leaf layer to write each one; load does
the reverse. Adding a parameter is one line in `registerMaterialTypes` and no
change to this format, to the reader, or to any file already written:

```cpp
reflect<Material>("Material")
    .property<&Material::roughness>("roughness")
    // ...
```

That is D23's rule applied to a second file type, and it is why there is no
per-field code in `Material.cpp` at all.

## The parameters

| Key | Default | Meaning |
|---|---|---|
| `baseColour` | `[1, 1, 1, 1]` | Linear RGBA, multiplied into `baseColourTexture` |
| `emissive` | `[0, 0, 0]` | Linear RGB added on top of shading |
| `metallic` | `1` | Multiplied into the **blue** channel of `metallicRoughnessTexture` |
| `roughness` | `1` | Multiplied into the **green** channel of the same texture |
| `normalScale` | `1` | How strongly `normalTexture` applies. 0 flattens it |
| `occlusionStrength` | `1` | How strongly `occlusionTexture` darkens ambient light |
| `alphaMode` | `Opaque` | `Opaque`, `Mask` or `Blend` — **by name**. Also decides which half of the frame the surface is submitted in, and whether its shader may read the screen (T0147) |
| `alphaCutoff` | `0.5` | The threshold `Mask` compares against; ignored otherwise |
| `doubleSided` | `false` | Whether back faces are drawn |
| `unlit` | `false` | Whether lighting is skipped and `baseColour` shown directly |
| `heightScale` | `0.04` | How deep `heightTexture`'s parallax reads, in texture-space units — UV0 units normally, projection units under `triplanar` (T0141.7, T0156) |
| `triplanar` | `false` | Project textures from world space; needs no UVs. Composes with `heightTexture`'s parallax, marched per projection (T0156). Normal map ignored while set (T0141.8) |
| `triplanarScale` | `1` | Texture tiles per metre when `triplanar` is set |
| `*Texture` | unset | A `TextureAsset`'s GUID, or absent for none |
| `uv0`, `uv1` | identity | Per-channel scale, offset, rotation and wrap |
| `*Uv` | `0` | Which UV channel that texture slot samples with |
| `shader` | unset | A `ShaderAsset`'s GUID — the module this surface is shaded by (T0142.15) |
| `params` | empty | Values for the parameters that module declares (T0160) |
| `textures` | empty | Textures for the slots that module uses (T0160) |

**The factors default to 1, not to 0**, and that is not a style choice: a
material that sets a texture and nothing else must come out as the texture says,
and a factor of 1 is what leaves it alone. A default of 0 would make every
textured material black and non-metallic.

**`emissive` defaults to black** for the mirror-image reason — it is *added*, so
any other default makes every material in the engine glow.

### Texture slots, and the ones that are not here

Five slots, and Diligent defines seventeen. What is missing is missing on
purpose:

| Slot | Here | Why |
|---|---|---|
| base colour, metallic-roughness, normal, occlusion, emissive | **yes** | The metallic-roughness set the renderer binds today |
| height | **yes** | **The engine's own slot, not one of Diligent's** (T0141.7): parallax occlusion in the surface stage. Samples and displaces UV0 |
| clearcoat ×3, sheen ×2, anisotropy, iridescence ×2, transmission, thickness | no | **Extended materials, off by D24.** Each widens the PSO permutation space *and* the material attribs buffer whether or not anything uses it |
| diffuse, specular-glossiness | no | The legacy spec-gloss workflow. glTF 2.0 core is metallic-roughness, and supporting both is two shading paths for one result |

**Metallic and roughness are one texture, not two.** That is glTF's packing —
roughness in green, metallic in blue — not a simplification here. A DCC tool that
exports them as separate images needs them packed at import (T0023's business);
two slots would have to be combined before they could be sampled anyway.

**There is no displacement or height slot, and Diligent has no path for one.**
`PBR_Renderer` implements neither parallax-occlusion mapping nor tessellation, so
a `displacementTexture` key would be a field nothing reads — which is the mistake
`Camera::cullingMask` spent three tickets being.

This is not a gap in the format, it is a gap in the *shader*: parallax needs a
hook before texture sampling and `RenderPBR.psh` has none. **T0141** owns it —
141.7 parallax and height, 141.8 triplanar, 141.9 tessellation. When the surface
stage lands, a height slot is one line here and no format change, because reading
is lenient and every file already written stays valid.

### UV channels: two of them, transformed per channel, selected per slot

`PBR_Renderer` carries exactly two texture coordinate sets —
`PSO_FLAG_USE_TEXCOORD0` and `USE_TEXCOORD1` — and its `SelectUV` picks between
them per texture. This format exposes that as **two channels with a transform
each, and a selector on each slot**:

```yaml
  uv0:
    scale: [8, 8]        # tiling
  uv1:
    wrapU: Clamp         # a unique unwrap, so clamping is right
    wrapV: Clamp
  occlusionUv: 1         # baked AO uses the unique unwrap
```

That is Godot's shape rather than Diligent's. `TextureShaderAttribs` carries an
independent transform on all 17 slots, and exposing it directly would mean five
copies of the same four numbers in the ordinary case, where every map on a
surface shares one tiling. The renderer writes a channel's settings into each
slot that selects it, which is one loop and recovers the general case if
anything ever needs it.

**Rotation is in radians**, matching every other angle in the engine
(`Light::innerConeAngle`, `Camera::verticalFov`) rather than inventing a second
unit for one file format.

**Wrap defaults to `Repeat`**, because a surface texture tiles. `Clamp` is what
a lightmap, a decal or an atlased texture wants — tiling one of those bleeds a
neighbour's pixels across the seam.

A mesh carrying only one UV set makes `uv1` inert: `SelectUV` falls back to
whichever set exists rather than sampling garbage.

## The extended features (T0143, amending D24)

Everything DiligentFX's PBR can shade is authorable here — the glTF extension
set, factors and textures both. **A feature is "in use" when its factor is
non-zero or it names a texture**, and only then does the material pay a PSO
permutation for it; every key below at its default costs nothing, which is why
they exist on every material rather than behind a mode switch.

| Key | Default | Meaning |
|---|---|---|
| `clearcoat` | `0` | Coat layer strength — `KHR_materials_clearcoat`. A second specular lobe over everything, dimming what is underneath by its Fresnel |
| `clearcoatRoughness` | `0` | The coat's own roughness, multiplied into `clearcoatRoughnessTexture`'s green channel |
| `clearcoatNormalScale` | `1` | How strongly `clearcoatNormalTexture` applies. The coat does **not** inherit the base normal map — a coat with no map of its own is smooth, per the extension |
| `sheenColour` | `[0, 0, 0]` | Sheen colour, linear RGB — `KHR_materials_sheen`. Black is off |
| `sheenRoughness` | `0` | Multiplied into `sheenRoughnessTexture`'s **alpha** |
| `anisotropyStrength` | `0` | Highlight stretch — `KHR_materials_anisotropy`. Meshes using it should carry TANGENT attributes |
| `anisotropyRotation` | `0` | Rotates the stretch direction, radians |
| `iridescence` | `0` | Thin-film factor — `KHR_materials_iridescence`. Bends the base F0, so it is visible under a plain lamp |
| `iridescenceIor` | `1.3` | The film's index of refraction — the extension's own default |
| `iridescenceThicknessMin` / `Max` | `100` / `400` | Film thickness range in **nanometres**; the thickness texture's green channel lerps between them |
| `transmission` | `0` | `KHR_materials_transmission`. Removes diffuse; on a `Blend` material the scene behind shows through at `1 - transmission` |
| `ior` | `1.5` | `KHR_materials_ior`. Carried into the material buffer; its consumer is T0087's refraction |
| `thickness` | `0` | `KHR_materials_volume`, metres. **Wired and debug-visible, shades nothing until T0087** — its consumer is image-based refraction |
| `attenuationColour` | `[1, 1, 1]` | What colour survives the volume. T0087's consumer, like `thickness` |
| `attenuationDistance` | `0` | Metres; **0 means unlimited**, the engine's convention, converted to the extension's +infinity on the way to the GPU |
| `clearcoatTexture` … `thicknessTexture` | unset | The ten extension texture slots, GUIDs like every other slot; each has its `*Uv` selector |

**What each shows without an environment differs, and the shader contract
states it per field** (`docs/shaders/`): clearcoat, sheen, anisotropy and
iridescence shape the response to punctual lights; transmission is a
blend-pass composite; the volume family waits for T0087. The debug views
(`SurfaceDebugView`, one per channel) are wired for all of them today.

## `params` and `textures`: the half of a material the engine did not name (T0160, T0161)

Every parameter above is the **engine's**. Its name means the same thing on
every material in every game, and that vocabulary cannot grow to fit a technique
the engine has never heard of. Before T0160 a game shader's own knobs had
nowhere to live at all — the first material shader written in this repository,
`samples/rockcube/content/shaders/rock_pom.slang`, hard-coded its one parameter
as a compile-time literal and said so in its own comment.

A module declares its parameters **and its textures** in Slang, under its own
names and at whatever count it wants (T0161, D35), and a `.hpmat` carries
values and bindings for them **by those names**:

```slang
// in the game's own .slang module
Texture2DArray detailAlbedo;      // the author's name -- no registration

cbuffer HpMaterialParams
{
    [HpRange(0.0, 1.0)]
    [HpTooltip("Which height sits on the polygon plane.")]
    float referencePlane;

    [HpColor]
    float3 tint;
}
```

```yaml
version: 2
material:
  shader: 1570000000000020
  params:
    - name: referencePlane
      value: 0.5
    - name: tint
      value: [0.8, 0.7, 0.6]
  textures:
    - name: detailAlbedo
      texture: 1570000000000031
```

**Nothing in this file knows what those names mean.** They belong to the module;
this is a name/value store, and a name the module does not declare is dropped
when the material reaches the GPU — the same leniency a parameter this build
does not have gets.

### Five rules worth knowing before authoring one

**A scalar stays a scalar.** `value: 0.5` and `value: [0.5, 0, 0, 0]` are
different documents and both round-trip to themselves, so saving a hand-written
material never rewrites a number into three components it did not have. The
*shader's* declared type decides how many floats reach the GPU: a `float3` given
`value: 0.5` is written as `(0.5, 0, 0)`, never as one float running into the
next parameter's bytes.

**A parameter the material does not set reads zero**, not the previous draw's
value and not undefined memory.

**Changing a value rebuilds no pipeline and invalidates no cook.** Values are
written into a constant buffer per draw, exactly as `heightScale` is. Module
*identity* already keys the pipeline cache, so parameters add no permutation
axis — which is the reason to prefer a parameter over a `#define` whenever the
choice exists, and it is recorded against T0151.

**Texture names are the author's** (T0161). The engine reflects the compiled
module and builds a per-module resource signature from what it finds, so a
`textures:` entry names whatever the module declared — `detailAlbedo`, not an
engine identifier. Declare them `Texture2DArray` (the one shape the engine
binds; anything else is refused by name at pipeline build) and sample through
the engine's palette — `HpSamplerLinearWrap`, `HpSamplerLinearClamp`,
`HpSamplerPointWrap`, `HpSamplerPointClamp`, `HpSamplerAnisoWrap`,
`HpSamplerAnisoClamp` — because sampler *state* is the engine's vocabulary
(D35's one deliberate limit) and a module declaring its own `SamplerState` is
refused by name with the palette in the log. `HpMaterialParams` stays the one
reserved name: the *block* is the engine's so these values have one place to
go; the fields inside it are the author's.

T0160's `HpTexture0` … `HpTexture3` still bind — they are deprecated
declarations in the contract file now, ordinary author-visible names — so a
shipped v2 `.hpmat` renders unchanged.

A texture the material does not name samples **white** — the same no-texture
answer every engine slot gives — and a GUID that does not resolve samples the
missing-asset checkerboard.

**Only what the engine can carry**: `float`, `float2`, `float3`, `float4`, `int`
and `bool`, and 256 bytes of block in total. A matrix, an array or a nested
struct in the block is reported by name in the log and left at whatever the
shader initialises it to; a module whose block overruns the cap is refused by
name rather than truncated.

### `version: 2`

The schema bumped when these two keys arrived. **Every version-1 `.hpmat` still
loads** — reading is lenient and both keys are simply absent from them. The bump
is the signal in the other direction: a build predating T0160 refuses a document
that might carry parameters it would drop on the next save.

### Strengths are 0..1, not 0..100

Every factor here is in **shader units**, which is 0..1 for the bounded ones —
they are multiplied straight into a texture sample or a BRDF term. Storing 0..100
would mean a conversion at exactly one edge, and a conversion at one edge is a
bug the first time someone writes a material by hand and it comes out a hundred
times too bright.

A **percentage is a presentation choice**, and the inspector is where it belongs:
`PropertyMeta` already carries `min`, `max` and a tooltip per property (T0053),
so a slider can read 0–100% while the file stays in the units the shader wants.

Three of these are deliberately **not** bounded at 1, and clamping them would be
wrong:

- **`emissive`** is HDR. Values above 1 are the entire point once T0096's
  tonemapping exists.
- **`normalScale`** above 1 exaggerates a normal map, which is a legitimate and
  common authoring move.
- **`baseColour`** *is* bounded to 0..1 — it is a reflectance, and a surface that
  reflects more light than falls on it is not a material, it is a bug.

### Normal maps are OpenGL-convention — green is up (T0168.5)

**Author NormalGL: `+Y` (green above 0.5) means the bump faces *up* the
image.** That is glTF's own rule — "the normal vectors use OpenGL conventions
where +X is right and +Y is up" — and since 2026-08-08 it is what the engine
actually does, pinned by `tangent_frame_test.cpp`'s lit case. A DirectX-style
map (green down; Unreal's default export, some Substance presets) shades
concave-for-convex in exactly the way that reads as an authoring mistake, so
if a bump looks *pressed in* under a light from above, flip the map's green
channel before touching anything else.

`tools/pack_test_textures.py` has always packed NormalGL and said so; until
T0168.5 the engine applied every map with the green channel inverted — the
construction it inherited builds its bitangent as `cross(t, n)`, which points
*down* a glTF chart's image. The fix rides `HpTangentFrameGrad`, which also
makes the frame follow a **mirrored UV shell** (standard game-art practice),
so a symmetric model's mirrored half now bumps the same way as its authored
half. A future per-asset "flip green" import setting belongs to T0097's
texture pipeline; until then the convention is fixed and this section is where
an artist finds it.

### `alphaMode` is written by name

`alphaMode: Blend`, never `alphaMode: 2`. A number silently means something else
the moment a value is inserted into the middle of the enum, and this is the
parameter most likely to be hand-edited. A number is still *read*, so nothing
already written breaks; a name this build does not have leaves the field at its
current value and says so.

This is the general rule for every reflected enum — see the scene format
document, which has the same section and the defect that motivated it.

### `alphaMode` decides more than blending (T0147, D37)

Since the frame gained its opaque/blend split, this field also decides **when**
a surface is drawn and **what its shader may reach**:

- `Opaque` and `Mask` are submitted at **10.9a**, before the scene snapshot.
- `Blend` is submitted at **10.9c**, after it — and is therefore the **only**
  alpha mode whose shader may sample `g_SceneColour` or `g_SceneDepth`. A module
  that reads either from an `Opaque` or `Mask` material does not get a pipeline
  at all: the console names the module and the rule, and the surface renders the
  missing-material checkerboard.

So a refraction, a heat haze, a frosted pane or a soft particle is
`alphaMode: Blend` in this file *and* a screen read in its `.slang` — neither
alone. Setting one and not the other is the mistake the loud refusal exists to
name.

## Reading is lenient; writing is exact

Identical to scenes, and for the same reason — files outlive the code that wrote
them:

- **A parameter absent from the document keeps its default**, so a material can
  gain one without invalidating a single file.
- **A parameter this build does not have is ignored**, so a material can lose
  one without a migration. A `clearcoatFactor` from a future build is skipped,
  not fatal.
- **A malformed parameter leaves its target alone** and is reported, rather than
  being reinterpreted. `roughness: not-a-number` keeps the default and does not
  poison the parameters after it.

Saving normalises: every parameter is written every time, so a hand-authored
three-line file becomes the full document the first time it is saved, and is
byte-stable from then on.

## Identity, and the missing material

A material is an asset like any other (T0023): its GUID lives in the
`.hpmat.hpmeta` metafile beside it, **not** inside the document. So renaming or
moving a `.hpmat` keeps its identity as long as the metafile travels with it, and
copying one to a new name and importing it produces a *new* material rather than
two files claiming the same GUID.

A `MeshRenderer` slot naming a GUID that is **not in the pool or failed to load**
renders the magenta-and-black checkerboard (60.10). A slot holding a **default**
GUID is not an error and never does: it means "use what the model imported",
which is the normal state of most surfaces. Conflating those two is what would
make every unassigned mesh look broken — see T0060 for the three-state table.

## The binary form

Materials cook through the same length-prefixed property format everything
reflected does (`cookProperties`), so the binary path carries every parameter the
YAML path does. Enums cook as their **number**, deliberately: a cook is a cache
keyed on a hash of its source, never read by a person, and an enumerator rename
invalidates it rather than being misread from it.

## What is not here yet

- ~~**Custom shader materials**~~ — landed. The `shader` key arrived with
  T0142.15 and its declared parameters with **T0160**; both are additive, and a
  material without either is what the rest of this document describes.
- ~~**Author-chosen texture slot names.**~~ — landed (**T0161**, D35). A module
  declares `Texture2DArray detailMap;` and the `.hpmat` binds `detailMap`. The
  second descriptor set T0160 weighed and deferred is exactly what shipped,
  after 161.1 measured its cost at 34 ns a draw; the rename pass stayed
  rejected as circular. `HpTexture0` … `3` remain as deprecated declarations.
- **Author-defined sampler state.** Filtering comes from the engine's
  six-entry palette; a custom aniso level, border colour or compare sampler is
  metadata a cooked build would have to carry, and waits for a real request
  (D35).
- **A parameter's default.** A module's block is zero-initialised, so a
  parameter no material sets reads zero rather than something the shader
  declared as sensible. `[HpDefault(...)]` is the obvious shape and nothing
  needs it yet.
- **Material instances** — see below.
- **Per-slot UV transforms.** Exposed per *channel*, which covers the ordinary
  case; a slot needing its own transform independent of the channel it selects
  has nowhere to say so yet. The plumbing underneath is per slot, so this is an
  addition rather than a rework.
- **Colour-space conversion per texture.** Inherited from T0028: `GetPBRTextureSRV`
  is not public, so textures bind through the model's own views with no
  conversion. Sits with T0097's sRGB work.
- **`SpecularFactor`.** Present in `PBRMaterialShaderAttribs` and read **only**
  under `PBR_WORKFLOW_SPECULAR_GLOSSINESS` (`RenderPBR.psh:159`), which this
  engine does not use. Absent on purpose, not forgotten.
- **Height, parallax occlusion, triplanar and tessellation** — **T0141.7/141.8/141.9**.
  These are not material parameters at all: they need a hook *before* texture
  sampling, and `PBR_Renderer` has none. See T0141 for the finding that
  DiligentFX's lighting is a reusable public library even though its material
  shader is not hookable, which is what makes owning the surface stage cheap.
- **Material instances** — a material overriding a few parameters of a parent.
  Worth having and not yet designed; it belongs with T0141's parameter work
  rather than being retrofitted onto this.
