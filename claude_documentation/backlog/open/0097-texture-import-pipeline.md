# T0097 — Texture import pipeline: mips, compression, colour space

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 7 — Content pipeline |
| **Order** | 710 |
| **Created** | 2026-08-03 |
| **Refs** | T0023, T0038, T0096, T0134, T0141; [../completed/0169-import-produces-engine-assets.md](../completed/0169-import-produces-engine-assets.md) — **the extraction/processing boundary is drawn and this ticket owns the processing side** (D39): `produceEngineAssets` writes embedded images **bytes-verbatim** and never re-encodes; colour-space flags (`loadTexture` hardcodes sRGB today — a normal map wants linear), compression, mips, and the spec-gloss→MR texel conversion the extraction warns about are all this ticket's, as per-import settings on the sidecar metafile |

**From T0141 (2026-08-06): the sRGB decode currently runs in the pixel shader,
and this ticket may move it.** The glTF loader creates `RGBA8_TYPELESS` textures
whose default shader view is `RGBA8_UNORM` — linear — so an sRGB-authored albedo
read through it is too bright. The free fix is an sRGB *texture view*, which is
what DiligentFX's `GetPBRTextureSRV` creates and which is **file-static in their
.cpp**, invisible outside that translation unit. So `SurfacePipeline::configure`
sets `TexColorConversionMode = SRGB_TO_LINEAR` and `HpSurface.psh` applies
`TO_LINEAR` per colour sample instead. **If this ticket gives the engine its own
texture import, create the sRGB view there and set the mode back to `NONE`** —
the shader needs no change, because `TO_LINEAR` compiles to nothing in that
mode.

## Why

Meshes get a full import pipeline — conversion (T0038), LOD generation (T0039),
bounding volumes (T0045) — but **textures have none**. No ticket decides mip
generation, GPU compression, or colour-space tagging at import. Uncompressed
PNGs uploaded raw cost 4-8× the GPU memory and bandwidth of BC-compressed
textures and load slower; a wrong sRGB flag silently breaks PBR shading
(T0096) in the way that gets compensated by hand-tuning lights and then has to
be un-compensated later.

Verified before filing, per the check-Diligent-first rule: **Diligent already
provides the machinery.** `DiligentTools/TextureLoader` has mip generation
(`GenerateMips`), BC compression (`TEXTURE_LOAD_COMPRESS_MODE`, `BCTools.h`)
and DDS writing. So this is wiring Diligent's existing compressor into the
import path and the metafile — not building a compressor.

## Done when

- [ ] Import generates mips and BC-compresses appropriate textures, cached as
      cooked DDS under the same cache rules as T0020's binary cook: rebuilt
      when the source changes, safely discardable, never the source of truth
- [ ] Per-asset import settings live in the metafile (T0023): sRGB flag,
      compression mode, mip policy — with **defaults by usage**: albedo
      sRGB + compressed, normal maps linear + a deliberate format choice,
      data/masks linear
- [ ] Runtime and export load the cooked form; export ships no raw source
      images unless uncompressed was explicitly chosen
- [ ] Normal maps survive compression visibly intact — compressed normals are
      a classic artefact source, so this is a visual check on a real mesh, not
      an assumption
- [ ] The assets panel (T0036) shows format and compressed size, so the effect
      of the settings is visible

## Subtasks

- [ ] 97.1 Enumerate what `TEXTURE_LOAD_COMPRESS_MODE` / `BCTools.h` actually
      support (BC1/3/4/5/7?) before promising formats — the normal-map answer
      depends on it
- [ ] 97.2 Hook compression + mips into `ImportAsset` (T0023), writing the
      cooked DDS beside the metafile
- [ ] 97.3 Metafile settings and defaults-by-usage; the sRGB flag must flow
      through to the view format (T0096's policy)
- [ ] 97.4 Check how glTF-embedded images arrive via `GLTFLoader` /
      `GLTFResourceManager` — decide where the cooked cache plugs into that
      path, since glTF textures may bypass a naive per-file import hook
- [ ] 97.5 Async cook on the job system (T0026) so importing a folder of
      textures does not freeze the editor
- [ ] 97.6 Report before/after sizes at import, like T0039 reports triangle
      counts

## Notes / findings

### Where 97.3 starts: sRGB is hardcoded on, today (2026-08-05, from T0023)

T0023 closed with texture import working and **sRGB forced on for every imported
texture** — `engine/src/AssetImport.cpp`, `loadInfo.IsSRGB = true`, one line with
a comment saying an imported image "almost always" is colour. That is right for
albedo and wrong for every normal map and mask, and the symptom is a lighting bug
that gets compensated by hand-tuning lights and then has to be un-compensated.

So 97.3 is not greenfield: it is replacing a hardcoded `true` with the metafile
setting, and the metafile mechanism it needs already exists and is proven — a
second import of the same file returns the same GUID from its metafile.

T0023 now points here explicitly. It previously said only "a small ticket of its
own" without naming this one, which made a decision that *was* filed read as
open.

- The colour-space *policy* is decided in T0096 and merely recorded per asset
  here — do not let the two drift.
- KTX2/BasisU is the heavier alternative (transcodable, smaller on disk). Not
  needed for two desktop targets where BC is universally supported; note it as
  the revisit path if download size ever matters.
- ~~Texture *arrays* and atlases are out of scope~~ — **reversed by T0106.**
  Diligent has `DynamicTextureAtlas` and it is now wanted: a VFX sprite sheet
  *is* an atlas with regular spacing, and flipbook explosions are the core of
  the effect system (D15, T0106). This exclusion was written before VFX were
  considered. Texture *arrays* remain out of scope; the reversal is about
  atlases and sprite sheets only.


### Architecture amendment (2026-08-03) — VFX pulls three things into this pipeline

T0106 (VFX sprites and flipbooks) makes demands this ticket did not anticipate:

- **Sprite sheet metadata.** A flipbook needs rows, columns and frame count
  travelling with the texture. Whether that is a distinct asset type or fields
  on the texture import is T0106.1's decision, but the metafile is where it
  would live and that is this ticket's territory.
- **Premultiplied alpha.** T0106 argues for it — it avoids the dark halos
  standard alpha blending produces at sprite edges and lets one texture hold
  both glowing and soft-edged content. It is a *convention in the import
  pipeline* (multiply RGB by A at import), so if it is adopted it belongs here,
  and it is far cheaper to adopt before there is art than after.
- **Colour space for VFX textures.** The linear-workflow policy (T0096) applies,
  but effect textures are frequently authored as masks and gradients rather than
  albedo, and tagging those sRGB is a common and hard-to-spot error.
