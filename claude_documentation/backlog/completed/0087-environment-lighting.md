# T0087 — Environment lighting, IBL and skybox

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 490 |
| **Created** | 2026-08-03 |
| **Closed** | 2026-08-08 on T0171 — **closed on IBL, which is delivered and measured; the sky is not, and moved to [T0088](../open/0088-sky-atmosphere-time-of-day.md)**. Three of seven Done-when boxes are ticked, one is partial, and three are `## Descoped`. Read that section before assuming this ticket delivered a skybox: **it did not, and nothing in the engine draws one** |
| **Refs** | [0134-pbr-renderer-adoption.md](../completed/0134-pbr-renderer-adoption.md) — **decide whether this ticket configures DiligentFX's IBL or supersedes it.** T0028 adopted `GLTF_PBR_Renderer`, which ships an IBL path; [0145-lighting-stage-own-the-light-loop.md](../completed/0145-lighting-stage-own-the-light-loop.md) — 145.4 keeps the IBL call a named seam inside the engine's lighting stage; this ticket fills that seam (and the ambient-off render mode 145.5 decides interacts with 87.8's local ambient control); [0152-winding-convention.md](../completed/0152-winding-convention.md) — **T0152's engine half landed first** (D33): IBL baselines are view-dependent, and the winding correction already moved the lit-quad value (211,144,144) → (242,25,25), so any IBL number recorded before it would have needed retaking; **[T0145](../completed/0145-lighting-stage-own-the-light-loop.md) left the IBL seam named and guarded, and this ticket fills it.** `HpResolveLighting` in `engine/shaders/HpSurface.slang` is the one place the image-based diffuse and specular are added — `(DiffuseIBL + SpecularIBL) * IBLScale * Occlusion`, which is why `HpShadedSurface` carries `Occlusion` although no punctual term reads it — and `#if USE_IBL` `#error`s there today, so turning the flag on without writing the term is a build failure rather than a silent zero. `AmbientOcclusionIBL` is the `HpSurfaceInput` field this ticket adds (D27: no field before its system). **A material that does not want ambient overrides `lighting()`** — D38 rejected an `ambient_light_disabled` render mode for that reason, with the reopen trigger recorded there. **[T0143](../completed/0143-extended-material-features.md) (landed 2026-08-07) constrains this ticket four ways.** (1) The resolve seam is **per layer** now: `HpResolveLighting`'s layered form accumulates sheen and clearcoat beside the base, and their IBL terms (`GetSheenIBL`, `GetClearcoatIBL` — pinned in the drift fixture) join *their* layers, not the base's. (2) `EnableIBL` stayed **off deliberately** (143.9): turning it on with sheen enabled makes the base ctor demand `PreintegratedCharlieBRDFPath` — another texture upstream ships only as a GLTFViewer sample asset, and the sheen albedo-scaling LUT's answer (embedded bytes from the pinned submodule, `cmake/hp_embed_binary.cmake`, decoded in `SurfacePipeline`'s ctor) is the worked pattern to reuse. (3) The volume family — `thickness`, `attenuationColour`, `attenuationDistance`, `ior` — is authored, packed and debug-visible today but **shades nothing**, and `extended_material_test`'s volume case pins "changes no shaded pixel" so this ticket flips that assertion deliberately when the refraction path lands. (4) Several T0143 claims are qualified "punctual-only" on its ticket — clearcoat-as-lacquer, sheen-as-velvet — and this is the ticket that makes them visually checkable |

**From T0141 (2026-08-06): this ticket owns a check T0141 could not make.** The
engine now samples metallic/roughness from a material's texture, and the guard
that proves it (`tests/gpu/textured_surface_test.cpp`) can only assert that the
*dielectric* set renders correctly. A metal has no diffuse response, so with
`EnableIBL = false` there is nothing for it to reflect and it renders nearly
black — centre (20, 19, 19) — whether the metallic channel is wired to blue,
to green, or to nothing at all. **When environment lighting lands, extend that
test to assert the metal set is brighter and more specular than the rock**, which
is the first assertion that can distinguish those cases.

## Why

Split out of T0079. Punctual lights alone produce harsh, unconvincing PBR —
surfaces facing away from every light go black. Image-based lighting from an
environment map is what makes physically-based materials actually look right, and
it is largely provided already by `EnvMapRenderer`.

