# T0169 — An import produces engine assets: every type a DCC tool exports becomes an `hp::` type on disk

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 7 — Content pipeline |
| **Order** | 460 |
| **Created** | 2026-08-08 |
| **Blocked by** | nothing |
| **Refs** | [../completed/0168-asset-import-coverage.md](../completed/0168-asset-import-coverage.md) — **the sibling and the prerequisite in practice**: it decides *which features survive the loader*, this decides *what an import produces on disk*, and a feature that does not survive cannot be written out; [../open/0167-sketchfab-asset-validation.md](../open/0167-sketchfab-asset-validation.md) — the asset that made this concrete, whose single material is unreachable today; [../completed/0023-asset-manager.md](../completed/0023-asset-manager.md) — the GUID and `.hpmeta` identity this extends, and **which already names sub-asset GUIDs as an open gap**; [0024-project-manager.md](../open/0024-project-manager.md) — a scene cannot reference an asset by path, which is why hand-written `.hpmeta` files exist at all; [0036-assets-panel.md](../open/0036-assets-panel.md) — the editor surface that would show and re-import these; [0097-texture-import-pipeline.md](../open/0097-texture-import-pipeline.md) — **owns the texture half**, and its settings are per-import settings by another name; [0038-fbx-to-gltf-converter.md](../open/0038-fbx-to-gltf-converter.md) — a converter's output lands here; [0043-export-pipeline.md](../open/0043-export-pipeline.md) — what ships must be derivable from what was imported; [../../documentation/11-material-format.md](../../documentation/11-material-format.md) — the `.hpmat` schema this would finally write; **D13**, **D23**, **D35** |

## Why

**A glTF import today produces one opaque thing.** `importAsset` loads the file, mints a `.hpmeta` with a GUID, and hands back a `MeshAsset` wrapping Diligent's model. Everything *inside* that model — its materials, its meshes, its textures, its node hierarchy — stays inside it, addressed by index, owned by the loader, and reachable by nothing.

The Aston Martin (T0167) makes the cost concrete. The engine logs `1 meshes, 1 materials, 1 nodes` for the sample cube and would log 31 meshes and 1 material for the car — and **not one of those materials can be inspected, overridden, reassigned, or pointed at by a scene**. There is no GUID for it. There is no file. A game author cannot change the car's roughness without editing the `.glb`.

**`writeMaterial` already exists and has no caller outside a unit test.** `Material.hpp:556` declares it, `Material.cpp:145` implements it, `tests/fast/material_test.cpp` is the only thing that has ever called it. The engine can write a `.hpmat`; nothing asks it to. That is the shape of this whole ticket — **the pieces are there and nothing joins them.**

**This is what "import" means in every engine a game developer has used.** Godot's `.import` sidecar with per-file settings and generated sub-resources; Unity's importer with a settings inspector and `Extract Materials`; Unreal's import dialog producing separate asset objects. In all three, dropping a model in the project yields *addressable, overridable engine objects*, and re-importing preserves the overrides. **The owner's requirement is that this engine does the same, and it is not a nicety** — an engine whose imported materials cannot be edited is one a studio works around rather than with.

**And it explains a finding rather than adding one.** T0167's spec-gloss problem is not only that `SceneRenderer.cpp:329` hardcodes metallic-roughness — it is that there is *no artefact* on which a workflow could be recorded, corrected or overridden. Fixing the mapping without this leaves the material unreachable either way.

## Done when

- [ ] Importing a glTF produces **engine-native assets on disk** — a `.hpmat` per material at minimum — each with its own GUID, resolvable by a scene and visible to the editor
- [ ] **Every type the format carries has a decision**: mapped to an `hp::` type, deliberately flattened, or recorded as not-yet — with **no type silently dropped**
- [ ] **Re-import preserves overrides.** Changing the source file and re-importing does not discard edits made to the generated assets, and what happens on conflict is written down
- [ ] Per-import settings live in a file beside the asset (the `.hpmeta` extension, or its successor) and are **reflected and serialised like everything else** (D23)
- [ ] A game author can change an imported material's parameters **without editing the source file**, and the change survives
- [ ] The Aston Martin's material is a file with a GUID

## The mapping table this must produce, and it is the ticket's spine

Every row is a decision, and **"we did not think about it" must not be reachable**:

| glTF carries | Engine type today | Decision needed |
|---|---|---|
| material | `hp::Material` / `.hpmat` — **exists, never written by an import** | the flagship; `writeMaterial` is already there |
| mesh / primitive | inside `MeshAsset`, index-addressed | sub-asset GUIDs, T0023's named gap |
| image / texture | `AssetKind::Texture` exists standalone | embedded images have no file — extract, or reference in place? T0097 |
| sampler | folded into Diligent's texture | filtering and wrap are material data in `.hpmat` |
| node hierarchy / scene | flattened at draw | does an import produce a `.hpscene` fragment or a prefab? **Not yet designed** |
| camera | `hp::Camera` exists | imported or ignored — decide, do not drift |
| `KHR_lights_punctual` | `hp::Light` exists | same |
| animation | nothing | ozz-animation is vendored ([T0041](../open/0041-ozz-animation.md)/[T0049](../open/0049-animation-runtime.md)) — a row, not a build |
| skin / joints | vertex attributes exist in the loader's defaults | same |
| morph targets | nothing | 07-design-gaps already names these |

**The table is the deliverable as much as the code is.** T0168 builds the same shape for *format features*; this one is for *engine types*, and between them a person can answer "what happens to my file" without reading the loader.

## Subtasks

- [ ] 169.1 **Design the artefact before writing one.** What an import emits, where it goes, how a sub-asset gets a stable GUID that survives re-import and reordering, and what a `.hpmeta` grows into. **Stable identity is the hard part and the whole thing rests on it** — an index is not an identity, because inserting a material in the DCC tool renumbers every one after it and silently repoints every scene reference. Godot keys on a path within the resource, Unity on a `fileID` map in the meta; both exist because index-keying failed. **A decision-log entry, not a comment**
- [ ] 169.2 **Materials first, end to end.** `writeMaterial` exists, `.hpmat` has a schema, `.hpmeta` has GUIDs — this is joining three things that are already built, and it is the subtask that proves the design. **Do it before generalising**
- [ ] 169.3 **Re-import and override.** The behaviour that separates an import pipeline from a one-shot converter. State the conflict policy explicitly — source wins, override wins, or three-way — and **prove an override survives a re-import with a test**
- [ ] 169.4 **Walk the table above and give every row a decision**, including the ones whose decision is "not yet, and here is the ticket". A row left blank is the failure mode D35 exists to prevent
- [ ] 169.5 **The editor surface** — what an import shows, what its settings look like, and where re-import is triggered. Coordinate with [T0036](../open/0036-assets-panel.md) rather than inventing a second panel
- [ ] 169.6 **Textures**: embedded images have no file of their own, so an import must extract them or address them in place. **This is [T0097](../open/0097-texture-import-pipeline.md)'s territory and must be settled with it, not around it** — the Aston Martin carries 48 MiB of embedded PNGs, so "extract everything" is not free
- [ ] 169.7 **Prove it on the asset that motivated it.** The car's material becomes a `.hpmat` with a GUID, a scene references it, and a parameter changed in the file changes the render
- [ ] 169.8 **Say what an import must never do.** It must not silently drop a type, must not renumber identities, and must not require the source file at run time (D13: everything is addressed through the VFS, and a shipped game has no `.glb` to reach back into)

## Not in scope

- **FBX, OBJ, USD.** [T0038](../open/0038-fbx-to-gltf-converter.md) converts *to* glTF; this ticket starts where a glTF exists. Deliberate, and it is what keeps the mapping table finite (**D13**: one mesh format).
- **Which glTF features survive the loader.** [T0168](../completed/0168-asset-import-coverage.md)'s question. This ticket assumes a feature arrives and asks what becomes of it; where T0168 says a feature does not arrive, the row here reads "blocked on T0168" rather than being solved twice.
- **An animation system.** 169.4 gives animation and skinning a row and a ticket. Building either is [T0041](../open/0041-ozz-animation.md)/[T0049](../open/0049-animation-runtime.md)'s.
- **The cooked/shipped form.** [T0043](../open/0043-export-pipeline.md) owns export. What this ticket owes it is that the shipped form is *derivable* — 169.8's last clause.

## Notes / findings

### Why Complex rather than Moderate

Not the code — the identity. A generated sub-asset needs a GUID that is stable across re-import, across the DCC tool reordering its material list, and across a file being replaced wholesale. Get it wrong and the failure is silent and unbounded: every scene reference repoints to the wrong material, and it looks like an art bug. **Godot and Unity both arrived at a keyed map rather than an index, and both did so after the index version failed.** That is 169.1, it is a decision-log entry, and everything else is easy by comparison.

### The pieces that already exist, which is why this is joining rather than building

- `writeMaterial` (`Material.cpp:145`) — **implemented, zero callers outside tests**
- `.hpmat` with a documented schema (`11-material-format.md`, schema 3 as of T0143)
- `.hpmeta` sidecars carrying `version`, `guid`, `type` — minted by `importAsset` when absent (T0023)
- `AssetKind` already distinguishes `Texture`, `Mesh`, `Material`, and reflection-driven serialisation (D23) already handles the writing

### The observation that started it

The engine logs `loaded model 'models/cube.gltf' (1 meshes, 1 materials, 1 nodes)`. **It counts the material it cannot give you.**
