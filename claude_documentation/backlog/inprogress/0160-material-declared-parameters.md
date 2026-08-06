# T0160 — Material-declared parameters and resources: a game's shader gets its own knobs

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
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

- [ ] A game's `.slang` **declares its own parameters and textures**, and a `.hpmat` carries values for them
- [ ] Those parameters appear in the **inspector** through the same reflection everything else uses (D23) — no second mechanism
- [ ] Changing a value **rebuilds no pipeline and invalidates no cook** — values are data, not permutations
- [ ] `rock_pom.slang`'s reference plane stops being a literal and becomes a parameter, **as the proof**
- [ ] The `.hpmat` format document gains its section, and the generated shader reference explains the declaration syntax

## Subtasks

- [ ] 160.1 **The declaration vocabulary** — a reserved `cbuffer` name plus module-declared textures and samplers, and the engine-defined attributes for hints (`[HpRange]`, `[HpColor]`, tooltip). The attribute *definitions* live in `HpMaterial.slang` so a module still includes nothing
- [ ] 160.2 **Reflection at import**: a pass over the module producing an `hp::ShaderParamLayout` — names, types, offsets, hints, resources — stored with the `ShaderAsset`. **Deviceless**, which is what D28 promised the editor. Check whether the in-process reflection API is reachable from the compile path we use, or whether it forces T0151.2's session-API migration; that is a dependency to discover early, not late
- [ ] 160.3 **`.hpmat` carries values**: a `params:` map and a `textures:` map, lenient like every other field, with one schema bump. `Material` gains an ordered name/value store rather than typed fields
- [ ] 160.4 **Binding**: a second, per-module resource signature beside the existing ones. The precedent for coordinating Slang's assignments with a declared signature is in-tree — `addVkInputLocations` already injects `[[vk::location]]` into generated source, and the same prologue trick pins bindings. **Spike this before the design freezes**; whether Diligent accepts it is asserted, not measured
- [ ] 160.5 **The writer**: `SceneRenderer`'s material binding fills the parameter buffer from the layout's offsets and binds GUID-resolved textures, falling back to the placeholder exactly as the fixed slots do
- [ ] 160.6 **Inspector rows** merge with reflected component fields — the unification D28's "one description, two reflections" anticipated
- [x] 160.7 **Register the permutation answer with T0151**: parameters add **no** PSO axis, because module identity already keys the pipeline. Said there explicitly 2026-08-06 — its Notes now carry "prefer a runtime parameter over a permutation bit" with the reasoning, and its Refs point back here
- [ ] 160.8 **Update the capability matrix** for every cell this lands, which should be the largest single edit that document ever takes

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
