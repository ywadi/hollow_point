# T0146 — The vertex stage: own the vertex main, and give games a vertex hook

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 456 |
| **Created** | 2026-08-06 |
| **Refs** | **T0161** ([../completed/0161-game-resource-model.md](../completed/0161-game-resource-model.md)) / **D35** — a vertex module's own resources (a wind field, a displacement map) come from `buildModuleSignatureDesc` (`engine/src/ModuleResourceSignature.hpp`): pass this stage's base-signature names and a `ModuleSignaturePolicy`, bind the result beside the pass signature — do **not** invent a second mechanism, and note the sampler palette is already declared `VS_PS` so vertex-stage sampling needs no base-signature change; **D30**/**D32** ([../../documentation/02-decision-log.md](../../documentation/02-decision-log.md)); [../completed/0141-custom-shader-materials.md](../completed/0141-custom-shader-materials.md) — 141.7's vertex-displacement note names this exact change; [../completed/0142-slang-shader-language.md](../completed/0142-slang-shader-language.md) — one compiler for both stages is what makes this cheap; T0041 — skinning must be built against *our* vertex shader, see "Sequencing"; [0155-terrain-rendering.md](0155-terrain-rendering.md) — **the likely first consumer of 146.7's tessellation**, whose 155.2 LOD decision is where it is first *wanted*; [0156-parallax-under-triplanar.md](../completed/0156-parallax-under-triplanar.md) — 156.6 evaluated silhouette POM (2026-08-06) and came back **negative**, so 146.7's tessellation is the only silhouette answer — see 146.7; [0145-lighting-stage-own-the-light-loop.md](0145-lighting-stage-own-the-light-loop.md) — the same ladder, one stage earlier |

## Why

**The vertex shader is the one stage still Diligent's.** `SurfacePipeline.cpp`
names it: `kVertexShader = "RenderPBR.vsh"` — with a comment saying vertex
displacement (141.7) is what changes it. D26 stopped one stage short of its own
principle: the engine owns the pixel `main` and not the vertex one, so a game
can displace *texture coordinates* per fragment and cannot move a *vertex* at
all.

That forecloses the hook Godot developers use most. In Godot, `vertex()` is
where wind sway, billboarding, breathing, jelly, flag flap and every cheap
vertex animation lives — it is the workhorse of stylised motion, and it is rung
zero of vertex-stage control. HollowPoint has no equivalent, and no ticket owned
the gap (T0141.7's vertex-displacement half assumed it in passing).

**The cost is bounded, and the shape is exactly D26's.** `RenderPBR.vsh` is
257 lines including the skinning block. The transform itself is public library
code — `Shaders/PBR/public/VertexProcessing.fxh` provides
`GLTF_TransformVertex` — so the engine's vertex main mirrors the orchestration
and calls their public helper, the same our-main-their-library split the pixel
shader already made. The skinning block (~30 lines of joint blending) is
mirrored with it.

**Measured, 2026-08-06:** slang compiles a module carrying both
`[shader("vertex")]` and `[shader("fragment")]` entry points to **one SPIR-V
module with two `OpEntryPoint`s** in a single compile (probe on the pinned
2026.14.1). Today the engine issues two compile requests per pipeline — one for
their `.vsh`, one for our `.slang`. Owning both stages in one file makes one
compile of one module, which is a real bite out of the measured 2–4x cold
compile (142.6), on top of the capability.

## Sequencing

- **Before T0041 (skinning runtime).** T0041 will build the joint-palette
  draw path against whatever vertex shader exists. If that is Diligent's, the
  move here relocates skinning afterwards — the same pay-twice shape as
  T0086 against T0145, and the reason this sits early in the order.
- **Motion vectors will land in this file.** T0111's TAA prerequisites (motion
  vectors, T0101.5) are vertex-stage work — previous-frame positions come out
  of the vertex shader. Owning the file first means T0111's remainder lands in
  our code once; this ticket only leaves the seam named, it builds nothing of
  TAA.

## Done when

- [ ] The engine's own vertex shader drives every draw, and the rendered
      output is **byte-identical** to the current build (same baseline
      discipline as 141.10/142.3)
- [ ] The contract has a vertex method with a default — a material that says
      nothing gets today's behaviour exactly
- [ ] A worked example displaces vertices (wind sway or a breathing scale) and
      renders correctly, silhouettes included — the thing 141.7's parallax
      explicitly cannot do
- [ ] The **space the hook works in is decided and documented** — Godot's
      `world_vertex_coords` / `skip_vertex_transform` question: what the
      method receives (object or world space), what it returns, and what the
      engine does after. One decision, written, not three defaults discovered
- [ ] **Custom interpolators are decided deliberately.** `VSOutput` is
      generated per permutation by `PBR_Renderer`, so a game cannot add fields
      to it today. Either a fixed number of custom slots is added, or the
      limitation is recorded with a trigger — silence is the one wrong answer
- [ ] The skinned path is guarded to whatever extent it can be before T0041 —
      stated plainly if that is "compiles, cannot render" (no skinned asset
      path exists yet)
- [ ] Both stages compile as **one slang module in one compile**, and the
      compile-count/time change is measured against 142.6's numbers

## Subtasks

- [ ] 146.1 Mirror the vertex main: `GLTF_TransformVertex` via their public
      header, the joint-blending block, the `VSOutput` population — then the
      byte-identical guard before anything else changes
- [ ] 146.2 The vertex method on the contract, with the default that is
      today's path; decide its space and document it (the Godot
      `world_vertex_coords` decision)
- [ ] 146.3 Worked example: wind sway in the sandbox, asserting the silhouette
      actually moves (a pixel far from the rest position)
- [ ] 146.4 The custom-interpolator decision, recorded either way
- [ ] 146.5 Single-module compile: move `kVertexShader` to the engine's
      `.slang`, measure the compile-request count and cold-compile delta
- [ ] 146.6 Name the motion-vector seam for T0111's remainder (a comment and a
      line here, not an implementation)
- [ ] 146.7 **Tessellation / displacement, or an explicit decision not to**
      (was T0141.9, inherited 2026-08-06 when T0141 closed — see "Inherited
      from T0141" below). **Deferred with a named trigger, not scheduled**:
      *when a silhouette must change at a density the mesh does not carry.*
      `PBR_Renderer` creates no hull or domain shaders at all, so this is new
      **pipeline** work rather than new shader code — which is why it lands
      here, where the pre-rasteriser stages become the engine's, and not on the
      ticket that happens to want it first.
      **T0156.6's silhouette-POM evaluation came back negative (2026-08-06)**,
      so this trigger is the *only* silhouette answer and the pressure behind
      it is real: silhouette POM does not compose with triplanar (no single
      height field to march), its `discard` degrades hierarchical-Z for
      everything drawn behind — terrain being the scene's dominant occluder —
      and nobody ships it for this problem (Unreal's answer is Nanite runtime
      tessellation; CryEngine gates its version to the very-high tier). The
      full evaluation with sources and this engine's march-cost anchors is on
      T0156's notes; do not reopen silhouette POM without new evidence

## Inherited from T0141, 2026-08-06 — tessellation, and a trigger that needed sharpening

**141.9 came here when T0141 closed**, and the choice of destination was
argued rather than defaulted, because there were two plausible ones.

**Why here and not T0155 (terrain).** Terrain is the obvious *consumer* —
displacement is what makes a heightmap read as landscape, and 155.2 is
literally the decision "clipmap, chunked quadtree, or theirs extended", where
GPU tessellation is a candidate. But what tessellation actually needs is
**hull and domain stages in the PSO**: `PBR_Renderer::CreatePSO` sets `pVS` and
`pPS` and nothing else, and D26 is what made adding stages possible at all.
That is this ticket's code — the one that stops the vertex shader being
Diligent's and moves `kVertexShader` into the engine's own module. Homing a
general pipeline capability on terrain would have shaped it like terrain, and
the second consumer (a hero asset, a wall that must have a real silhouette)
would have found it in the wrong place. **T0155 gets the cross-reference
instead**, on 155.2, which is where the want will first appear.

**The trigger needed sharpening, and that is the useful part of the move.**
T0141 deferred this *"when a silhouette must change"*. This ticket **delivers
silhouette change** — 146.3's worked example asserts a pixel moves far from its
rest position, and the Done-when says "silhouettes included, the thing 141.7's
parallax explicitly cannot do". So the trigger as written is discharged by the
ticket that inherited it, which would have read as "do it now" to anyone
arriving cold.

What tessellation adds over 146.1–146.6 is **new geometry**, not moved
geometry: vertex displacement is bounded by the mesh's existing density, so a
low-poly wall pushed along its normals still has a faceted outline. The trigger
is therefore restated: **when a silhouette must change at a density the mesh
does not carry.** Two things would pull it in — T0155's LOD scheme choosing
tessellation, and T0156.6's silhouette-POM evaluation coming back negative
(if pixel-shader silhouettes work, the pressure drops; if they do not, this is
the remaining answer). Both tickets say so on their own side.
**The second of those has now happened**: 156.6 evaluated silhouette POM on
2026-08-06 and recommended against it — see 146.7's subtask text for the
short form and T0156's notes for the full evaluation. This ticket's
tessellation is the remaining answer to silhouettes past mesh density.

**What T0141 established and this inherits unchanged:** tessellation is
reachable *only* because D26 chose C2 — we own PSO creation. C1 (patching
DiligentFX's shaders) could never have reached it, because the missing piece
was C++ PSO construction rather than shader text, and that was the finding that
settled C1-vs-C2 as a capability question rather than a maintenance one.

## Notes / findings

### What the probe showed (2026-08-06, pinned slangc 2026.14.1)

A file with `[shader("vertex")] vsMain` and `[shader("fragment")] psMain`
compiles in one invocation to one SPIR-V module containing two
`OpEntryPoint`s; Diligent accepts per-entry-point bytecode
(`ShaderCreateInfo::ByteCode` + `EntryPoint`). The reflection JSON from the
same compile lists both entry points — one compile can feed both stages *and*
the inspector.

### Godot reference points (4.7.1, surveyed 2026-08-06)

`vertex()` runs per vertex with `VERTEX`/`NORMAL`/`UV`/`COLOR` writable;
`world_vertex_coords` switches the space, `skip_vertex_transform` hands the
transform to the shader. **Custom interpolators: Godot has them** — a shader
author declares `varying` values passed vertex → fragment — and HollowPoint
does not, because `VSOutput` is generated by `PBR_Renderer` per permutation.
This is a row where Godot currently *exceeds* this engine, which is why 146.4
is a decision and not a footnote.
