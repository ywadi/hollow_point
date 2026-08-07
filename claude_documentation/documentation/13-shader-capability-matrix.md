# What a game's shader can and cannot do

**This document exists to make the *next* gap visible before somebody hits it.**

It was written on 2026-08-06 after a game shader could not implement parallax
self-shadowing — a technique from 2006 — and failed **silently**: it compiled,
rendered, and produced a zero shadow term while reading as correct in its own
source. That was not bad luck. The material contract had been shaped entirely by
techniques that happened to fit in a **single hook** — parallax fits in
`surfaceCoordinates`, triplanar fits in sampling, unshaded is a bool — so the
first technique needing two hooks fell through a gap nobody had looked for.

The audit below is the answer to "what else is like that". Its value is entirely
in being **maintained**.

---

## How to use it

**Before building a technique**, find its row. If a cell it needs is empty, that
is the work — discovered in advance rather than after a day of debugging.

**Before proposing a technique that has no row, add one.** Fill in what it needs
and see what is missing. That is the whole mechanism; a row added afterwards is
just a record, a row added first is a warning.

**When a capability lands**, flip its cells and date the edit.

Legend: **Y** exists · **P** partial, see the note · **–** absent, with its
owning ticket.

---

## The capability columns

| Capability | Status | Owner |
|---|---|---|
| Stable inputs (`HpSurfaceInput`) | **P** — tangent, undisplaced UV (`UV0Base`) and `Time` landed 2026-08-06 (T0159); front-facing still reaches only `shadingNormal` | (front-facing: unowned) |
| Coordinate hook (`surfaceCoordinates`) | **Y** | — |
| Per-channel hooks (`baseColor`, `occlusion`, …) | **Y** | — |
| Whole-output hook (`surface`) | **Y** | — |
| Per-tap sampling seam | **–** | T0153.1 |
| Cross-hook state | **Y** — hooks are `[mutating]`, members zero-initialised (2026-08-06, T0159) | — |
| Game-declared parameters | **Y** — `cbuffer HpMaterialParams` with author-named fields, valued by `.hpmat`, hinted by `[HpRange]`/`[HpColor]`/`[HpTooltip]` (2026-08-06, T0160). float/2/3/4, int, bool; 256 bytes | — |
| Game-declared textures | **Y** — author-named `Texture2DArray` globals at any count, reflected into a per-module resource signature and bound by `.hpmat` under the author's names (2026-08-07, T0161, D35). Cost measured before commitment: 34 ns/draw (161.1). **The one deliberate limit is sampler *state***: modules sample through the engine's six-name immutable palette (`HpSamplerLinearWrap` …), and a module's own `SamplerState` is refused by name. `HpTexture0`…`3` remain as deprecated declarations | — |
| Light access | **Y** — `HpLight` (2026-08-07, T0145, D31): the engine's own mirror of DiligentFX's packed struct, with type, position, direction, **`ToLight`**, colour, engine-computed attenuation, radiance, range in **metres** and both cone cosines. Every field exists in every permutation, which is what a re-exported `PBRLightAttribs` could never promise. **The order is a contract now** (D38): nearest, then brightest, then entity — `HpLight::Index == 0` is the **dominant light**, and the unstable-sort hazard that cost T0158/T0159 a day is closed | — |
| Lighting stage | **Y** — `IHpMaterial.light()` (per light, rung 3) and `IHpMaterial.lighting()` (whole stage, rung 4), 2026-08-07, T0145/D30. `HpLightResponse` splits the four factors the engine multiplies — `(Diffuse + Specular) * Intensity * NdotL` — so cel shading is one assignment. `HpLightCount()`, `HpGetLight(i, pos)`, `HpStandardLight(L, S)` and `HpResolveLighting(S, punctual)` are the primitives a whole-stage override is built from. A material overriding nothing renders **bit-identically**: 0 differing bytes, asserted | — |
| Vertex hook | **Y** — `IHpMaterial.vertex()`, object space in and out (2026-08-07, T0146, D36). The engine owns the vertex `main`; `HpVertexInput` carries the mesh's own position, normal, tangent, UVs and colour plus the **rest world position**, the object-to-world matrix, the camera position and `Time`. `HpVertexOutput` takes displaced geometry, rewritten UVs and vertex colour. Godot's `skip_vertex_transform` is refused, and D36 says why | — |
| Custom interpolators | **P** — four fixed `float4` slots, `HpVertexOutput::Custom0` … `Custom3` in and `HpSurfaceInput::Custom0` … `Custom3` out (2026-08-07, T0146.4). **Not a declaration**: `VSOutput` is generated per permutation and reflection rides the compile, so the count cannot depend on the module — D36 records the circularity. Present only in custom-material permutations; unwritten slots read zero | (widening: T0151) |
| Screen resources (depth, scene colour, fed textures) | **Y** — `HpSceneColour(uv)`, `HpSceneDepth(uv)`, `HpSceneViewDepth(uv)` and `HpViewDepth(z)` over the engine-fed `g_SceneColour`/`g_SceneDepth`, addressed by `HpSurfaceInput::ScreenUV` (2026-08-07, T0147, D37). Snapshots taken between the opaque and blend passes, so **only a material with `alphaMode: Blend` may read them** — enforced at pipeline build, by name, with the checkerboard. A texture a *game layer* produced rides T0161's declaration unchanged: `setGameTexture(name, view)` feeds it, after the `.hpmat` and before white. **No normal-roughness read** — forward-only, see the row below | — |
| Time / frame counter | **P** — `HpSurfaceInput::Time` landed 2026-08-06 (T0159): the caller's clock reaches shaders through every render path. The *frame counter* (`uiFrameIndex`) is still only zeroed, never written | (frame counter: unowned) |
| Deferred normal-roughness read | **–**, and **decided** rather than owed (2026-08-07, D37): this engine is forward-only (D24), so there is no G-buffer to read and synthesising one is a prepass every game pays for. A shader already has *its own* normal and roughness; the screen-space read is the *other* surface's, which is what the prepass would produce. Arrives with the pass that writes it, or not at all | (rejected — reopen with a pass, per D35) |
| Per-instance data | **–** — layout exists, bytes undefined. Explicitly *not in scope* on T0160, which recorded the trigger: `PBRPrimitiveAttribs::CustomData` is the place, the write is skipped when null and the engine passes null | (unowned) |
| Pass and pipeline control | **–** | T0148 / T0150 / T0094 |

