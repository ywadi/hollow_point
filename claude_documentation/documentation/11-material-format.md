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
version: 1
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
```

And this is a complete, valid material too:

```yaml
version: 1
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
| `alphaMode` | `Opaque` | `Opaque`, `Mask` or `Blend` — **by name** |
| `alphaCutoff` | `0.5` | The threshold `Mask` compares against; ignored otherwise |
| `doubleSided` | `false` | Whether back faces are drawn |
| `unlit` | `false` | Whether lighting is skipped and `baseColour` shown directly |
| `*Texture` | unset | A `TextureAsset`'s GUID, or absent for none |

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
| clearcoat ×3, sheen ×2, anisotropy, iridescence ×2, transmission, thickness | no | **Extended materials, off by D24.** Each widens the PSO permutation space *and* the material attribs buffer whether or not anything uses it |
| diffuse, specular-glossiness | no | The legacy spec-gloss workflow. glTF 2.0 core is metallic-roughness, and supporting both is two shading paths for one result |

**Metallic and roughness are one texture, not two.** That is glTF's packing —
roughness in green, metallic in blue — not a simplification here. A DCC tool that
exports them as separate images needs them packed at import (T0023's business);
two slots would have to be combined before they could be sampled anyway.

**There is no displacement or height slot, and Diligent has no path for one.**
`PBR_Renderer` implements neither parallax-occlusion mapping nor tessellation, so
a `displacementTexture` key would be a field nothing reads — which is the mistake
`Camera::cullingMask` spent three tickets being. Height-mapped surfaces need
shader work rather than a material parameter, and that belongs with **T0141**.

### `alphaMode` is written by name

`alphaMode: Blend`, never `alphaMode: 2`. A number silently means something else
the moment a value is inserted into the middle of the enum, and this is the
parameter most likely to be hand-edited. A number is still *read*, so nothing
already written breaks; a name this build does not have leaves the field at its
current value and says so.

This is the general rule for every reflected enum — see the scene format
document, which has the same section and the defect that motivated it.

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

- **Custom shader materials** — T0141. Nothing in this format forecloses them: a
  `shader` key and its declared parameters are additive, and a material without
  one is what this document describes.
- **Per-texture sampler and colour-space settings.** `TextureShaderAttribs`
  carries UV selector, wrap modes and a UV transform per slot; none of it is
  exposed here yet, and the sRGB question is inherited from T0028 and sits with
  T0097.
- **Material instances** — a material overriding a few parameters of a parent.
  Worth having and not yet designed; it belongs with T0141's parameter work
  rather than being retrofitted onto this.
