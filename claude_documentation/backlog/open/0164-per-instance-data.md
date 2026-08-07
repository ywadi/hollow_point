# T0164 — Per-instance data: the last empty cell in the capability matrix

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 469 |
| **Created** | 2026-08-07 |
| **Blocked by** | nothing |
| **Refs** | [../../documentation/13-shader-capability-matrix.md](../../documentation/13-shader-capability-matrix.md) — **this closes its only `(unowned)` row**; [../completed/0146-vertex-stage-hook.md](../completed/0146-vertex-stage-hook.md) — the vertex family is five of six expressible and **this is the sixth**; [../completed/0160-material-declared-parameters.md](../completed/0160-material-declared-parameters.md) — recorded the trigger and put it explicitly out of scope; [../completed/0161-game-resource-model.md](../completed/0161-game-resource-model.md) — the declaration mechanism a per-instance value would be surfaced through; [0039-meshoptimizer-auto-lod.md](0039-meshoptimizer-auto-lod.md), [0040-runtime-lod-selection.md](0040-runtime-lod-selection.md) — a dithered LOD fade needs the same channel; [0045-culling-and-render-queues.md](0045-culling-and-render-queues.md) — batching and instancing are neighbours; **D23**, **D35**, **D36** |

## Why

**It is the only row in the capability matrix with a gap and nobody's name on it.**

Two techniques are blocked and both are ordinary: **per-instance offsets** — crowd variation, a thousand rocks that are not all identical — and **per-object fade**, which every dithered LOD transition needs. The vertex stage landed on T0146 and can express both; what it cannot get is a number that differs between two draws of the same mesh with the same material.

**And there is a latent defect underneath it.** `PBRPrimitiveAttribs` carries a `float4 CustomData` per draw. DiligentFX skips the write when the pointer is null (`GLTF_PBR_Renderer.cpp:916-921`), and the engine passes null (`SceneRenderer.cpp:944-945`) — so those **sixteen bytes are undefined memory** in every draw the engine submits. Nothing reads them today, which is why it is latent rather than active; it is the same class as the uninitialised `Camera` block T0159.6 fixed, and it should be closed whether or not the feature is wanted.

## The two levels, and why this ticket is the first one

**A per-draw channel is not GPU instancing.** They are often confused and the cheap one is worth having on its own:

| | What it is | Cost |
|---|---|---|
| **This ticket** — a per-draw value | One `float4` (or a small block) that differs between draws of the same mesh and material | The layout already exists; a `DrawItem` field and one write |
| **Real instancing** — not this ticket | One draw call, N copies, an instance buffer, and the culling and batching to decide N | T0045's neighbourhood, and a much larger design |

Godot's `instance uniform` is the precedent for the first: per-node values, capped at sixteen, no textures, no new material. It exists precisely because the expensive version is not always what you need.

## Done when

- [ ] A game's shader reads a **value that differs per draw** of the same mesh and material, in both the vertex and pixel stages
- [ ] The value is set from gameplay through the engine's own vocabulary — not by writing a Diligent struct
- [ ] **The undefined bytes are gone**, whether or not anything reads them
- [ ] A material that uses none of it is **byte-identical**, and the plain glTF path costs nothing new
- [ ] The capability matrix's last `(unowned)` row has an owner and a status

## Subtasks

- [ ] 164.1 **Close the latent defect first**: `CustomData` is written, defined, and zero when nothing sets it. This is worth doing even if the rest of the ticket is deferred
- [ ] 164.2 **Decide the shape, and record it.** One `float4` matching the existing layout, or a small block? Godot caps at sixteen values; DiligentFX gives four floats for free. **The trade is that a wider channel costs every draw** — measure before widening, the way T0161.1 measured before designing
- [ ] 164.3 **The gameplay-facing side**: a component or a `MeshRenderer` field, reflected and serialised like everything else (D23), so a scene file can author per-object variation and the inspector shows it
- [ ] 164.4 **The contract side**: `HpVertexInput` and `HpSurfaceInput` gain it, documented at the declaration so `docs/shaders/` picks it up. **Stage-agnostic per D35** — a value that reaches the vertex stage and not the pixel stage would be a gap of exactly the kind that decision exists to prevent
- [ ] 164.5 **Prove it with two draws of one mesh** differing only by the value — the rock cube sample already draws two materials on one mesh, so a second cube at a different phase is a small step
- [ ] 164.6 **Update the capability matrix** — per-instance data, per-instance offsets and per-object fade
- [ ] 164.7 **Record what this is not.** A note on T0045 that real instancing is still open, with the trigger that would make it worth doing

## Not in scope

- **GPU instancing.** One draw, N copies, an instance buffer and the batching to fill it. Neighbouring, larger, and T0045's territory.
- **A LOD system.** T0039/T0040 own that; this ticket supplies the fade factor's *channel*, not the factor.

## Notes / findings

### Measured 2026-08-06, during the capability audit

- `PBRPrimitiveAttribs::CustomData` is `float4`, in the cbuffer layout, written only when a non-null pointer is supplied; the engine supplies null. **Verified by reading both call sites**, not inferred.
- Whether those bytes read as zero or as noise on real hardware was **not measured** — undefined per the API is enough to fix it, but if anyone wants the number it is one debug view away.
- No `DrawItem` field feeds it, so there is nothing to plumb *from* yet — 164.3 is the missing half.
