# T0146 — The vertex stage: own the vertex main, and give games a vertex hook

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 456 |
| **Created** | 2026-08-06 |
| **Refs** | **D30**/**D32** ([../../documentation/02-decision-log.md](../../documentation/02-decision-log.md)); [../inprogress/0141-custom-shader-materials.md](../inprogress/0141-custom-shader-materials.md) — 141.7's vertex-displacement note names this exact change; [../inprogress/0142-slang-shader-language.md](../inprogress/0142-slang-shader-language.md) — one compiler for both stages is what makes this cheap; T0041 — skinning must be built against *our* vertex shader, see "Sequencing"; [0145-lighting-stage-own-the-light-loop.md](0145-lighting-stage-own-the-light-loop.md) — the same ladder, one stage earlier |

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