## The technique rows

| Family | Needs, beyond what exists | Blocked on |
|---|---|---|
| **Parallax** — POM, relief, cone-step | nothing — knobs landed 2026-08-06 (T0160): `rock_pom.slang`'s reference plane is a declared parameter valued by `rock.hpmat`, and self-shadowing landed on T0159 | *expressible today* |
| **De-tiling / macro variation** | the per-tap sampling seam (declared textures and parameters landed) | T0153.1 |
| **Layered surfaces** — snow, wetness, moss, detail maps | nothing — declared textures and parameters landed 2026-08-06 (T0160) | *expressible today* |
| **World-aligned masks, game triplanar** | nothing | *expressible today* |
| **Anisotropy, sheen, clearcoat, skin** | the extended features themselves. **The shading half landed 2026-08-07 (T0145)**: a custom BRDF is `light()` writing `R.Diffuse`/`R.Specular`, and the real tangent has been in `HpSurfaceInput` since T0159. What is missing is the *material data* — a sheen colour, a clearcoat normal, an anisotropy direction — and the engine's loop `#error`s on `ENABLE_SHEEN`/`ENABLE_CLEAR_COAT`/`ENABLE_ANISOTROPY` rather than dropping them silently, so T0143 has a named place to write | T0143 |
| **Rim / Fresnel / unshaded** | nothing | *expressible today* |
| **Vertex motion** — wind, sway, billboarding, displacement, morph | nothing — the vertex stage landed 2026-08-07 (T0146). Sway is the rock cube sample's worked example; billboarding has `In.CameraPos`; morph deltas need a buffer the mesh format does not carry (T0041's neighbourhood) | *five of six expressible today* |
| **Vertex motion** — per-instance offsets | per-instance data, which is still undefined bytes in `PBRPrimitiveAttribs::CustomData` | (unowned — see the per-instance row above) |
| **Transparency** — dissolve, fade | nothing — a dissolve threshold and a fade factor are declared parameters (T0160), and `In.Time` drives them | *expressible today* |
| **Transparency** — refraction | nothing — scene colour landed 2026-08-07 (T0147). `screen_inputs_test` asserts the refracted image is the scene *pixel for pixel* at the displaced coordinate | *expressible today* |
| **Transparency** — decals, dithered LOD | per-instance data, a LOD system | T0039/T0040 |
| **Screen-space** — soft particles, heat haze, frosted glass | nothing — bound screen resources landed 2026-08-07 (T0147, D37). The soft-particle read is the ticket's own depth-fade example, proved before T0106 needs it; heat haze and frosted glass are the refraction with a different offset | *expressible today* |
| **Screen-space** — fog-of-war | nothing *mechanically* — a game layer renders the visibility field and feeds it by name (T0147.4); what is still absent is the engine **computing** one, and a `HpSurfaceInput::Visibility` field waits on that rather than on this | T0093 (the field; the mechanism is done) |
| **Time-driven** — scrolling, flowmaps, pulsing emissive | nothing — `In.Time` landed on T0159 and a flowmap's texture is a declared slot (T0160) | *expressible today* |
| **NPR** — cel, hatching, custom BRDF | nothing — the light loop landed 2026-08-07 (T0145, D30). Cel is `R.NdotL = ceil(R.NdotL * 4) / 4` inside `light()`, measured at **3 distinct colours against a smooth control's 99** on the same material and light; a custom BRDF writes `R.Diffuse`/`R.Specular`; hatching reads `In.ScreenUV` alongside `R.NdotL`. **Per light, which a post-process cannot be** — a pass over the summed image bands two lights wrongly, Godot proposal #484's open complaint | *expressible today* |
| **NPR** — ramp lighting | nothing — the light loop landed 2026-08-07 (T0145). `R.Diffuse = ramp.SampleLevel(HpSamplerLinearClamp, float2(R.NdotL, 0.5), 0).rgb` over a texture the module declared itself (T0160/T0161), with `R.NdotL = 1.0` so the ramp *is* the falloff | *expressible today* |
| **NPR** — outlines | a second pass (the vertex stage landed 2026-08-07, T0146: an inverted-hull shell is `Out.Position += In.Normal * width`) | **T0148** |

