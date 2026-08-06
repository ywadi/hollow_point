# T0160 — Material-declared parameters and resources: a game's shader gets its own knobs

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 466 |
| **Created** | 2026-08-06 |
| **Blocked by** | nothing — independent of [0159-open-the-material-contract.md](../completed/0159-open-the-material-contract.md), and the two together are "a game can do anything" |
| **Refs** | [../../documentation/13-shader-capability-matrix.md](../../documentation/13-shader-capability-matrix.md) — **this is the single largest row-unblocker in it, ~13 techniques**; [../../documentation/11-material-format.md](../../documentation/11-material-format.md) — the `.hpmat` gains a section; [0151-shader-variants-and-compile-cost.md](../open/0151-shader-variants-and-compile-cost.md) — **registers that parameters add no PSO axis**, which is the point; [0035-hierarchy-and-inspector.md](../open/0035-hierarchy-and-inspector.md) — the inspector rows these produce; [0153-surface-detiling.md](../open/0153-surface-detiling.md), [0149-style-bundles.md](../open/0149-style-bundles.md) — consumers; **D13**, **D23**, **D28**, **D34** |

## Why

**A game cannot ship a tunable material.** The first material shader ever authored in this repository — `samples/rockcube/content/shaders/rock_pom.slang` — hard-codes its one parameter as a compile-time literal, and says so in its own comment, because there is nowhere else to put it:

> A literal rather than a `.hpmat` parameter because that vocabulary is the engine's and this knob is this shader's.

The inventory is five glTF texture slots plus the engine's height slot, and `CustomData.x/y/z/w` of which two are already spoken for. No names, no types, no inspector rows, no per-material variation. An artist cannot change anything about a game's own shader without editing the shader.

**This is not something opening DiligentFX can fix.** Diligent does not offer it either; it is our material model, our `.hpmat` and our resource signature. The 2026-08-06 capability audit found it as the **most-depended-on missing capability in the matrix** — it appears as a blocker in more technique rows than anything else, ~13 of 40, including detail maps, layered snow/wetness, ramp lighting for toon, flowmaps, dissolve progress and every textured layer blend.

## The shape, from the field

Three engines converged on the same answer and it is worth copying rather than inventing:

- **Godot**: `uniform float x : hint_range(0,1)` in the shader auto-generates the inspector row; `instance uniform` gives per-node variation with no new material.
- **Filament**: a `.mat` descriptor pairs *declared parameters* with a shader snippet, compiled offline by `matc` into cooked variants — structurally D34 plus this ticket.
- **Bevy**: `#[derive(AsBindGroup)]` on a struct beside a fragment shader.

**Measured on the pinned `slangc 2026.14.1`:** a module-declared `cbuffer` plus its own `Texture2D`/`SamplerState` compiles, and `-reflection-json` reports every field with name, offset and size and every resource with its binding. **Slang user-defined attributes survive reflection with their arguments**, so `[HpRange(0.0, 1.0)]` on a field can drive an inspector row without inventing syntax — Godot's shape, in Slang.

## Done when

- [x] A game's `.slang` **declares its own parameters and textures**, and a `.hpmat` carries values for them
- [x] Those parameters carry the **same description** an inspector already consumes for reflected fields (D23) — `ShaderParam::meta()` returns `hp::PropertyMeta`, no second mechanism. **There is no inspector to put them in** (T0035/T0032.9 are open); what this ticket owes is the description, and it is bidirectionally referenced from both
- [x] Changing a value **rebuilds no pipeline and invalidates no cook** — values are data, not permutations
- [x] `rock_pom.slang`'s reference plane stops being a literal and becomes a parameter, **as the proof**
- [x] The `.hpmat` format document gains its section, and the generated shader reference explains the declaration syntax

## Subtasks

