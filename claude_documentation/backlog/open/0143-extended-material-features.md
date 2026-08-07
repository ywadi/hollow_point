# T0143 — Everything DiligentFX's PBR has, and the ability to extend it

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 425 |
| **Created** | 2026-08-06 |
| **Refs** | **Amends D24**; D26, D27, **D28** (authored in Slang); [T0142](../completed/0142-slang-shader-language.md) — this is written in Slang and waits for it; [T0141](../completed/0141-custom-shader-materials.md) — the surface stage this plugs into; [T0060](../completed/0060-material-system.md) — `hp::Material` gains the authoring fields; T0087 — several of these are only *visible* with environment lighting; [0145-lighting-stage-own-the-light-loop.md](../inprogress/0145-lighting-stage-own-the-light-loop.md) — the sheen/clearcoat/anisotropy terms 143.3 fills accumulate *inside* the loop T0145 mirrors, and each feature's fields join the D31 mirror; [0149-style-bundles.md](0149-style-bundles.md) — the "ultra realistic" style is largely this ticket's features switched on (149.4); [0152-winding-convention.md](../inprogress/0152-winding-convention.md) — **T0152's engine half landed first** (D33): each feature's pixel test written before the assets were re-wound would have been calibrated against the inverted two-sided flip |

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

- [ ] Every feature DiligentFX's PBR supports is reachable from a `.hpmat`.
- [ ] A game's Slang material can **read and write** each one — not just receive
      it. Parity without extensibility is half the requirement.
- [ ] Each feature has a debug view and a test that fails if it is wired wrong.

## Subtasks

- [ ] 143.1 **Turn the features on** — `EnableClearCoat`, `EnableSheen`,
      `EnableAnisotropy`, `EnableIridescence`, `EnableTransmission`,
      `EnableVolume` in `SurfacePipeline::configure`. **Each needs three things
      moved together**, and this is where the last bug hid: the `CreateInfo`
      setting builds the signature slot, the PSO flag compiles the shader that
      reads it, and `TextureAttribIndices` maps where the data lives. Any one
      alone is silent.
- [ ] 143.2 **Map the remaining texture slots.** `TextureAttribIndices` currently
      maps only the five glTF core textures; the rest stay `-1`, which every
      reader treats as "disabled". The loader's `Default*TextureAttribId`
      constants already exist for all of them.
- [ ] 143.3 **Fill each `SurfaceShadingInfo` sub-struct** and call the matching
      public function. `RenderPBR.psh` is the worked reference for every one.
- [ ] 143.4 **Grow `HpSurfaceOutput` / `IHpMaterial`** with `Clearcoat`,
      `ClearcoatRoughness`, `ClearcoatNormal`, `SheenColor`, `SheenRoughness`,
      `Anisotropy`, `Iridescence`, `IridescenceThickness`, `Transmission`,
      `Thickness`. **This is the subtask that makes "modify for custom too"
      real** — without these fields a game can *have* clearcoat and cannot
      *change* it, which is exactly half of what was asked for.
- [ ] 143.5 **Add the fields to `hp::Material`** and the `.hpmat` format, or they
      are reachable only from an imported glTF and not authorable. Follows
      T0060's per-slot pattern; `11-material-format.md` gains a section.
- [ ] 143.6 **A debug view per feature**, extending `SurfaceDebugView`. **Not
      optional decoration.** Occlusion sat silently unread for this entire
      ticket's life and was found the first minute its debug view existed —
      shipping six more features without one each would be repeating that
      deliberately.
- [ ] 143.7 **Test assets that actually exercise them.** The rock and metal sets
      cannot: rock is dielectric with no clearcoat, metal has no sheen. A feature
      whose test data is all zeroes passes whether it is wired to the right
      channel or to nothing.
- [ ] 143.8 **Measure the permutation count.** Six more feature bits multiply the
      PSO variants, and that is the one genuine cost here. **Measure before and
      after** rather than assuming; it is what T0141.3's `RenderStateCache` and
      `BytecodeCache` exist for, and it may pull that ticket forward.
- [ ] 143.9 **Decide `EnableIBL` and `EnableShadows` deliberately**, with T0087
      and T0086, rather than leaving them off by drift. Several of these features
      are **invisible without an environment** — clearcoat and sheen are almost
      entirely reflections of one.

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
