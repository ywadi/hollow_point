# Test assets

Source material for the gpu bucket's visual and regression tests (T0141.11).

## `derived/` — committed, and the only thing tests read

Eight 512x512 PNGs, packed and downscaled from the 2K originals by
`tools/pack_test_textures.py`. **2.5 MB total, and that is the point**: a
regression guard's job is to catch a garbage `MipBias` and a broken tangent
basis, and 512 does that as well as 2048 while keeping the repository and every
CI checkout small. The full-size originals stay local for looking at.

| File | Contents |
|---|---|
| `*_basecolour.png` | Linear RGB albedo |
| `*_normal.png` | Tangent-space normal, **OpenGL convention** |
| `*_orm.png` | Occlusion in **red**, roughness in **green**, metallic in **blue** |
| `*_height.png` | Displacement, for parallax (T0141.7) |

**`rock` is a dielectric** — its ORM blue channel is zero — and **`metal` is
not**, which is why both exist: a texture set that is entirely non-metallic
cannot catch a metallic term wired to the wrong channel.

### Two conventions worth knowing, because getting either wrong is silent

**NormalGL, never NormalDX.** Both sets ship both. glTF specifies the OpenGL
convention, where green is +Y. The DirectX map has green inverted, and the result
is not an obvious failure — it is lighting that looks subtly wrong, as though lit
from the opposite vertical direction. `NormalDX` is deliberately not packed.

**Occlusion, roughness and metallic are one texture**, glTF's ORM packing, not
three. AmbientCG ships them as three separate greyscale files, so they are
combined here. The engine has **no importer that does this yet** — that is
T0023's, and this directory is the first concrete instance of the gap the
material format document describes in the abstract.

The two sets are asymmetric on purpose and it reflects the material: rock has an
occlusion map and no metalness, metal has metalness and no occlusion. The packer
substitutes white occlusion and black metallic respectively, which is what those
absences mean.

## `textures/` — the 2K originals, **not committed**

Ignored by git. 26 MB of JPEG that would sit in history permanently and cannot be
removed later without a rewrite, in exchange for detail no assertion reads.

Source: [ambientCG](https://ambientcg.com) — **CC0**, so there is no licence
obstacle to committing the derived set. Recorded here rather than assumed,
because "where did this come from" is a question with no answer in two years'
time otherwise.
