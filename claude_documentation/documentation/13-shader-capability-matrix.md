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
| Game-declared textures | **P** — four slots, **named by the engine** (`HpTexture0`…`3`), bound by `.hpmat`. Author-chosen names need a per-module descriptor set or a circular rename pass; see the widening below | (author names: unowned) |
| Light access | **P** — readable since D27's amendment (2026-08-06, T0159): `g_Frame.Lights[]` is in scope, **but its order is unspecified for directional lights** (unstable sort on equal keys — pick by intensity, never index). Replacing the loop is T0145 | T0145 |
| Vertex hook | **–** | T0146 |
| Screen resources (depth, scene colour, fed textures) | **–** | T0147 |
| Time / frame counter | **P** — `HpSurfaceInput::Time` landed 2026-08-06 (T0159): the caller's clock reaches shaders through every render path. The *frame counter* (`uiFrameIndex`) is still only zeroed, never written | (frame counter: unowned) |
| Per-instance data | **–** — layout exists, bytes undefined. Explicitly *not in scope* on T0160, which recorded the trigger: `PBRPrimitiveAttribs::CustomData` is the place, the write is skipped when null and the engine passes null | (unowned) |
| Pass and pipeline control | **–** | T0148 / T0150 / T0094 |

## The technique rows

| Family | Needs, beyond what exists | Blocked on |
|---|---|---|
| **Parallax** — POM, relief, cone-step | nothing — knobs landed 2026-08-06 (T0160): `rock_pom.slang`'s reference plane is a declared parameter valued by `rock.hpmat`, and self-shadowing landed on T0159 | *expressible today* |
| **De-tiling / macro variation** | the per-tap sampling seam (declared textures and parameters landed) | T0153.1 |
| **Layered surfaces** — snow, wetness, moss, detail maps | nothing — declared textures and parameters landed 2026-08-06 (T0160) | *expressible today* |
| **World-aligned masks, game triplanar** | nothing | *expressible today* |
| **Anisotropy, sheen, clearcoat, skin** | the light loop, extended features (the real tangent landed — zero only when the mesh has none, `w` always +1) | T0145, T0143 |
| **Rim / Fresnel / unshaded** | nothing | *expressible today* |
| **Vertex motion** — wind, sway, billboarding, displacement, morph, per-instance | a vertex stage that does not exist | **T0146 — zero of six expressible** |
| **Transparency** — dissolve, fade | nothing — a dissolve threshold and a fade factor are declared parameters (T0160), and `In.Time` drives them | *expressible today* |
| **Transparency** — refraction, decals, dithered LOD | scene colour, per-instance data, a LOD system | T0147, T0039/T0040 |
| **Screen-space** — soft particles, heat haze, frosted glass, fog-of-war | bound screen resources | **T0147** |
| **Time-driven** — scrolling, flowmaps, pulsing emissive | nothing — `In.Time` landed on T0159 and a flowmap's texture is a declared slot (T0160) | *expressible today* |
| **NPR** — cel, hatching, custom BRDF | the light loop | **T0145** |
| **NPR** — ramp lighting | the light loop (the ramp texture itself is a declared slot since T0160) | **T0145** |
| **NPR** — outlines | a vertex stage and a second pass | T0146, T0148 |

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

A D27-clean light (T0145), screen resources (T0147), a vertex hook and custom
interpolators (T0146), an instancing path, a LOD fade factor, motion vectors,
`ShadowFactor` / `Visibility` / `AmbientOcclusionIBL` (T0086 / T0093 / T0087).

~~Declared parameters~~ — **landed 2026-08-06 (T0160)**, and it was the largest
single item on this list: it appeared in the "blocked on" column of ~13 of the
40 audited techniques, against ~9 for the light loop. Six rows above are now
*expressible today* because of it.

---

## The one widening this landing owes

**A module's texture slots are named by the engine, not by its author** —
`HpTexture0` … `HpTexture3`. That is not a limitation of Slang or of Diligent;
it is the price of using **one** pipeline resource signature, which is created
once at renderer construction, before any module exists, and which Diligent
matches a shader's resources to *by name*.

Two ways to author-chosen names were designed on T0160 and neither was taken:

- **A per-module signature**, added beside the base one. Costs a second SRB and
  a second `CommitShaderResources` on every draw of every custom material, and
  it was the last unproven item in the ticket. The shared-slot design removed
  the need to prove it at all.
- **A rename pass** — reflect the module, learn it declares `detailMap`, and
  emit `#define detailMap HpTexture0` into the generated include ahead of it.
  This is **circular**: the reflection rides the compile, so the names are not
  known until after the compile that would have to be told them.

A third path exists and is what would actually pay for it: a deviceless
reflection pass that runs at *import* time, before any pipeline. That does not
exist either, and the reason is the next entry.

**Parameter *fields* are the author's** for the mirror-image reason — a field is
not a resource, so it costs the signature nothing.

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
- The permutation multiplier once surface, lighting and vertex modules can vary
  independently — undesigned, so uncounted.
