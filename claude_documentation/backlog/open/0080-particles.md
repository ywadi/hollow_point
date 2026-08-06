# T0080 — Particle and VFX system

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 550 |
| **Created** | 2026-08-03 |
| **Refs** | [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D15, T0106 (Blocks this), T0107 |

## Why

Diligent provides **nothing** for particles — confirmed, there is no particle
module in DiligentFX. Every game needs them: impacts, muzzle flashes, dust, fire,
magic. Without a system, each effect becomes bespoke rendering code.

## Done when

- [ ] A particle emitter component, authored in the editor
- [ ] Emission shapes, rates, bursts, and lifetime
- [ ] Per-particle velocity, size, colour and rotation, driven by curves over life
- [ ] Sorted correctly against transparency and against other particles
- [ ] Simulation cost is bounded — a budget, not unlimited particles. With GPU
      simulation the budget is a fixed-size buffer allocated up front rather
      than a soft cap, which makes it easier to enforce and harder to change at
      runtime
- [ ] Emitters are culled when off-screen (T0045)
- [ ] Effects are reusable as prefabs (T0059)
- [ ] Triggerable from animation events (T0049) and from gameplay

## Subtasks

- [ ] 80.1 Emitter component and its reflected properties
- [ ] 80.2 Simulation: spawn, update, retire — **GPU compute, per D15**. No CPU
      path exists to fall back to, so this subtask is the system rather than a
      choice within it
- [ ] 80.3 Curve/gradient types over particle lifetime, editable in the inspector
- [ ] 80.4 Batched rendering as camera-facing quads, in the transparent queue
      (T0045). Sprite/flipbook texturing and blend modes are **T0106**, which
      this subtask covers only the geometry half of
- [ ] 80.5 Emission shapes: point, sphere, cone, mesh surface — all evaluated
      in the compute shader, so mesh-surface emission needs the mesh readable
      from the GPU side
- [ ] 80.6 Bursts and one-shot effects that clean themselves up
- [ ] 80.7 ~~Parallel simulation via the job system (T0026)~~ — **dropped by
      D15.** The job system is not involved in simulation. What the CPU still
      owns is emitter *bookkeeping*: which emitters exist, their parameters, and
      writing spawn requests into a GPU-visible buffer. That is a small,
      per-emitter cost, not per-particle work
- [ ] 80.8 A global particle budget with graceful degradation
- [ ] 80.9 Editor preview that plays effects without entering play mode

## Notes / findings

**~~CPU versus GPU simulation is the fork.~~ Decided: GPU only (D15).** The
original text recommended starting on the CPU with the job system, on the
grounds that it is simpler, debuggable, lets gameplay read particle state, and
is "fine for thousands". That recommendation is withdrawn. "Fine for thousands"
is the problem rather than the reassurance — one explosion with smoke, embers
and distortion is tens of thousands on its own — and building CPU-first lets the
render path acquire CPU-only assumptions (per-particle writes into a mapped
vertex buffer, CPU sorting) that each become a rewrite later. There is no CPU
simulation path, not even as a fallback. See D15 for the rejected alternatives,
including the CPU/GPU hybrid.

**Sorting is where particles look wrong.** Additive blending is order-independent
and forgiving; alpha blending is not, and unsorted alpha particles produce visible
artefacts as the camera moves. Sort within an emitter by depth, and place emitters
correctly in the transparent queue.

**Budget from the start.** An uncapped particle system is one bad emitter away
from tanking the frame rate, usually during a demo. A global cap with oldest-first
retirement is unglamorous and saves real pain.

Curves over lifetime are the bulk of the authoring value, and they need a curve
editor widget in the inspector — worth noting as it is more UI work than the rest
of the ticket.


### Architecture amendment (2026-08-03) — GPU-only, and what that pulls in (D15)

**The enabling constraint, which is not a caveat but the reason this works:**
particles are cosmetic. **Gameplay never reads particle state.** An explosion
that damages is gameplay computing damage from the *event*; the particles merely
depict it. The two never share state. Readback is where GPU simulation gets
genuinely painful, and this rule removes the need for it entirely.

Consequences this ticket was not written for:

- **Collision is depth-buffer based**, so it is screen-space and approximate:
  particles collide with what the camera can see and pass through everything
  else. Exact scene collision needs an SDF or voxel representation and is its
  own project. Acceptable for smoke and debris; do not promise more.
- **Sorting moves to the GPU.** 80.4's "sorted correctly against transparency"
  needs a GPU sort (bitonic or radix) for alpha-blended particles. Additive
  blending is order-independent and needs none, which is a reason to make
  additive the default for most effects and alpha the considered exception.
- **Non-deterministic**, so particles can never participate in networking
  (T0070) or replays. Follows from being cosmetic.
- **A debug readback path is a tool, not a fallback.** Particle state lives in
  GPU buffers and will need inspecting. Build it early, and never let it become
  a simulation path.
- **One dispatch, not one per emitter.** A muzzle flash is a handful of
  particles, and a compute dispatch each would be dominated by overhead. All
  emitters share buffers and are simulated together; this is what makes the
  small-emitter case viable without a second CPU path.
- **~~OpenGL 4.3 floor~~ moot since D29/T0144 (2026-08-06):** the GL
  backend is removed, and D15's compute-shader requirement is a given on
  Vulkan.

**Editor preview (80.9) is harder than it reads** now that simulation is on the
GPU: previewing without entering play mode means running compute outside the
normal frame loop, and scrubbing an effect backwards is not possible without
re-simulating from the start.

### Amendment (2026-08-03) -- do not let 80.2's buffer layout preclude ribbons

From the design-gap survey (`documentation/07-design-gaps.md`, item 8):
trails, ribbons and beams have zero hits anywhere, and this ticket's rendering
is exclusively "camera-facing quads" (80.4). For a shooter-shaped game (T0093's
vision cones, T0098's agents) tracers, projectile trails and beam weapons are
near-certain requests.

A ribbon is **not an emitter-parameter tweak** -- it is a different topology, a
strip built through a particle's history -- and it touches D15's fixed GPU
buffer layout. It does not need building now. What is needed now is one
constraint on 80.2, before the buffer layout freezes: the particle buffer and
dispatch structure should not *preclude* strip-topology emitters -- concretely,
do not design away the possibility of addressing a particle's recent positions
(a history window or a stable ordering within an emitter's allocation). If
honouring that turns out to have a real cost, escalate the trade-off instead of
silently dropping it; the survey's claim is that keeping the door open is
cheap, and that claim should be tested against the actual layout, not assumed.

Screen distortion -- the other VFX shape the survey flagged -- is a frame-layout
constraint, not a particle-buffer one; it is recorded on T0046.

### Cross-ticket obligations (2026-08-04, T0124 backfill)

- **T0107.5**: the concurrent-*effect* cap and 80.8's particle budget saturate
  together under fire, and T0107 warns that effects spawning against an
  exhausted particle buffer produce silent, invisible explosions. Design
  80.8's degradation with the effect-level cap in view — the two policies must
  know each other.
