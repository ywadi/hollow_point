# T0143 — Everything DiligentFX's PBR has, and the ability to extend it

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 425 |
| **Created** | 2026-08-06 |
| **Refs** | **Amends D24**; D26, D27, **D28** (authored in Slang); [T0142](../completed/0142-slang-shader-language.md) — this is written in Slang and waits for it; [T0141](../completed/0141-custom-shader-materials.md) — the surface stage this plugs into; [T0060](../completed/0060-material-system.md) — `hp::Material` gains the authoring fields; T0087 — several of these are only *visible* with environment lighting; [0145-lighting-stage-own-the-light-loop.md](../completed/0145-lighting-stage-own-the-light-loop.md) — the sheen/clearcoat/anisotropy terms 143.3 fills accumulate *inside* the loop T0145 mirrors, and each feature's fields join the D31 mirror; [0149-style-bundles.md](0149-style-bundles.md) — the "ultra realistic" style is largely this ticket's features switched on (149.4); [0152-winding-convention.md](../completed/0152-winding-convention.md) — **T0152's engine half landed first** (D33): each feature's pixel test written before the assets were re-wound would have been calibrated against the inverted two-sided flip; **[T0145](../completed/0145-lighting-stage-own-the-light-loop.md) mirrored the base layer only, and `#error`s on every extended feature rather than dropping it.** `ENABLE_SHEEN`, `ENABLE_CLEAR_COAT`, `ENABLE_ANISOTROPY`, `ENABLE_IRIDESCENCE`, `ENABLE_TRANSMISSION` and `ENABLE_VOLUME` each fail the build in `engine/shaders/HpSurface.slang`'s lighting stage, so this ticket has a named place to write and cannot enable a flag whose path silently does nothing. Two constraints from D31: the sheen/clearcoat/anisotropy fields go on **`HpShadedSurface`**, present and zeroed when the feature is off, never behind an `#if` — a contract struct that changes shape per permutation is the exact hazard mirroring exists to close; and the accumulation grows `HpLightResponse` or a second layer accumulator, matching `ApplyPunctualLight`'s sheen/clearcoat blocks, which `tests/fixtures/upstream_shading.pinned` holds verbatim for the diff |

## Why

**The owner's requirement, in their words:** *"we should have what the PBR
Diligent have ++ not less"*, and *"I want us to have all what they have and have
the ability to add to it (modify to it for custom too)"*.

**This amends D24.** D24 turned clearcoat, sheen, anisotropy, iridescence,
transmission and volume **off**, and D26 defended that as *"the subset is D24's,
not a simplification"*. That was scoped when there was no surface stage to put
them in. There is one now, and an engine that is *less* capable than the library
it is built on is the wrong trade for a studio shipping several games.

**The cost collapsed on 2026-08-06.** The engine used to plan its own texture
sampling, which would have meant writing every extended slot by hand. It now
`#include`s `PBR_Textures.fxh` and calls their getters — and **all fifteen are
already included and callable**: `GetClearcoatFactor`, `GetClearcoatRoughness`,
`GetClearcoatNormal`, `GetSheenColor`, `GetSheenRoughness`, `GetAnisotropy`,
`GetIridescence`, `GetIridescenceThickness`, `GetTransmission`,
`GetVolumeThickness`. The lighting side is public in `PBR_Shading.fxh` the same
way. So this is **wiring, not new BRDF maths**, and no part of it should be
reimplemented.

## Done when

- [x] Every feature DiligentFX's PBR supports is reachable from a `.hpmat`.
      *(2026-08-07 — factors, textures and UV selectors for all six; schema
      version 2 → 3)*
- [x] A game's Slang material can **read and write** each one — not just receive
      it. Parity without extensibility is half the requirement. *(2026-08-07 —
      ten `IHpMaterial` channel hooks with getter defaults, ten
      `HpSurfaceOutput` fields writable from any hook, and the engine derives
      the lit values — the anisotropy frame, the coat F0, iridescence's F0
      bend — **after** `surface()` so an overridden value is the value lit)*