## Done when

- [x] **Environment maps provide diffuse irradiance and specular reflection** —
      `PBR_Renderer`'s own path, turned on. Measured on the RTX 2080,
      `aston_martin_test`, same camera and lamps: mean luma of covered pixels
      **10.4 → 112.9**. The car goes from near-black to its authored cyan
- [x] **Prefiltering happens once at load, never per frame** — `PrecomputeCubemaps`
      at `create()`, shared per device and refcounted. It also cured a
      pre-existing stall: *"Space in dynamic heap is exhausted!"* appeared
      **7,886 times** in one test case before and **zero** after
- [x] **Materials without an environment still render sensibly** — stronger than
      asked: there is *always* an environment (a generated procedural sky), and
      `setEnvironmentIntensity(0)` reproduces the pre-environment image exactly,
      at no per-draw cost and no pipeline rebuild
- [~] **Ambient intensity is controllable** — `SceneRenderer::setEnvironmentIntensity`
      scales `IBLScale`, forwarded by `SceneView` and `SceneRenderLayer`.
      **Shortfall: it is an API, not a serialized per-scene field**, so "tuned
      per scene" is not yet true. Carried to T0088.3/88.5

## Descoped — three Done-when items and their subtasks, with where they went

**Nothing here was dropped and nothing was silently ticked.** The rule this
follows is `CLAUDE.md`'s: close on what was achieved, move the remainder, make
the references point both ways.

| Not delivered | Where it went |
|---|---|
| **A skybox renders behind the scene** (87.2) | **T0088.2.** `EnvMapRenderer` exists, is complete, and **this engine never constructs it** — `grep EnvMapRenderer engine/` returns nothing. The environment is integrated into cubemaps and never drawn |
| **The skybox is in the right place in the render order** | **T0088.2**, with T0087's own note carried: after opaque with depth test on, so it fills only unwritten pixels |
| **The environment is authorable per scene as an asset** (87.1, 87.5) | **T0088.3.** T0170.5 shipped a *procedural* default deliberately — 512 KiB of startup scratch and no bytes at rest, rather than embedding 16 MiB of `papermill.ktx` into every shipped game or making default lighting depend on a VFS asset. **A real HDR environment is the game's to supply**, and that is the seam T0088 now owns |
| **87.7 procedural sky via `EpipolarLightScattering`** | **T0088**, which owns that component. The generated sky T0170.5 ships is a 256×128 gradient, not scattering |
| **87.8 local ambient control** for interiors under a scene-global IBL | **T0088.10.** Still undispositioned, and "rejected, scenes will be lit to tolerate it" remains an acceptable answer; silence is not |
| **T0143's "punctual-only" qualifications made visually checkable** | **discharged here** — see the verification note below |

## Subtasks

- [~] 87.1 Environment map as an asset — **a generated default exists; the
      authored-asset path does not.** → T0088.3
- [~] 87.2 Skybox rendering via `EnvMapRenderer` — **not done.** → T0088.2
- [x] 87.3 Irradiance and prefiltered specular generation — `PrecomputeCubemaps`
- [x] 87.4 Cache prefiltered results — shared per device, refcounted, dying with
      the last renderer on that device
- [~] 87.5 Scene-level environment settings, serialized — **not serialized.**
      → T0088.5
- [x] 87.6 Feed IBL inputs into `PBR_Renderer` — `SetIBLResourceViews`,
      `PSO_FLAG_USE_IBL` off `kFeatureMask`, `EnableIBL` on
- [~] 87.7 Procedural sky via `EpipolarLightScattering` — **not evaluated.**
      → T0088
- [~] 87.8 Local ambient control — **not dispositioned.** → T0088.10

## Notes / findings

### A symptom this ticket owns, measured 2026-08-06 on T0159

**The black pixel speckle on the rock cube sample is the N·L clamp, not the
parallax or self-shadow math.** Normal-mapped texels tip past every light's
horizon and clamp to pure black with no ambient to catch them; POM aggravates
it at grazing view by relocating which texels are seen. Measured on the sample
with the self-shadow march forced off: the near-black count is bit-identical
either way — 431 at the test pose, 10000 at yaw 0.9 where it peaks (of ~47k
covered pixels). The scene's `normalScale: 0.8` note documents the same
mechanism. When this ticket lands ambient/IBL, those pixels become dark instead
of black and the speckle disappears — worth re-rendering the sample then as the
before/after.

