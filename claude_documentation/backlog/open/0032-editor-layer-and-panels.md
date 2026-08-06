# T0032 — Editor layer and panel framework

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 6 — Editor |
| **Order** | 600 |
| **Created** | 2026-08-02 |
| **Refs** | [../completed/0142-slang-shader-language.md](../completed/0142-slang-shader-language.md) — **inherited its editor half** (142.8/142.9/142.10 → 32.7/32.8/32.9), including an undecided question this ticket must settle; [../completed/0141-custom-shader-materials.md](../completed/0141-custom-shader-materials.md) — the same work reached T0142 from 141.2/141.5 first, so the chain is 141.2 → 142.9 → 32.8 and 141.5 → 142.8 → 32.7; [../completed/0085-layers-and-masks.md](../completed/0085-layers-and-masks.md) — 85.6's mask editor widget also waits here; [0035-hierarchy-and-inspector.md](0035-hierarchy-and-inspector.md) — the inspector panel itself, which 32.8/32.9 describe the *contents* of; [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) **D28** (Slang), **D12** (the editor is a module host) |

## Why

The editor is added as **one layer** pushed onto the LayerStack, which is what
keeps the engine library ignorant that an editor exists. Inside it, UI is
organised as panels behind an `IEditorPanel` interface so the editor layer does
not itself become a dumping ground.

## Done when

- [ ] `EditorLayer : ILayer` lives in `apps/editor`, not in the engine
- [ ] `IEditorPanel` with an update entry point; the layer owns and iterates them
- [ ] An ImGui dockspace hosts panels, and docking actually works
- [ ] Dock layout persists across editor restarts
- [ ] `grep -ri editor engine/` returns nothing
- [ ] **A material inspector is built from shader reflection, with no device and
      no successful compile required** — inherited from T0142, which could not
      close it without an editor to put it in

## Subtasks

- [ ] 32.1 `EditorLayer` in `apps/editor`
- [ ] 32.2 ImGui context, Diligent renderer backend and platform wiring
- [ ] 32.3 Dockspace over the main viewport
- [ ] 32.4 `IEditorPanel` and the panel collection
- [ ] 32.5 Persist ImGui layout (`imgui.ini`) into the project or user config
- [ ] 32.6 Verify the engine has no editor references
- [ ] 32.7 **Shader hot reload in the editor** (was T0141.5, then T0142.8).
      A game's `.slang` module changes while the editor is open and the surface
      redraws with it, keyed on the same content hash everything else uses.
      **The engine half already exists** — an edited module is picked up
      whenever a new pipeline builds (T0142.15), and `ShaderAsset` re-reads its
      path through the VFS at pipeline-build time — so what is missing is the
      editor side: noticing the file changed, and forcing the rebuild. Slang's
      runtime API is built for it. A module that fails to recompile must land
      on T0141.4's path (the checkerboard plus one logged compiler error), not
      on a stale pipeline
- [ ] 32.8 **Material inspector from shader reflection** (was T0141.2, then
      T0142.9) — **and the mechanism is an open question, not a given.**
      See "The inspector's reflection question" below; do not start this by
      turning on Slang's reflection because it is the one this ticket
      inherited. The requirement is: a material's parameters appear in the
      inspector automatically, with sensible presentation, **before** the
      shader compiles and while the developer is typing
- [ ] 32.9 **One inspector over two reflection systems** (was T0142.10).
      Component fields come from `entt::meta` (T0053), material parameters from
      the shader. **These do not merge** — a shader reflector cannot see a
      `MeshRenderer` and never will. What unifies is the *presentation*: the
      editor should consume one description of "a named, typed, editable value"
      whichever side produced it. Getting this wrong means two inspectors that
      look different for no reason a user can explain

## Inherited 2026-08-06 from T0141 and T0142 — the editor half of the shader work

**Three items arrived here because they need scaffolding this ticket builds**,
not because they were hard or skipped. Both source tickets closed on what they
achieved rather than staying open near the top of the queue for work nobody
could start — the T0095 → T0105 and T0054/T0056 → T0025 shape.

| Now | Was | Then |
|---|---|---|
| **32.7** hot reload | T0141.5 | T0142.8 |
| **32.8** material inspector from reflection | T0141.2 | T0142.9 |
| **32.9** one inspector, two reflection systems | — | T0142.10 |

**What already exists, so this is genuinely editor work and not engine work:**