---

## The three-way split, which is what makes this costable

Not every missing input costs the same. Sorting them this way is the most useful
thing the audit produced:

### (a) The engine computes it and throws it away — cheapest possible

- **The tangent frame.** Built per fragment in `HpParallaxUv`, built *again*
  inside DiligentFX's `PerturbNormal`, and handed to a game **zero** times. The
  rock cube sample computes it a **third** time — and since T0159 it can at
  least *keep* its copy across hooks rather than rebuilding per hook.
- ~~The undisplaced UV0~~ — **landed 2026-08-06** as
  `HpSurfaceInput::UV0Base` (T0159.3).
- **Camera matrices, viewport size, mip bias, the previous camera.** All written
  every frame; none of it contract.

### (b) It exists upstream but never reaches the contract

- ~~The vertex tangent~~ — **landed 2026-08-06** (T0159.4):
  `HpSurfaceInput::Tangent` is the mesh's world-space tangent when it has one,
  zero when it does not, `w` always +1 because their wire format is `float3`.
- **Front-facing.** Enters `main`, reaches only `shadingNormal`.
- **Per-primitive `CustomData`.** In the layout; the write is skipped when null
  and the engine passes null, so those sixteen bytes are **undefined memory**.

### (c) It does not exist — real work

An instancing path, a LOD fade factor, motion vectors, `ShadowFactor` /
`Visibility` / `AmbientOcclusionIBL` (T0086 / T0093 / T0087).