### Inherited from T0134 / D24 (2026-08-05) — configure DiligentFX's IBL, do not supersede it

**D24 answers this ticket's open question: configure.** The mechanism, so it is
not re-surveyed:

- `PBR_Renderer::CreateInfo::EnableIBL` — currently `false`, which is one of the
  three reasons every mesh renders black.
- `PBR_Renderer::PrecomputeCubemaps(ctx, attribs)` takes an environment map SRV
  and produces the irradiance cube and prefiltered environment map, with
  `GetIrradianceCubeDesc()` / `GetPrefilteredEnvMapDesc()` describing the targets
  and `NumDiffuseSamples` / `NumSpecularSamples` controlling quality.
- `Renderer.IBLScale` (a `float4`) and `Renderer.PrefilteredCubeLastMip` are the
  frame parameters. **`PrefilteredCubeLastMip` is the one field Diligent wants to
  own** — `SetInternalShaderParameters(params, prefilteredEnvMapSRV)` sets it, and
  `SceneRenderer` already calls that with `nullptr`. Passing the real SRV is this
  ticket's change.
- `PSO_FLAG_USE_IBL` must come out of `kFeatureMask` in `SceneRenderer.cpp`.
- `Components/EnvMapRenderer.hpp` draws the skybox itself.

Read [../completed/0134-pbr-renderer-adoption.md](../completed/0134-pbr-renderer-adoption.md) first.

**`EnvMapRenderer` and `PBR_Renderer` already implement most of this** — the work
is asset plumbing and authoring, not writing IBL. Check what prefiltering Diligent
provides before implementing any of it.

**Prefiltering is expensive and must be cached.** Generating irradiance and
specular mip chains per scene load is a visible stall. Compute once and store
alongside the asset — this is a natural fit for the binary cook step in T0020.

**Skybox draw order:** after opaque geometry with depth testing on, so it only
fills unwritten pixels. Drawing it first wastes fill rate on every pixel later
covered by geometry.

DiligentFX ships `EpipolarLightScattering`, which the old `terrain_lab` used — so
a procedural atmosphere is available if outdoor scenes matter, rather than a fixed
cubemap.

### Amendment (2026-08-03) -- interiors under a global IBL have no answer

The design-gap survey (`documentation/07-design-gaps.md`, item 6) found
`lightmap`, `light probe`, `reflection probe` and `global illumination` at zero
hits in every sense. The lighting stack is punctual lights (T0079), shadows
(T0086), and **one environment map per scene** with a global ambient intensity
(this ticket). The consequence: a single scene-global IBL illuminates interiors
with sky light -- a basement is lit by the same environment as the street above
it. Every engine with authored indoor/outdoor spaces grows *some* answer, and
this backlog had none, not even a rejection.

T0093's visibility decision raises the stakes beyond cosmetics: "a dark room
inside the vision cone is *visible and dark* -- lit by whatever lights exist",
so interior light levels are gameplay-legible.

The failure mode if this waits is the one T0096 names for colour-space bugs:
lights get tuned per scene to compensate for ambient leakage, then every one of
those scenes needs re-tuning when a real mechanism lands. Hence 87.8:
**disposition a cheap first answer before scenes are lit.** The candidate is an
ambient/IBL-intensity *volume* or per-room scalar applied where 87.6 feeds IBL
inputs into `PBR_Renderer` -- deliberately not probes, not lightmaps, not GI,
none of which should be built speculatively. "Rejected, scenes will be lit to
tolerate it" is also an acceptable disposition; silence is not.

A single all-outdoor or all-interior game would need only half of this. The
engine does not get to assume that, because the next game is the one that needs
the other half. **This ticket owns both**, and the live question is scheduling:
which half is built first, and whether the transition between them is designed
in now -- cheap -- or retrofitted later, which is not.

### 2026-08-08 — moved to #2 in the sequence, on measurement rather than intuition