- [x] 160.1 **The declaration vocabulary** — a reserved `cbuffer` name plus module-declared textures and samplers, and the engine-defined attributes for hints (`[HpRange]`, `[HpColor]`, tooltip). The attribute *definitions* live in `HpMaterial.slang` so a module still includes nothing
- [x] 160.2 **Reflection**: a pass producing an `hp::ShaderParamLayout` — names, types, offsets, hints, resources — cached per module path on `SurfacePipeline`. **Not deviceless and not a separate pass over the module**, both of which were measured impossible rather than skipped; see the findings. It rides the real `HpSurface.slang` compile through `spGetReflection`, so no T0151.2 migration, and a cooked build reads the same layout out of the SPIR-V through Diligent
- [x] 160.3 **`.hpmat` carries values**: a `params:` map and a `textures:` map, lenient like every other field, with one schema bump. `Material` gains an ordered name/value store rather than typed fields
- [x] 160.4 **Binding**: a second, per-module resource signature beside the existing ones. The precedent for coordinating Slang's assignments with a declared signature is in-tree — `addVkInputLocations` already injects `[[vk::location]]` into generated source, and the same prologue trick pins bindings. **Spike this before the design freezes**; whether Diligent accepts it is asserted, not measured
- [x] 160.5 **The writer**: `SceneRenderer`'s material binding fills the parameter buffer from the layout's offsets and binds GUID-resolved textures, falling back to the placeholder exactly as the fixed slots do
- [x] 160.6 **One description, two reflections** — `ShaderParam::meta()` returns the same `hp::PropertyMeta` a reflected C++ property carries, so an inspector consumes one struct whichever reflection produced it. **The panel itself is T0035/T0032.9's**, and both now reference this
- [x] 160.7 **Register the permutation answer with T0151**: parameters add **no** PSO axis, because module identity already keys the pipeline. Said there explicitly 2026-08-06 — its Notes now carry "prefer a runtime parameter over a permutation bit" with the reasoning, and its Refs point back here
- [x] 160.8 **Update the capability matrix** for every cell this lands, which should be the largest single edit that document ever takes

## Not in scope

- **Per-instance parameters** (Godot's `instance uniform`). Wanted, and a different mechanism — `PBRPrimitiveAttribs.CustomData` is the place to investigate, and it is currently **undefined memory**: the layout exists, the write is skipped when null, and the engine passes null. Record the trigger and leave it.
- **A material graph or any authoring UI.** Text declaration and an inspector row; a node editor is a much later question.

## Notes / findings

### Downgraded Complex -> Moderate, 2026-08-06

Both unknowns that carried the Complex label were retired by the two spikes this
ticket mandated before designing:

- **Reflection rides the existing `ICompileRequest` via `spGetReflection`** — no
  T0151.2 session-API migration is inherited. That was the scope risk that could
  have doubled the ticket.
- **The failure mode is loud, on device**: PSO creation refuses by name, the
  frame is the missing-material checkerboard, and one log line names the module.
  It also proved Diligent name-matches slang-emitted resources and remaps
  bindings, so no `[[vk::binding]]` injection is needed.

What remains is mechanical: vocabulary, the reflection pass, a `.hpmat` schema
bump, the binding and writer, inspector rows. Only the *additional per-module
signature* is still unproven, and the spike surfaced a shared-slot design that
would remove even that.

### Measured 2026-08-06

- Slang reflection reports names, offsets, sizes and bindings for a module-declared `cbuffer` and its resources — verified via the CLI's `-reflection-json`. **The in-process equivalent on the compile path this engine uses is asserted, not verified**, and if it forces the session-API migration then this ticket inherits T0151.2 as a dependency.
- User-defined attributes survive into reflection with their argument values.
- Whether Diligent accepts a Slang-emitted second descriptor set via a per-module signature is **asserted from in-tree precedent, not proven on device**. Half a day of spike before 160.4's design freezes.

### The two early discoveries, executed — 2026-08-06, after T0159 closed

**160.2's dependency question is answered: no session-API migration.** The
engine compiles through `slang::ICompileRequest` (`SlangCompiler.cpp`), and the
pinned `slang.h` exposes full reflection on exactly that object —
`spGetReflection(request)` returns a `ProgramLayout` (slang.h:3686) with
parameter, type-layout, binding and **user-attribute** queries
(`findUserAttributeByName`, `getTypeLayout`, `getBindingIndex`). Import-time
reflection can ride the same deprecated-but-pinned API the compile does;
T0151.2 is *not* inherited as a dependency.

One reflection constraint discovered by reading rather than measuring, worth
settling in 160.1's vocabulary: **a module's declarations can sit inside
permutation `#if` blocks** — `rock_pom.slang`'s whole body is inside
`HP_USE_HEIGHT_MAP && USE_TEXCOORD0` — so "reflect the module" is only
well-defined per macro set. The cheap rule is that the reserved `cbuffer` and
declared resources must be **unconditional at module top level**, and the
import-time reflection pass compiles with a canonical macro set; decide it in
160.1 and say it in the authoring docs.

**160.4's failure-mode half is executed, on device** (the capability matrix
carried it as an open question): a module declaring
`cbuffer HpMaterialParams { float4 HpTint; }` and reading it compiles clean
under slang, and pipeline creation then fails **loudly, with the resource
named**:

    [error] render.diligent: Shader 'hp surface PS (slang)' contains resource
    'HpMaterialParams' that is not present in any pipeline resource signature
    used to create pipeline state 'hp surface'. (PipelineStateVkImpl.cpp:918)