- Shaders are **Slang** (D28). A game's custom material is a `.slang` module
  implementing `IHpMaterial`, named by `Material::shader` (a `Guid`), arriving
  through the VFS as content (T0142.14/142.15). `ShaderAsset` is **device-free**
  — identity plus path plus load-time source — which is exactly what an
  inspector that must work before a compile needs.
- The generated contract reference is `docs/shaders/index.md` (T0141.14). Read
  it before designing a panel around `IHpMaterial`.
- A module that will not compile already renders the magenta checkerboard and
  logs the compiler's error once (T0141.4). **The inspector must not invent a
  second failure convention**; one pattern, three causes, the log says which.
- The compiled result is cached and cooked on a content hash of the resolved
  source (T0141.3, T0142.7 / **D34**), so a reload is a cache miss rather than
  a special path.

### The inspector's reflection question — undecided, and it must not be decided by default

This is the part that moved as *text* rather than as a pointer, because it is a
live decision and a closed ticket is not where a live decision survives.

**32.8's inherited plan was Slang's source reflection. That is not
automatically right**, and the alternative is nearly free:

- **Diligent already reflects constant-buffer contents** — name, type, offset,
  array size, nested members — through `IShader::GetConstantBufferDesc()`, and
  it is **one bool away**: `LoadConstantBufferReflection`, currently `false`.
  It is authoritative, because it comes from the compiled shader and therefore
  cannot drift from it.
- **Slang's reflection buys two things Diligent's structurally cannot.** It
  needs **no device and no successful compile**, so the panel renders while the
  developer is mid-edit — which is most of what makes an inspector feel alive.
  And **user-defined attributes survive into it**: `[Range(0.0, 1.0)] float
  roughness;` comes back from `-reflection-json` as `userAttribs: [{name:
  "Range", arguments: [0.0, 1.0]}]`. That is Godot's `hint_range` shape,
  carried by the compiler, with **no annotation parser to write** — measured on
  the pinned `slangc 2026.14.1`, not assumed. `ParameterBlock<T>` also works on
  SPIR-V, each block landing in its own descriptor set, which is the typed
  alternative to a hand-rolled constant buffer for a material's parameters.
- **The original plan from T0141.2 — annotate the source and write a parser —
  is beaten by both**, and should not come back.
- The two are not exclusive: layout from one, presentation hints from the
  other, is a legitimate answer. What is not legitimate is picking one because
  it is the one written down here. **Record what was rejected and why.**

**Where the values live on the GPU is already answered and should be checked
before a buffer is invented**: `PBR_Renderer` reserves a per-draw custom-data
region (`GetPBRPrimitiveAttribsSize(..., CustomDataSize)` with
`PSO_FLAG_ENABLE_CUSTOM_DATA_OUTPUT`), and `Material::ShaderAttribs::CustomData`
is a per-material `float4` of the same kind — the engine already rides
`heightScale` and `triplanarScale` in it (T0141.7/141.8). A custom shader with
four floats of parameters should not cost a constant buffer of its own.

**A parameter present in the annotation but absent from the compiled buffer is
a warning, not a silent omission.** That rule came with the work and survives
whichever mechanism wins.

## Notes / findings

**ImGui docking is already proven working** — that was the whole point of the
earlier probe app: `ImGui 1.92.9b, docking ON`, verified on both OpenGL and
Vulkan, and a screenshot showing a window docked into a dockspace. Link
`Diligent-Imgui`, not raw ImGui; it carries `ImGuiDiligentRenderer` and the
per-platform impls.

**ImGui is not optional anyway** — `DiligentFX` links `Diligent-Imgui` PUBLIC and
its post-process components call `ImGui::` for their settings panels (D6).

Decide where `imgui.ini` lives. Per-project keeps layouts with the work; per-user
avoids churning the project on every window drag. Per-user is the usual choice
and probably right.


### Architecture decision (2026-08-03) — the editor is a module host (D12)

The editor loads the gameplay module, exactly as the runtime does. That is not
an incidental capability — it is how the editor knows about game-defined types
at all, and it is the model this project is following deliberately (Godot loads
extensions into the editor for the same reason).

Consequences this ticket did not previously account for:

- The editor links the **shared** engine library and hosts modules through the
  same loader the runtime uses. The build-id check (T0104) lives in that shared
  loader, or the editor will cheerfully load a stale module and the failure will
  present as a broken panel
- In-editor hot reload becomes first-class rather than a runtime-only concern
  (T0048), because the editor is where reload actually gets used
- Panels that display game-defined types depend on the module being loaded, so
  "no module loaded" and "module failed to load" are real editor states that
  need designing, not error paths to bolt on