**T0167 put a real asset on screen and it is dark, and this ticket is why.** The
Aston Martin's paint colour is authored into its **specular** map — `texture_1`
is bright cyan while the diffuse `texture_0` is navy-black — which is the
spec-gloss workflow doing exactly what it is for. Sketchfab's teal preview *is*
that map reflecting **their** environment. We have none, so the paint reads black
except where the two punctual lamps raise a specular lobe, which is why the rims
and wheel arch are the only bright things in the frame.

**The defect that was also suspected turned out to be real and is already
fixed** — the shader read spec-gloss through the metallic-roughness
interpretation, closed in T0168.2 with a test that fails on the old code. So what
remains is not a bug at all. **It is this ticket, and nothing else.**

**Three reasons it moved ahead of shadows:**

1. **It is not asset-specific.** A metal with nothing to reflect is black by
   definition, and every PBR export from every DCC tool assumes an environment.
   The engine currently cannot show a metal correctly at all.
2. **A shipped family is invisible until it lands.** T0143 qualifies
   clearcoat-as-lacquer and sheen-as-velvet as **punctual-only** on its own
   ticket, and the volume family — `thickness`, `attenuationColour`,
   `attenuationDistance`, `ior` — is authored, packed and debug-visible while
   **shading nothing**, with `extended_material_test` pinning "changes no shaded
   pixel" for this ticket to flip. Those features are built and cannot be seen.
3. **Shadow bias must not be tuned against pure black.** With no ambient term a
   shadowed pixel has no floor, so every bias value T0086 picks would be
   calibrated against a baseline this ticket then moves. Re-baselining against a
   wrong baseline is the trap T0152 recorded in this backlog, twice, and
   sequencing is the cheap way to avoid it a third time.

**Cheaper than "Moderate" suggests, and both reasons are already scouted.**
`HpResolveLighting` is a **named, guarded seam** — `#if USE_IBL` `#error`s there
today, so enabling the flag without writing the term is a build failure rather
than a silent zero. And 143.9's blocker has a worked pattern: the sheen
albedo-scaling LUT is embedded from the pinned submodule via
`cmake/hp_embed_binary.cmake` and decoded in `SurfacePipeline`'s constructor, so
`PreintegratedCharlieBRDFPath` is the same problem already solved once.

**The open question this ticket must answer first is in its Refs and has not got
easier:** whether it *configures* DiligentFX's existing IBL path or supersedes
it. On the evidence of T0166 and T0168 — four separate cases where Diligent
already had the answer and the engine was not asking — **the burden of proof is
on superseding**, and the answer must be written down either way.

### 2026-08-08 — folded into T0170 by the owner

**This ticket's core is absorbed by [T0170](../inprogress/0170-diligent-owns-the-render-loop.md).**
Once the engine is a `GLTF_PBR_Renderer` subclass, `PBR_Renderer`'s IBL comes
with it — so the question this ticket opened with, *"does it configure DiligentFX's
IBL path or supersede it"*, answers itself: **it configures it.** Turning it on is
T0170.5, and it is what makes T0167's car read as it does in Blender, Godot and
Unity, because the paint colour is authored into the specular map and there is
currently nothing to reflect.

Folded rather than merely reordered, on the owner's instruction: *"expose
capabilities to game devs, not replace"*. Building an IBL path beside one we
already ship would be the exact failure D26's amendment records.

**What is NOT absorbed, and stays here or moves to [T0171](0171-expose-not-replace-sweep.md):**

- **The skybox** as a visible background, and its authoring — an environment map
  the renderer samples is not the same thing as a sky a player sees.
- **The seam.** T0171.3 says the attachment pattern is designed **once** for all
  of these, not nine times; whatever a game overrides about ambient belongs to
  that design, not to a hook invented here.
- **87.8's local ambient control**, and its interaction with D38's rejected
  `ambient_light_disabled` render mode — a material that wants no ambient
  overrides `lighting()`, and that reopen trigger is recorded on D38.