The frame is the full missing-material checkerboard (centre (255, 0, 255),
4096/4096 magenta) and the renderer logs one substitution line naming the
module. **Loud, not silent** — pinned by
`tests/gpu/custom_shader_material_test.cpp`'s spike case on both targets.

**What that log line also proves about the mechanism**: Diligent's Vulkan
backend reflects the slang-emitted SPIR-V itself and matches resources to
signatures **by name** at PSO creation (that is where the refusal comes from).
The in-tree precedent is therefore stronger than the `addVkInputLocations`
note suggests: `cbFrameAttribs`, `cbPrimitiveAttribs`, `cbMaterialAttribs` and
`g_HeightMap` are all slang-emitted resources matched by name to signature
slots and **rebound by Diligent regardless of the set/binding slang chose** —
rendering correctly every frame. No `[[vk::binding]]` prologue injection is
needed for resources; that trick is only required for vertex *inputs*, which
Diligent does not remap. **What remains unproven on device** is narrower than
the ticket stated: whether an *additional* signature (the per-module one,
added beside `m_ResourceSignatures` via `AddSignature`) participates in that
same name-match-and-remap, and what the SRB commit order costs. That is the
first thing 160.4's implementation must render.

**A viable simplification for 160.4, found by the spike, to be weighed in
160.1**: a pipeline resource signature declares a constant buffer *by name
only* — no size — so a **single shared slot** named `HpMaterialParams` in
`CreateCustomSignature` (the exact superset pattern `g_HeightMap` uses) would
serve every module's differently-sized parameter block without any per-module
signature at all. Per-module *textures* cannot ride that shortcut when the
vocabulary lets authors pick names — fixed reserved texture-slot names would
be the price, and Godot's `uniform sampler2D` precedent argues authors accept
declared-name slots. The trade — author-chosen names + per-module signature
versus reserved names + one shared signature — is 160.1's to decide, and it
should be decided knowing the second option removes 160.4's only remaining
unknown.

### Why this outranks almost everything else

From the capability matrix: parameters unblock ~13 of 40 audited techniques, against ~9 for the light loop and ~6 each for time and the vertex hook. It is also the only item on that list a game developer hits on **day one** rather than when they attempt something advanced — the moment they want an artist to change a number.

---

## What landed — 2026-08-06

A game's `.slang` declares `cbuffer HpMaterialParams` with **its own field
names**, hints them with `[HpRange]` / `[HpColor]` / `[HpTooltip]`, samples up to
four texture slots, and a `.hpmat` gives all of it values by name. Changing a
value rebuilds nothing.

