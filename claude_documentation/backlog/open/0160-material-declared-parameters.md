# T0160 — Material-declared parameters and resources: a game's shader gets its own knobs

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 466 |
| **Created** | 2026-08-06 |
| **Blocked by** | nothing — independent of [0159-open-the-material-contract.md](0159-open-the-material-contract.md), and the two together are "a game can do anything" |
| **Refs** | [../../documentation/13-shader-capability-matrix.md](../../documentation/13-shader-capability-matrix.md) — **this is the single largest row-unblocker in it, ~13 techniques**; [../../documentation/11-material-format.md](../../documentation/11-material-format.md) — the `.hpmat` gains a section; [0151-shader-variants-and-compile-cost.md](0151-shader-variants-and-compile-cost.md) — **registers that parameters add no PSO axis**, which is the point; [0035-hierarchy-and-inspector.md](0035-hierarchy-and-inspector.md) — the inspector rows these produce; [0153-surface-detiling.md](0153-surface-detiling.md), [0149-style-bundles.md](0149-style-bundles.md) — consumers; **D13**, **D23**, **D28**, **D34** |

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
- [ ] 160.7 **Register the permutation answer with T0151**: parameters add **no** PSO axis, because module identity already keys the pipeline. Say it there explicitly, because "prefer a runtime parameter over a permutation bit" is the rule this makes available
- [ ] 160.8 **Update the capability matrix** for every cell this lands, which should be the largest single edit that document ever takes

## Not in scope

- **Per-instance parameters** (Godot's `instance uniform`). Wanted, and a different mechanism — `PBRPrimitiveAttribs.CustomData` is the place to investigate, and it is currently **undefined memory**: the layout exists, the write is skipped when null, and the engine passes null. Record the trigger and leave it.
- **A material graph or any authoring UI.** Text declaration and an inspector row; a node editor is a much later question.

## Notes / findings

### Measured 2026-08-06

- Slang reflection reports names, offsets, sizes and bindings for a module-declared `cbuffer` and its resources — verified via the CLI's `-reflection-json`. **The in-process equivalent on the compile path this engine uses is asserted, not verified**, and if it forces the session-API migration then this ticket inherits T0151.2 as a dependency.
- User-defined attributes survive into reflection with their argument values.
- Whether Diligent accepts a Slang-emitted second descriptor set via a per-module signature is **asserted from in-tree precedent, not proven on device**. Half a day of spike before 160.4's design freezes.

### Why this outranks almost everything else

From the capability matrix: parameters unblock ~13 of 40 audited techniques, against ~9 for the light loop and ~6 each for time and the vertex hook. It is also the only item on that list a game developer hits on **day one** rather than when they attempt something advanced — the moment they want an artist to change a number.