- [x] Each feature has a debug view and a test that fails if it is wired wrong.
      *(2026-08-07 — eleven `SurfaceDebugView` channels in upstream's
      numbering; `extended_material_test` asserts each feature's shaded
      delta AND its debug channel, with the control's channel asserted black)*

## Subtasks

- [x] 143.1 **Turn the features on** *(2026-08-07)* — all six in
      `SurfacePipeline::configure`. The three-switches rule held, and a fourth
      switch surfaced: **`EnableSheen` demands a LUT upstream ships only as a
      GLTFViewer sample asset** loaded from a native path. Answer: the bytes
      are embedded from the pinned submodule at build time
      (`cmake/hp_embed_binary.cmake`, the `HLSLDefinitions.fxh` pattern),
      decoded by the importer's own `CreateTextureLoaderFromMemory`, and
      assigned to the protected `m_pSheenAlbedoScaling_LUT_SRV` before
      `CreateSignature()` runs the base's static-variable binding. One
      landmine recorded in the code: a `DILIGENT_DEBUG` third-party build
      would hit the base ctor's UNEXPECTED before that assignment; this tree
      pins release Diligent everywhere.
- [x] 143.2 **Map the remaining texture slots** *(2026-08-07)* — all ten, in
      the loader's own constants.
- [x] 143.3 **Fill each `SurfaceShadingInfo` sub-struct** *(2026-08-07)* — in
      the engine's mirrored shape rather than upstream's structs (D31):
      `HpShadedSurface` gained the flattened fields, `evaluateSurface` mirrors
      `Read*Properties` for the derivations, `HpStandardLight` gained the
      anisotropic-BRDF branch and the transmission diffuse dim, the default
      `lighting()` loop gained the sheen and clearcoat layer accumulators
      (LUT albedo scaling included), and `HpResolveLighting` gained a layered
      overload — the two-argument form is unchanged so a pre-T0143 rung-4
      override compiles and behaves identically. Every mirrored block is
      pinned: the drift fixture went 6 → **16 functions**.
- [x] 143.4 **Grow `HpSurfaceOutput` / `IHpMaterial`** *(2026-08-07)* — the
      ten fields plus `AnisotropyDirection`, all present and zeroed in every
      permutation (D31), and ten `[mutating]` channel hooks whose defaults are
      DiligentFX's getters.
- [x] 143.5 **Add the fields to `hp::Material`** *(2026-08-07)* — 16 factors,
      10 textures, 10 UV selectors, reflected one line each;
      `11-material-format.md` gained the section; schema bumped to 3 with the
      same leniency rule as the 2 bump.
- [x] 143.6 **A debug view per feature** *(2026-08-07)* — eleven channels
      (`ClearCoatFactor` 21 … `Thickness` 33, DiligentFX's numbering, gaps
      theirs), each reading a field that exists in every permutation, so a
      feature that is off debugs as flat black rather than as a missing mode.
- [x] 143.7 **Test assets that actually exercise them** *(2026-08-07)* —
      `tests/gpu/extended_material_test.cpp`: an authored material per
      feature against a shared punctual control, measured values in the
      findings below.
- [x] 143.8 **Measure the permutation count** *(2026-08-07 — measured, and
      registered at T0151; numbers in the findings below)*.
- [x] 143.9 **Decide `EnableIBL` and `EnableShadows` deliberately**
      *(2026-08-07 — both stay **off**, and it is a decision rather than
      drift: `EnableShadows` is T0086's and its `#error` shape in `HpGetLight`
      is untouched; `EnableIBL` is T0087's, its per-layer resolve seam is now
      wider (sheen and clearcoat IBL terms join their own layers), and
      turning it on beside sheen demands the Charlie LUT — the same
      sample-asset problem 143.1 solved, with the embed pattern named in
      T0087's Refs as the answer. The environment-visibility caveats are
      stated per claim below rather than hidden)*.

## Notes / findings

### Turning one on has three switches, not one

Recorded because the engine already lost a full ticket to it. `EnableAO` was
false while the AO map was **loaded, packed and bound** — `USE_AO_MAP` was 0, so
`GetOcclusion` was never compiled in, and `Occlusion` sat at the material factor.
Flat white channel, zero variation, and completely invisible in a shaded frame.

Separately, `TextureAttribIndices` defaulted to all `-1` and silently disabled
three subsystems at once: no attrib-id macros, no bindings, no per-texture
attributes written. It surfaced only as a shader that would not compile.

**So for each feature: the `CreateInfo` flag, the PSO flag, and the texture
index. And a debug view to prove it.**

### Metal renders near-black without an environment

Measured: centre (20, 19, 19). A metal has no diffuse response, so with
`EnableIBL = false` there is nothing for it to reflect. The shaded frame looks
identical whether metalness is wired to blue, to green, or to nothing — which is
why the metalness check lives in the debug-channel test and not the shaded one.
The same trap applies to clearcoat and sheen, and worse.

### Measured 2026-08-07 — every feature against one punctual control, and what each claim is worth

RTX 2080, Vulkan, both targets. The control is a red rough dielectric under
one white directional lamp, through the assigned-material path: **(242, 25,
25)** — exactly the lit-surface baseline, which is what attributes every
delta below to the feature and not the scene.

| feature | authored | shaded centre | the honest claim |
|---|---|---|---|
| clearcoat | 1.0, roughness 0.3 | (252, **136**, 136) | the coat's achromatic GGX lobe and Fresnel dim exist and are large; **that it reads as lacquer is an environment claim, not made** |
| sheen | white, roughness 0.5 | (218, 21, 21) | the albedo-scaling energy trade through the **embedded LUT** (this case is also the LUT's binding proof); **the velvet rim needs grazing angles and T0087** |
| anisotropy | strength 1 on roughness 0.5 | (244, 50, 50) vs isotropic (253, 93, 93) | the lobe reshapes (alphaT 0.25 → 1); **the stretched-highlight look needs curvature and an environment** |
| iridescence | 1.0 at 400 nm | (248, **129**, **93**) | **a real punctual validation, unqualified**: the thin film makes the white lamp's specular chromatic — g and b split by 36 where every non-iridescent frame holds g == b |
| transmission | 1.0, alphaMode Blend | (0, 0, 255) | the diffuse is gone and the clear colour composites through at `alpha = 1 - T`; **the refractive version is T0087's** |
| volume | thickness 0.5 | (242, 25, 25) — **identical to control** | wired, debug-visible (view reads sRGB(0.5) = 188), and deliberately shades nothing; the test pins the no-op so T0087 flips it consciously |

Every feature's debug channel asserted its authored value and the control's
channel asserted black — the occlusion lesson, executed six more times.

### 143.8, measured: the multiplier is per-material data, and one cost is always paid

The ×64 worst case exists only if one scene authors every feature
combination. `extendedMaterialFlags` raises a feature's PSO bits only from
the drawn material's own extension blocks, so the whole gpu suite gained
exactly **6 pixel-shader permutations** — one per feature its test exercises
— and a material using none keys its pre-T0143 pipeline: the
frame-byte-identity guard held at **0 differing bytes**, and every recorded
baseline in the suite (parallax `no-height vs zero-scale: 0` ×3 included) is
unmoved. The USE-map bits ride the ENABLE bits (upstream's own policy), so
ten potential axes collapse into the six.

**The cost that is always paid**: the channel hooks compile into every
permutation as zero-valued calls slang does not fold — the standard `psMain`
family grew ~20.5–21.1 KB → ~24.5–25.2 KB of SPIR-V, with pixel output
identical. Registered at T0151 (its link-time-constants probe is the named
fix for exactly this class), together with the signature growth: 8 → **19**
sampled images, 13 → **19** immutable samplers, measured at creation and
pinned by `custom_shader_material_test` — both Vulkan guaranteed floors
crossed knowingly, desktop is far above them, and `ShaderTexturesArrayMode`
is the recorded lever if a floor-level device ever matters.

### A module-authored constant feature lights nothing, and that is data's job

A game module can `override clearcoat()` to return 1 with no material data —
the value flows to `HpSurfaceOutput`, shows in the debug view, and **does not
light**, because the lighting blocks compile only into permutations whose
*material* carries the feature (the PSO bits come from data, not code). This
is D38's `receiveShadows` shape again: the material datum is the switch. A
module wanting a coat everywhere sets `clearcoat: 1` in its `.hpmat`, which
is one line and keys the permutation honestly.

### Not verified here, stated rather than implied

- **No feature combination was exercised** — each test material carries one
  feature. The flag plumbing is combination-agnostic by construction
  (upstream's own bit set), but no pixel test pins, say, clearcoat over
  sheen.
- **Imported glTF extension materials** (a `.gltf` carrying
  `KHR_materials_clearcoat`) flow through the same `extendedMaterialFlags`
  and the same loader blocks, but no gpu test imports one — the authored
  path is what T0143 makes real, and the import path shares every line of it
  past the loader.
- **The editor inspector** shows none of the new fields specially — T0035's
  reflected inspector will pick them up as it does every reflected field.
