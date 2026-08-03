# T0080 — Particle and VFX system

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 550 |
| **Created** | 2026-08-03 |

## Why

Diligent provides **nothing** for particles — confirmed, there is no particle
module in DiligentFX. Every game needs them: impacts, muzzle flashes, dust, fire,
magic. Without a system, each effect becomes bespoke rendering code.

## Done when

- [ ] A particle emitter component, authored in the editor
- [ ] Emission shapes, rates, bursts, and lifetime
- [ ] Per-particle velocity, size, colour and rotation, driven by curves over life
- [ ] Sorted correctly against transparency and against other particles
- [ ] Simulation cost is bounded — a budget, not unlimited particles
- [ ] Emitters are culled when off-screen (T0045)
- [ ] Effects are reusable as prefabs (T0059)
- [ ] Triggerable from animation events (T0049) and from gameplay

## Subtasks

- [ ] 80.1 Emitter component and its reflected properties
- [ ] 80.2 Simulation: spawn, update, retire — decide CPU or GPU (see notes)
- [ ] 80.3 Curve/gradient types over particle lifetime, editable in the inspector
- [ ] 80.4 Batched rendering as camera-facing quads, in the transparent queue (T0045)
- [ ] 80.5 Emission shapes: point, sphere, cone, mesh surface
- [ ] 80.6 Bursts and one-shot effects that clean themselves up
- [ ] 80.7 Parallel simulation via the job system (T0026)
- [ ] 80.8 A global particle budget with graceful degradation
- [ ] 80.9 Editor preview that plays effects without entering play mode

## Notes / findings

**CPU versus GPU simulation is the fork.** CPU is far simpler, debuggable, and
lets gameplay read particle state — fine for thousands. GPU compute scales to
hundreds of thousands but makes collision, gameplay interaction and readback hard.
**Start CPU with the job system**; Diligent supports compute shaders on both
backends, so GPU remains available if the numbers demand it.

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