```slang
cbuffer HpMaterialParams
{
    [HpRange(0.0, 1.0)]
    [HpTooltip("Which height sits on the polygon plane.")]
    float referencePlane;
}
```

```yaml
version: 2
material:
  shader: 1570000000000020
  params:
    - name: referencePlane
      value: 0.5
```

### Measured, both targets

| What | Number |
|---|---|
| `tint`/`level` from a `.hpmat`, same module and pipeline | **(0, 255, 0)** and **(0, 0, 255)**, exact |
| `level: 0.5` on white — the scalar's own offset, after a `float3` | **(188, 188, 188)** (sRGB encode of linear 0.5) |
| a material setting nothing, after three white renders | **(0, 0, 0)** — the block is cleared, not stale |
| `HpTexture0` unbound / bound to the checkerboard | **(255, 255, 255)** / **(255, 0, 255)** |
| **the acceptance test**: `rock.hpmat` alone, plane 0.5 vs 1.0 | mean abs difference **25.1578**, **33207 of 47393** texels moved >10 luminance, covered **47393 both** |
| **byte-identical guard** — `no-height vs zero-scale` | **0** on all three parallax baselines, unchanged |
| the shipped rock frame, against T0159's record | **0.0449892** mean abs difference, **72** darkened >10, max drop **51.7406** — bit for bit |
| magenta guard | 0 on every frame asserted above |

`zig build all`, `zig build test -Dtest=all`, `zig build test -Dtest=gpu`,
`zig build docs` — clean, Linux and Windows-under-wine. Fast 320 cases /
214928 assertions; gpu 39 cases / 995 assertions.

### The three design decisions, and what each rejected

**One shared signature, so the buffer's name is the engine's and the fields are
the author's.** A pipeline resource signature is created once at renderer
construction, before any module exists, and Diligent matches a shader's
resources to it **by name** — so the *buffer* has to be a name the engine
already knows. Fields are not resources, so they cost the signature nothing and
a module names and orders them freely. This is the shortcut the ticket's own
spike found ("a signature declares a constant buffer by name only — no size")
and taking it removed 160.4's last unproven item outright: **no per-module
signature was needed, so nothing about a second descriptor set had to be
proven.**

**The price is that texture slots are named by the engine** — `HpTexture0` …
`HpTexture3`, declared in `HpMaterial.slang` so a module uses one without
declaring anything. Author-chosen names were designed twice and rejected twice:

- a **per-module signature** costs a second SRB and a second
  `CommitShaderResources` on every draw of every custom material;
- a **rename prologue** (`#define detailMap HpTexture0` ahead of the include,
  the `addVkInputLocations` trick the ticket pointed at) is **circular** — the
  reflection rides the compile, so the names are not known until after the
  compile that would have to be told them. The only thing that would break the
  circle is a deviceless import-time reflection pass, and that is the next
  finding.

Recorded in the capability matrix as the one widening this landing owes.

**Values are data, not permutations**, so nothing feeds them into the PSO key.
Module identity already keys the pipeline cache; the two materials in the gpu
case above render different colours through one pipeline.

### 160.2's deviceless clause is **not met**, and it is not met for a measured reason

The subtask said "**Deviceless**, which is what D28 promised the editor". It is
not, and the ticket would be overstating itself to tick that half quietly.

**A module is a fragment, not a program.** It names `IHpMaterial` and
`VSOutput` — the first declared by `HpSurface.slang`, the second generated per
permutation by `PBR_Renderer` — and since D27's amendment its bodies may reach
`g_HeightMap`, `g_Frame.Lights[]` and DiligentFX's getters. A translation unit
containing the module alone is a wall of undefined identifiers, and slang emits
**no** reflection at all for a failed compile: `slangc -reflection-json` writes
no file, with `-no-codegen` and without, measured on the pinned 2026.14.1.

