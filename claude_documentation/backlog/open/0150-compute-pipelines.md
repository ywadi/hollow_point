# T0150 — Compute pipelines: the stage the engine promised and never built

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 548 |
| **Created** | 2026-08-06 |
| **Blocks** | [0080-particles.md](0080-particles.md) — 80.2 *is* a compute dispatch (D15: "GPU compute... this subtask is the system"), and no compute pipeline exists to write it against |
| **Refs** | **T0161** ([../completed/0161-game-resource-model.md](../completed/0161-game-resource-model.md)) / **D35** — a compute module's buffers and textures come from `buildModuleSignatureDesc` (`engine/src/ModuleResourceSignature.hpp`) with `ModuleSignaturePolicy::allowBuffers = true`; the helper already carries BUFFER_SRV/UAV and TEXTURE_UAV, so this ticket supplies the compute base signature's names and the data paths that feed what a module declares — never a parallel reflection walk; **D15** (the commitment), **D32** ([../../documentation/02-decision-log.md](../../documentation/02-decision-log.md)); [../completed/0142-slang-shader-language.md](../completed/0142-slang-shader-language.md) — `SlangCompiler` grows a stage; D22 — the shape any gameplay exposure must take; T0050 — transitions are not thread-safe, and dispatch placement must respect it; T0030 — dispatches need GPU zones |

## Why

**D15 committed the entire particle system to GPU compute, and nothing owns
compute.** Measured 2026-08-06: `ComputePipeline`, `DispatchCompute` and
`SHADER_TYPE_COMPUTE` have **zero hits** across `engine/`, `apps/` and
`tests/`. `hp::ShaderStage` has exactly two values (`Vertex`, `Pixel`), and
`SlangCompiler.cpp`'s stage mapping is a ternary that sends
anything-not-Vertex to `SLANG_STAGE_FRAGMENT` — compute cannot even be
*requested* through the engine's shader path today. T0080.2 says "GPU compute,
per D15... this subtask is the system rather than a choice within it", and
would currently have to invent pipeline plumbing inside a particle ticket —
which is how machinery gets built twice (T0093's rule: gaps go to the owning
ticket).

**The Slang half is already proven trivial** (probe on the pinned 2026.14.1,
2026-08-06): `[shader("compute")] [numthreads(64,1,1)]` compiles to SPIR-V
with `OpEntryPoint GLCompute` / `LocalSize 64 1 1` through the same compiler
the material path uses. The Diligent half is standard API
(`CreateComputePipelineState`, `DispatchCompute`). What needs *designing* is
the engine's ownership: where dispatches sit in the frame, how buffers
transition, and what — if anything — gameplay is handed.

**The Godot comparison, and where this exceeds it** (4.7.1, surveyed
2026-08-06): Godot's compute is raw GLSL 450 through `RenderingDevice` — a
different language from its materials, hand-rolled binding code, no shader
hints, unavailable on its Compatibility renderer. HollowPoint's compute
arrives in **the same Slang** as every material: same modules, same
`import`s, same reflection (a dispatch's parameter block is inspectable the
same way a material's is), one backend (D29) so no availability matrix. That
is a concrete "even more" — if and only if the gameplay exposure ships; an
engine-internal-only compute path matches Godot for engine features and
exceeds nothing.

## Done when

- [ ] `ShaderStage::Compute` exists, and the slang stage mapping is an
      explicit switch — an unknown stage **fails loudly** instead of
      compiling as a fragment shader (the current ternary's failure mode)
- [ ] An engine compute pipeline compiles from a `.slang` file, dispatches,
      and its output is consumed by a draw **in the same frame** — proven by
      a gpu test with a pixel assertion
- [ ] Dispatch placement in the frame is decided and named in
      `08-frame-anatomy.md` — a phase with a profiler zone, per D17's rule
      that unbuilt phases are named rather than bolted in later
- [ ] Resource transitions around dispatch are stated (T0050's warning:
      transitions are not thread-safe) and validated in debug
- [ ] **Gameplay exposure is decided deliberately** — D22's shape (drive
      through handed-in pointers, never create) makes it *possible*; whether
      it ships now or waits for T0094's layer experience is the decision, and
      "engine-internal only, revisit with T0080's findings" is an acceptable
      answer written down
- [ ] Compute pipelines participate in whatever caching T0141.3 lands —
      stated, so the cache is not graphics-only by accident

## Subtasks

- [ ] 150.1 `ShaderStage::Compute`, the explicit stage switch in
      `SlangCompiler`, and the loud-failure default
- [ ] 150.2 An engine `ComputePipeline` alongside `SurfacePipeline` — creation,
      cache keyed like its sibling, `DispatchCompute` wrapper
- [ ] 150.3 The frame-anatomy placement and its Tracy zone
- [ ] 150.4 Transition/barrier rules, debug-validated
- [ ] 150.5 The proving gpu test: fill a buffer in compute, draw from it,
      assert pixels
- [ ] 150.6 The gameplay-exposure decision, recorded either way
- [ ] 150.7 Check the shape against T0080.2's needs before closing — the
      first consumer's requirements (shared buffers, one dispatch over all
      emitters) are the acceptance criteria that matter

## Notes / findings

### Owner decision 2026-08-06 — build it now, ahead of T0080

Asked whether compute should land now or after T0080's experience, the owner
chose now: *"Probably now, because this can be built on for 0080."* That matches
what this ticket already records — T0080's GPU particles need a compute stage
and cannot invent one.

**One caveat, and it is an engineering one rather than a disagreement.**
Building a subsystem with no consumer risks designing it against imagined
requirements, and a compute API shaped by guesses is the kind of wrong
abstraction that is expensive to unpick later. **Sequence T0080 immediately
behind this**, or make the first compute pass something concrete, so the shape
is decided by a real user rather than by anticipation. "It compiles and
dispatches" is not evidence the API is right.

### Sequencing

Sits directly before T0080 in the order because that is its first consumer;
nothing else blocks on it. If particles are pulled earlier, this moves with
them — the pairing is the constraint, not the number.

### The probe (2026-08-06, pinned slangc 2026.14.1)

```
[shader("compute")] [numthreads(64,1,1)]
void main(uint3 tid : SV_DispatchThreadID) { gParticles[tid.x].xyz += ...; }
```

compiles to SPIR-V: `OpEntryPoint GLCompute`, `OpExecutionMode LocalSize
64 1 1`. Nothing about the language or the pinned version blocks this ticket;
the work is entirely engine-side ownership.