- The T0143 constraints this ticket carried: the **per-layer** resolve
  (`GetSheenIBL`, `GetClearcoatIBL` join their own layers, not the base's), the
  **Charlie-LUT** embed that 143.9 blocked on, and
  `extended_material_test`'s volume assertion to flip. **T0170.5 must not tick
  those off silently** — they are checkable claims and they belong to whoever
  turns IBL on.

Removed from the Current ticket sequence in the same change.

### 2026-08-08 — closed on T0171, and what was verified rather than assumed

**The four constraints this ticket carried from T0143 were checked individually
rather than ticked along with the IBL box**, because the note above says in as
many words that *"T0170.5 must not tick those off silently — they are checkable
claims"*:

1. **The per-layer resolve is correct.** `HpResolveLighting` in
   `engine/shaders/HpSurface.slang` adds `Surface.SheenIBL` to the sheen layer
   (`:1133`) and `Surface.ClearcoatIBL` inside the clearcoat term (`:1152`) —
   **their own layers, not the base's** — and `HpIBLWeight()` names the
   `IBLScale * Occlusion` product once (`:1091`) so a custom resolve reuses it
   rather than re-deriving it. ✅ discharged.
2. **The Charlie-LUT blocker (143.9) is gone.** `g_PreintegratedCharlie` is
   embedded from the pinned submodule, 25 KiB, through the same
   `hp_embed_binary` path T0143 used for the sheen albedo-scaling LUT. It was
   *forced* rather than chosen: upstream loads that LUT inside its `EnableSheen`
   block but only `if (EnableIBL)`, so turning IBL on with sheen already on is
   exactly what made a second embed necessary — without it
   `g_PreintegratedCharlie` binds **null** for every sheen permutation.
   ✅ discharged.
3. **Clearcoat-as-lacquer and sheen-as-velvet are no longer punctual-only** —
   both layers receive image-based light now, so T0143's qualifications on its
   own ticket can be read as satisfied. ✅ discharged.
4. **The volume family still shades nothing**, and `extended_material_test`'s
   volume case still asserts *"thickness 0.5 … shades nothing — by design"*.
   ❌ **not discharged — and it is not this ticket's to flip.** The sweep found
   that **upstream does not shade it either**: `VolumeThickness` reaches a debug
   view only (`PBR_Shading.fxh:984`), and upstream's whole transmission
   treatment is `diffuse *= 1 - Transmission`, which is precisely what
   `HpSurface.slang:2357` already does. So this is ⬆️ genuinely ours and needs a
   decision rather than a switch. **Recorded as an unowned gap** in
   [`12-vendored-capabilities.md`](../../documentation/12-vendored-capabilities.md),
   with the note that a *game* can already write screen-space refraction today
   through `IHpMaterial` plus T0147's scene-colour read — `13-shader-capability-matrix.md`
   marks refraction *expressible today*. What is undecided is whether the
   **standard** material does it.

**The check T0141 handed this ticket is now makeable, and was not made.**
`textured_surface_test` could only assert the *dielectric* set rendered
correctly, because with `EnableIBL = false` a metal has nothing to reflect and
renders near-black at (20, 19, 19) whether the metallic channel is wired to
blue, to green, or to nothing at all. With an environment, *"the metal set is
brighter and more specular than the rock"* becomes an assertion that can
distinguish those cases. **It was not written.** Recorded rather than claimed;
it belongs to whoever next touches that file.

**One prediction this ticket made turned out right, recorded as a hit.** It
predicted the rock cube's black speckle — normal-mapped texels tipping past
every light's horizon and clamping to pure black with no ambient to catch them —
would become *dark* rather than *black* once ambient landed. It did: T0167
reports the top face is no longer black, and the residual speckle is a different
mechanism entirely (the view march at grazing incidence), diagnosed on T0158.

### Why this closes rather than staying open

Four of seven Done-when items are not met, which normally argues for leaving a
ticket open. It closes anyway, for the reason `CLAUDE.md` gives: **the remainder
is not blocked on anything this ticket knows about, and it has a better owner.**
The unmet items are all *the sky* — drawing one, authoring one, and how ambient
varies under one — and T0088 owns the sky. Splitting "the environment that
lights the scene" from "the sky you can see" across two tickets was the original
mis-shape; this closure removes it rather than carrying it.

**The references point both ways**: T0088's `Refs` names this ticket and what it
inherited, and the `## Descoped` table above names the subtask on the other end
of every move.
