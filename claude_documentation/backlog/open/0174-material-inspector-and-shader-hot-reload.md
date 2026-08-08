# T0174 — The material inspector, and shader hot reload in the editor

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 6 — Editor |
| **Order** | 605 |
| **Created** | 2026-08-08 |
| **Splits from** | **T0032**, which keeps the shell — the layer, the ImGui context, the dockspace, the panel collection and the layout. This ticket is the functionality that accreted onto it |
| **Refs** | **T0032** — builds the `IEditorPanel` collection these panels register into; do not build a second panel mechanism. [../completed/0142-slang-shader-language.md](../completed/0142-slang-shader-language.md) (142.8/142.9/142.10 arrived here via T0032); [../completed/0141-custom-shader-materials.md](../completed/0141-custom-shader-materials.md) (141.2 → 142.9 → 32.8 → **174.2**; 141.5 → 142.8 → 32.7 → **174.1**); [../completed/0160-material-declared-parameters.md](../completed/0160-material-declared-parameters.md) — **answers the reflection question with measurements** and builds `hp::ShaderParamLayout`, which 174.2 consumes; [0035-hierarchy-and-inspector.md](0035-hierarchy-and-inspector.md) — the inspector panel these are the *contents* of; [../completed/0085-layers-and-masks.md](../completed/0085-layers-and-masks.md) — 85.6's mask editor widget; **D28** (Slang), **D34** (cooked shaders) |

## Why this is not T0032

The owner's framing for T0032 was *"I want docks so the editor turns out awesome
and clean. We need a good architecture flow so that we start on the right foot.
We don't want to add any major functionality yet."*

These three subtasks are major functionality. They are here because two closed
tickets needed an editor to put their work in and T0032 was the only editor
ticket — not because they belong with a dockspace. Keeping them there makes the
shell ticket read as Complex and unfinishable, and it is the accretion the
render-ticket merge pass of 2026-08-08 was cleaning up in the other direction.

| Now | Was | Before that |
|---|---|---|
| **174.1** shader hot reload | 32.7 | T0142.8 ← T0141.5 |
| **174.2** material inspector from reflection | 32.8 | T0142.9 ← T0141.2 |
| **174.3** one inspector over two reflection systems | 32.9 | T0142.10 |

## Done when

- [ ] A game's `.slang` module edited while the editor is open redraws the
      surface without a restart
- [ ] A material's declared parameters appear in the inspector with their
      `[HpRange]` / `[HpColor]` / `[HpTooltip]` presentation, automatically
- [ ] Component fields and material parameters are drawn by **one** presentation
      path, so they do not look different for no reason a user can explain

## Subtasks

- [ ] 174.1 **Shader hot reload in the editor.** A game's `.slang` module
      changes while the editor is open and the surface redraws with it, keyed on
      the same content hash everything else uses. **The engine half already
      exists** — an edited module is picked up whenever a new pipeline builds
      (T0142.15), and `ShaderAsset` re-reads its path through the VFS at
      pipeline-build time — so what is missing is the editor side: noticing the
      file changed, and forcing the rebuild. Slang's runtime API is built for it.
      A module that fails to recompile must land on T0141.4's path (the
      checkerboard plus one logged compiler error), **not** on a stale pipeline
- [ ] 174.2 **Material inspector from shader reflection.** Read
      `SurfacePipeline::paramLayout(module)` through whatever accessor the editor
      gets. See the settled reflection question below — **do not re-open it**
- [ ] 174.3 **One inspector over two reflection systems.** Consume
      `hp::PropertyMeta` from both sides and nothing else

## Notes / findings

### The reflection question is settled — T0160, with measurements

T0032 carried a long section insisting this "must not be decided by default".
T0160 then needed the same reflection for the *renderer* side and settled it on
evidence. Both mechanisms are in the tree and they are **not alternatives**:

- **Slang's reflection is primary**, and it carries the hints. `[HpRange]`,
  `[HpColor]` and `[HpTooltip]` are engine-defined attributes declared in
  `HpMaterial.slang`, and slang delivers them into reflection with their
  argument values. The tooltip arrives as its *source text*, quotes included.
- **Diligent's `GetConstantBufferDesc` is the fallback**, for bytecode that came
  from a cooked archive with nothing compiled. Same bytes, so the offsets cannot
  disagree; hints do not survive into SPIR-V, and an editor always has the
  compiler. `LoadConstantBufferReflection` is on only for pipelines with a
  custom module.
- **Annotate the source and write a parser** (T0141.2's original plan) stays
  rejected, beaten by both.

**The deviceless promise is struck, and this is the measurement.** A module names
`IHpMaterial` and `VSOutput`, and since D27's amendment its bodies may reach
`g_HeightMap`, `g_Frame.Lights[]` and DiligentFX's getters — so a translation
unit containing the module alone is a wall of undefined identifiers. Slang emits
**no** reflection for a failed compile (`slangc -reflection-json` writes no file,
with `-no-codegen` and without, on the pinned 2026.14.1). So reflection rides the
real `HpSurface.slang` compile, which needs `PBR_Renderer::DefineMacros` — ~100
macros read from `m_Settings` *and* `m_Device.GetDeviceInfo().Features`.
Reproducing that without a device means a second, hand-maintained macro set,
which is the second path D28 forbids.

**So a material's parameters are known once a pipeline has been built for its
module** — after the first frame that draws it. Good enough for an inspector, and
*not* "while the developer is typing". Do not re-promise that.

### What 174.3 must consume

`ShaderParam::meta()` already returns exactly the struct a reflected C++ property
carries — min, max, tooltip, read-only, hidden — so the unification is already
available as data and needs no second description invented in the panel. The one
thing the two genuinely do not share is *identity*: a component field is an
`entt::meta_data` on a type, a material parameter is a name in a `.hpmat`'s
`params` list.

### The parameter buffer is settled too, and the answer is not `CustomData`

Riding the per-material `float4` was the suggestion; it does not survive contact
with author-declared parameters, whose count and layout the engine cannot know. A
module declares `cbuffer HpMaterialParams` and the engine binds one 256-byte
buffer to a slot the shared signature names. `CustomData.x`/`.y` stay the
engine's own `heightScale` and `triplanarScale`.

**A parameter present in the annotation but absent from the compiled buffer is a
warning, not a silent omission.** That rule came with the work and survives.

### Read before designing a panel

- The generated shader contract reference is [`docs/shaders/index.md`](../../../docs/shaders/index.md) (T0141.14).
- A module that will not compile already renders the magenta checkerboard and
  logs the compiler's error once (T0141.4). **The inspector must not invent a
  second failure convention**; one pattern, three causes, the log says which.
- The compiled result is cached and cooked on a content hash of the resolved
  source (T0141.3, T0142.7 / **D34**), so a reload is a cache miss rather than a
  special path.
</content>