So reflection reads the `ProgramLayout` of the **real pixel-shader compile**.
That is stronger than a separate pass, not weaker — there is one compile, so a
layout can never describe a shader different from the one the device runs, which
is the divergence T0142.13 exists to prevent. What it costs is that the compile
needs `PBR_Renderer::DefineMacros`, whose ~100 macros read `m_Settings` **and**
`m_Device.GetDeviceInfo().Features`. Reproducing that without a device means a
second, hand-maintained macro set — the second path D28 forbids.

**The consequence a person meets:** a material's declared parameters are known
once a pipeline has been built for its module, not while the shader is being
typed. T0032's Done-when carried the deviceless promise and has been struck
there with this reasoning; 32.8 and 32.9 now reference this ticket.

### The reflection API is C functions, and this engine links no Slang

Found at link time on the Windows target, as twenty-two undefined
`spReflection_*` symbols. Everything else in `SlangCompiler.cpp` goes through
COM vtables precisely so an MSVC-built DLL is callable from a MinGW-built engine
and so a shipped game inherits no link edge (D28's boundary table). The
reflection API is not vtable-shaped, so the entry points are resolved out of the
handle the loader already holds — once, with a library missing any of them
yielding no reflection rather than half a layout.
`ICompileRequest::getReflection()` is the one vtable method in the chain.

### A cooked build reflects from its SPIR-V, and the first version broke it

A shipped game links no Slang and its bytecode comes from an archive, so nothing
compiles and nothing reflects. Diligent parses the constant-buffer layout out of
the SPIR-V (`LoadConstantBufferReflection`) — the runtime-side mechanism D28
named when it argued *for* slang's reflection — over the same bytes, so offsets
cannot disagree. Only the hints are lost, and they are wanted by an editor,
which has a compiler. The same path runs when a module simply declares nothing,
so it stays exercised on every developer machine rather than only in a package.

Two bugs, both found by running it:

- forcing a compile to get a reflection did so in **cooked-only** mode too,
  where there is no compiler to fall through to — turning a working archive
  lookup into "not in any cooked shader archive" and no shader at all. Two
  cooked cases failed, one with a SIGSEGV.
- a `float3` arrived as **3x1, not 1x3**. Diligent's header says "for shaders
  compiled from GLSL, NumRows and NumColumns are swapped", and its SPIR-V reader
  performs that swap only when it believes the source was HLSL — which it cannot
  know for bytecode handed in directly. A vector has a 1 in the other slot
  either way, so the width is the larger of the two.

### What is deliberately not here

- **A parameter's default.** A module's block is zero-initialised, so a
  parameter no material sets reads zero rather than something the shader
  declared as sensible. `[HpDefault(...)]` is the obvious shape; nothing needs
  it yet, and a field nothing reads is the `Camera::cullingMask` mistake.
- **`params:` as a YAML *map*.** The ticket asked for one; it is a sequence of
  `{name, value}` entries. The reflected serializer bottoms out in sequences and
  leaves, and an associative-container leaf is T0020's work rather than this
  ticket's. Ordering by declaration is also what an inspector wants and what a
  diff reads, so the sequence is not purely a concession.
- **Matrices, arrays and nested structs in the block.** Reported by name in the
  log and left at whatever the shader initialises them to, never mis-sized.
- **Per-instance parameters**, as the scope note says. The trigger is recorded
  in the capability matrix instead of here, where nobody would find it.

### What could not be verified

- **Shader hot reload of a parameter block.** Editing a module's declarations
  while the engine runs is not picked up, for the reason `ShaderAsset`'s own
  comment already records: nothing invalidates a pipeline whose cache key
  already exists. That is T0032.7's, unchanged by this.
- **The 256-byte cap being enough.** Chosen against the capability audit's
  techniques, not against a real game. Overrunning it is loud (the module is
  named and its parameters are not written) rather than silent, which is the
  property that matters.
- **Cost.** No measurement of what the extra constant buffer and four texture
  descriptors do to a draw. They are bound on every SRB including materials that
  never read them, and the parallax baselines say the *image* is unchanged;
  nothing says the frame time is.
