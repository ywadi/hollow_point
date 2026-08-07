# T0162 — The shader authoring guide: what a game author reads, and why the generator cannot produce it alone

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 467 |
| **Created** | 2026-08-07 |
| **Blocked by** | nothing, but it should be written *after* the capabilities it documents — [0146-vertex-stage-hook.md](../completed/0146-vertex-stage-hook.md), [0147-engine-intermediates-for-shaders.md](../inprogress/0147-engine-intermediates-for-shaders.md), [0145-lighting-stage-own-the-light-loop.md](0145-lighting-stage-own-the-light-loop.md) — or it documents a moving target |
| **Refs** | [../completed/0118-generated-api-reference-for-agents.md](../completed/0118-generated-api-reference-for-agents.md) — the C++ half, and the precedent for generating rather than hand-cranking; [../completed/0141-custom-shader-materials.md](../completed/0141-custom-shader-materials.md) 141.14 — which generated `docs/shaders/` in the first place; [../completed/0159-open-the-material-contract.md](../completed/0159-open-the-material-contract.md), [../completed/0160-material-declared-parameters.md](../completed/0160-material-declared-parameters.md), [../completed/0161-game-resource-model.md](../completed/0161-game-resource-model.md) — **each records what it could not express as a declaration; those lists are this ticket's input**; [../../documentation/09-gameplay-authoring.md](../../documentation/09-gameplay-authoring.md) — the prose precedent, for C++ gameplay; [../../documentation/13-shader-capability-matrix.md](../../documentation/13-shader-capability-matrix.md) — a planning document for *us*, deliberately **not** this; **D26**, **D27** (amended), **D28**, **D35**; **T0146** ([../completed/0146-vertex-stage-hook.md](../completed/0146-vertex-stage-hook.md)) — **the vertex hook is new authoring surface this ticket must cover**: `IHpMaterial.vertex()`, `HpVertexInput`/`HpVertexOutput`, the object-space decision (**D36**), the two rules nothing enforces (do not invert a triangle; members do not cross the stage boundary), and the four `Custom` interpolators. The flat-shaded-normals gotcha the rock cube sample records is exactly the kind of convention this ticket exists to surface; **T0147** ([../inprogress/0147-engine-intermediates-for-shaders.md](../inprogress/0147-engine-intermediates-for-shaders.md)) / **D37** — the contract grew a whole section (screen resources, the four helper functions, the game-fed texture feed) and one field, `HpSurfaceInput::ScreenUV`. **The obligation this places on the generated authoring doc is that it must carry the *validity rules*, not only the symbols**: an author who learns `HpSceneColour` exists without learning it requires `alphaMode: Blend` gets a missing-material checkerboard and a log line, which is exactly the invisible-convention failure this ticket was opened for |

## Why

**The conventions a game author must obey are invisible in the one document
generated for them.** Measured 2026-08-07 against `docs/shaders/`, before T0161
landed:

- `HpRangeAttribute` and its fields — present, because they are declarations.
- `HpMaterialParams` — the reserved buffer name an author must spell exactly —
  appeared only incidentally inside a doc comment.
- `HpTexture0…3` — **zero occurrences in `IHpMaterial.md`**.
- The rule *"declare a `cbuffer` with this exact name and your fields are
  yours"* — nowhere at all.

**The cause is structural, not an oversight.** `tools/gen_shader_docs.py`
extracts **declarations** classified `hp-shader-doc: public`/`export`. A name the
*engine* reserves and the *author* declares in their own module is not a
declaration in our contract, so there is no symbol to hang it on. Everything
T0161 landed has the same property: an author writing
`Texture2DArray detailAlbedo;` produces no symbol in any engine header.

T0161 closed the part that *could* be closed this way — it taught the generator
to capture top-level globals, so the sampler palette and the deprecated slots now
generate, and it moved the conventions prose into `HpMaterial.slang`'s preamble,
which the generator already emits. **What remains is the part no generator can
reach**: a guide that teaches somebody to write a shader, rather than a reference
that lists what exists.

**The precedent is exact.** `09-gameplay-authoring.md` is prose for gameplay C++
and `docs/api/index.md` is generated for the same audience. Shaders have the
generated half and not the prose half.

## Done when

- [ ] A game author can learn to write a **material, vertex, compute and
      post-process module from one document**, without reading a ticket or a
      decision-log entry
- [ ] The conventions that are not declarations are where the author will find
      them — reserved names, the engine-fed versus game-declared split, the
      sampler palette and its cap, what a `.hpmat` may say
- [ ] The generated reference **includes** the hand-written sections rather than
      duplicating them, so there is one source and the existing CI gate covers
      both
- [ ] `docs/shaders/index.md` reads as an entry point rather than a symbol dump
- [ ] **Every worked example compiles** — a guide that ships a broken snippet is
      worse than one that ships none

## Subtasks

- [ ] 162.1 **Audit what T0159, T0160 and T0161 could not express as
      declarations.** Each ticket's findings already name them; start from those
      lists rather than re-deriving. T0161's is the longest
- [ ] 162.2 **The guide**: one document, four stages, written as *the code an
      author writes* rather than as description
- [ ] 162.3 **Teach the generator to include hand-written sections** at a
      declared point, so prose and generated symbols ship as one page under one
      check. This is the piece that stops the two drifting
- [ ] 162.4 An entry point in `docs/shaders/index.md` and a row in `CLAUDE.md`'s
      table
- [ ] 162.5 **Compile every fenced example.** Consider a test that extracts and
      builds them, the way the API reference is gated
- [ ] 162.6 Cross-reference `09-gameplay-authoring.md` both ways — one is C++
      gameplay, the other is shaders, and an author arriving at either should
      find the other

## Not in scope

- **The capability matrix.** It is a planning document for *us* — what is not
  yet possible, and which ticket owns it. An authoring guide says what *is*
  possible and how. Conflating them would spoil both.
- **A documentation website.** Markdown, in the repository, gated by the check
  that already exists.
- **Teaching shader programming.** The audience knows what a normal map is; it
  does not know this engine's contract.

## Notes / findings

### Why this is High rather than Medium

D28 makes a game's shader module *the* authoring surface for anything the engine
does not do itself, and T0159/T0160/T0161 have now made that surface genuinely
capable — a module reaches DiligentFX, keeps state across hooks, declares its own
parameters and resources under its own names, and reads `Time` and a real
tangent. **None of that is discoverable.** The gap between what the engine can do
and what an author can find out how to do is now the widest it has ever been, and
it grows with every ticket in the current sequence.