~~A D27-clean light~~ — **landed 2026-08-07 (T0145, D30/D31)**, and it was the
last whole *family* in this table: cel, hatching, ramp lighting and custom BRDFs
were blocked on one fact, that quantised `N·L` lives inside DiligentFX's
`ApplyPunctualLight` with the BRDF called inline at `:690` and no seam anywhere
in the body. The work was mirroring ~120 lines and deciding a light *order*; the
hook itself is two methods with defaults. The mirror is guarded rather than
remembered — `tests/fast/upstream_drift_test.cpp` re-extracts six upstream
functions on every run of the fast suite and prints a line diff when one
moves.

~~Screen resources~~ — **landed 2026-08-07 (T0147, D37)**, and they were the
only entry in this table whose whole technique *family* was blocked: six
techniques, none of them expressible, for the single reason that a shader cannot
sample a buffer that is not bound to the pipeline. The work was not the contract
— four helper functions and one `ScreenUV` field — it was the frame gaining a
seam to take the snapshot at, which turned out to also be the fix for blended
geometry being submitted before the opaque geometry behind it.

~~A vertex hook and custom interpolators~~ — **landed 2026-08-07 (T0146,
D36)**, and it was the largest remaining item after declared parameters: the
vertex family was the only one in the table with **zero** of six techniques
expressible, and the reason was that there was nothing to expose — the vertex
`main` was DiligentFX's and had no hook at any setting. Building the stage was
the work; the hook itself is one line inside it.

~~Declared parameters~~ — **landed 2026-08-06 (T0160)**, and it was the largest
single item on this list: it appeared in the "blocked on" column of ~13 of the
40 audited techniques, against ~9 for the light loop. Six rows above are now
*expressible today* because of it.

---

## The widening T0160 owed — paid on 2026-08-07 (T0161, D35)

This section used to record that a module's texture slots were named by the
engine (`HpTexture0` … `HpTexture3`), the price of one shared resource
signature created before any module exists. **That price is no longer paid.**
The per-module signature T0160 weighed and deferred is exactly what shipped:
the engine reflects the compiled module, subtracts the names its own
signatures carry, and builds a second signature from what is left — the
author's textures, under the author's names, at any count. The rename pass
stayed rejected as circular, and the cost that parked the second signature on
T0160 was measured before commitment: **34 ns per draw** on this machine, 40
on llvmpipe (T0161.1), including the second per-draw parameter map.

What remains engine-named, deliberately:

- **`HpMaterialParams`** — the parameter *block*, so a `.hpmat`'s values have
  one place to go. Fields inside it are the author's.
- **Sampler state** — modules sample through the six-name immutable palette
  (`HpSamplerLinearWrap` …); an author-declared `SamplerState` is refused by
  name with the palette in the log. D35 records the additive escape.

The old slots survive as deprecated declarations in the contract, bound
through the same reflection walk as any author name, so a shipped v2 `.hpmat`
renders unchanged — pinned by the rockcube sample tests.

### What the texture budget looks like since the migration

The four slots and the params buffer left the shared signature, so a **plain
glTF material's SRB carries fewer descriptors than before T0160 existed**, and
a custom material pays for exactly what it declares. Counted from the
signature at creation (logged by the renderer, pinned by
`custom_shader_material_test`), on 2026-08-07:

| | sampled images | immutable samplers |
|---|---|---|
| **T0160's count** (5 glTF + `g_HeightMap` + 4 module slots) | 10 | 11 |
| **T0161** (5 glTF + `g_HeightMap`; palette samplers added) | 6 | 13 |
| **today** (+ T0147's `g_SceneColour`, `g_SceneDepth`) | **8** | **13** |
| + T0087's IBL | 11 | 13 — all reuse `g_LinearClampSampler` |
| + T0086's shadow map | 12 | 14 |
| + **T0143**'s extended materials (clearcoat ×3, sheen ×2 and two LUTs, anisotropy, iridescence ×2, transmission, thickness) | **~22** | **~20** |
| + a module's own declarations | + what it declares, in **its** signature | palette, shared |

**T0147 spent two images and no samplers, deliberately.** The screen
intermediates are sampled through the palette that already exists
(`HpSamplerLinearClamp` for colour, `HpSamplerPointClamp` for depth) rather than
through slot samplers of their own — so the count that was *already* under
pressure from T0143 did not move at all. `custom_shader_material_test` pins both
numbers off the renderer's own creation-time log line.

Vulkan's *guaranteed* per-stage floor is 16 for both — **quoted from the spec
and not queried on any device here**, and desktop hardware is far above it.
T0143 still crosses the image floor on its own (~22 with the four module slots
no longer contributing), and the answer there remains Diligent's
`ShaderTexturesArrayMode` collapsing the seventeen material slots into one
array — never trimming what a game may declare. The sampler count crossing 16
under T0143 is new pressure from the palette's six; the palette is shared
across every pipeline, so collapsing material samplers is the lever there too.

A signature occupies at most 2 Vulkan descriptor sets and everything here is
`MUTABLE`, so base + module = **2 sets** against a spec floor of 4 (this
machine: 32 on the RTX 2080, 8 on llvmpipe).

---

## What a game shader may reach — since 2026-08-06

**Anything.** D27 was amended on T0159: a game's `.slang` may include DiligentFX
and reach engine internals, with no warning and no version check. The reasoning
is on that ticket and in the decision log; the short version is that this engine
is permanently on Diligent, a shipped game never meets a newer engine under D12's
lockstep, and a broken shader already renders loud magenta with a logged
compiler error.

**That does not make this document less important — it makes it more.** Reaching
for an internal is a *signal that a contract widening is owed*, and the point of
the matrix is that the widening is a ticket rather than every game reinventing
it. An escape hatch used routinely is a contract that failed.

---

## What the audit could not determine

Recorded because an overstated document is worse than an open question.

- ~~Whether Diligent rejects a module-declared `cbuffer` absent from the
  signature~~ — **executed 2026-08-06 (T0160's spike): loud.** PSO creation
  fails naming the resource (`'HpMaterialParams' ... not present in any
  pipeline resource signature`), the frame is the missing-material
  checkerboard, and the renderer logs one substitution line naming the module.
  That is now what the *absence* of a signature slot looks like; the slot
  exists, and `custom_shader_material_test.cpp` pins the capability instead.
- **Reflecting a module without a device is not possible today, and D28 promised
  it.** Measured on T0160: a module is a *fragment*, not a program — it names
  `IHpMaterial` and `VSOutput`, and since D27's amendment its bodies may reach
  `g_HeightMap`, `g_Frame.Lights[]` and DiligentFX's getters — so a translation
  unit containing it alone does not type-check, and slang emits **no**
  reflection for a failed compile (`slangc -reflection-json` writes no file,
  with `-no-codegen` and without). Reflection therefore rides the real
  pixel-shader compile, which needs `PBR_Renderer::DefineMacros` — ~100 macros
  read from `m_Settings` *and* `m_Device.GetDeviceInfo().Features`. Reproducing
  that without a device means a second, hand-maintained macro set, which is the
  second path D28 forbids. The consequence a person meets: a material's declared
  parameters are known only after a pipeline has been built for its module.
- Whether the per-primitive `CustomData` bytes read as zero or as noise on real
  hardware. Undefined per the API; not measured.
- The register and occupancy cost of publishing the tangent frame
  unconditionally. Unmeasured.
- **Shader hot reload is traced, not executed.** The chain analysis says it does
  not work; no live edit-while-running session was performed.
- ~~The permutation multiplier once surface, lighting and vertex modules can
  vary independently~~ — **answered, and the answer is 1** (T0146 for the
  vertex half, T0145 for the lighting half). Every stage is a method on the
  *same* `IHpMaterial`, compiled from one file in one request, so no stage adds
  a permutation axis. A game does not ship a "lighting module"; it ships a
  material that overrides `light()`.
- **Register pressure is measured for the standard material and inferred for a
  game's.** T0145 measured the SPIR-V byte counts before and after the mirrored
  loop — identical for the standard material (12504 `psMain`, 7280 `vsMain`),
  124 bytes *smaller* for a custom module — and the per-light fill cost at
  0.035 ns/pixel/light, unchanged inside the run-to-run spread. What is not
  measured is a *driver's* register allocation, which SPIR-V size only proxies;
  no tool here reads it.
