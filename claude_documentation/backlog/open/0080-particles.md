# T0080 — Particle and VFX system

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 550 |
| **Blocked by** | [0150-compute-pipelines.md](0150-compute-pipelines.md) — 80.2 is a compute dispatch and no compute pipeline exists; T0150 builds the stage and checks its shape against 80.2's needs (150.7) before closing |
| **Merged** | 2026-08-08 — **absorbed [T0106](../completed/../completed/0106-vfx-sprites-and-flipbooks.md) (VFX sprites, flipbooks and blend modes)**, ❌ SUPERSEDED. T0106 existed because **80.4 was under-specified** and said so in its own text; it also *blocked the ticket it was half of*. See *Why this was two tickets* |
| **Created** | 2026-08-03 |
| **Refs** | [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D15, [../completed/0106-vfx-sprites-and-flipbooks.md](../completed/0106-vfx-sprites-and-flipbooks.md) — **absorbed here 2026-08-08**; [0107-composed-vfx-assets.md](0107-composed-vfx-assets.md) — **considered for merge and kept separate**: it spans lights, decals, audio and prefabs, none of which this ticket touches. This ticket owns the *particle* budget and self-retirement (80.6, 80.8); T0107 owns the *effect* budget and the detached lifetime that lets an explosion outlive the entity that exploded |

## Why

Diligent provides **nothing** for particles — confirmed, there is no particle
module in DiligentFX. Every game needs them: impacts, muzzle flashes, dust, fire,
magic. Without a system, each effect becomes bespoke rendering code.

**And a particle system is simulation *and* texturing, which is one system.**
Emitters, shapes, curves and budgets decide *where the quads are*; sprites,
flipbooks, blend modes and soft edges decide *what is on them*. A muzzle flash
**is** a sprite; an explosion is normally a **flipbook** — a grid of frames in
one texture, advanced over the particle's normalised age, so a single quad shows
fire igniting, expanding and dissipating; smoke is a soft-edged sprite that must
fade where it meets geometry. **Without the second half, this system emits ten
thousand correctly simulated white squares.**

### Why this was two tickets

T0106 was split out because **80.4 was under-specified**, and both tickets said
so in as many words:

- **80.4** read *"Batched rendering as camera-facing quads… Sprite/flipbook
  texturing and blend modes are **T0106**, which this subtask covers only the
  geometry half of."*
- **T0106's own `## Why`** read *"T0080 designs emitters in detail… **It never
  says what is on the quads.** That is the entire visual half of a VFX system and
  it is currently undefined."*

And the dependency ran the wrong way round: **T0106 was marked `Blocks: T0080`**
— a ticket blocking the ticket it is one half of. That is not a dependency, it is
a seam through the middle of one job.

**D15 is why there can only be one implementation.** Particles are GPU-only and
purely cosmetic, with no CPU fallback, so simulation and texturing share the same
compute dispatch, the same buffer and the same draw. Two tickets could only
produce two partial designs of it — and the flipbook UV, which 106.2 places *"in
the vertex or compute stage from normalised particle age"*, is literally a line
inside 80.2's dispatch.

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

### Texturing — absorbed from T0106
- [ ] A particle's material references a texture, and the quad samples it
- [ ] **Flipbook animation**: a sprite sheet with rows and columns, the frame
      advanced by the particle's normalised age, so one emitter plays an
      explosion sequence
- [ ] **Sub-frame blending** between flipbook frames, so slow effects do not
      visibly step
- [ ] **Blend modes selectable per effect**: at minimum additive and alpha
- [ ] **Soft particles** — fade where the quad intersects scene depth, so smoke
      does not slice visibly through the floor
- [ ] Per-particle colour and opacity from the lifetime curves **modulate the
      sampled texture**
- [ ] Works on both targets

## Subtasks

- [ ] 80.1 Emitter component and its reflected properties
- [ ] 80.2 Simulation: spawn, update, retire — **GPU compute, per D15**. No CPU
      path exists to fall back to, so this subtask is the system rather than a
      choice within it
- [ ] 80.3 Curve/gradient types over particle lifetime, editable in the inspector
- [ ] 80.4 Batched rendering as camera-facing quads, in the transparent queue
      (T0045) — **the geometry half; 80.10–80.16 are the texturing half of the
      same draw**
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

**Texturing — absorbed from T0106. Same dispatch, same draw, same ticket.**

- [ ] 80.10 **Sprite-sheet asset shape**: the texture plus rows, columns and
      frame count. Decide whether it is a distinct asset type or metadata on a
      texture import (T0097)
- [ ] 80.11 **Flipbook UV** computed in the vertex or compute stage from
      normalised particle age — a line inside 80.2's dispatch, not a separate
      mechanism
- [ ] 80.12 **Frame-to-frame blending** (sample two frames, lerp) behind a
      toggle — it costs a second sample and is not always wanted
- [ ] 80.13 **Blend mode as material state**: additive, alpha, and consider
      **premultiplied** alpha, which avoids the halo artefacts the other two
      produce at sprite edges
- [ ] 80.14 **Soft particles** — sample scene depth, fade over a configurable
      range. **The depth read is built and proved** (T0147/D37):
      `HpSceneViewDepth(In.ScreenUV)` against the fragment's own
      `HpViewDepth(In.ScreenPos.z)`, worked example in
      `tests/gpu/screen_inputs_test.cpp`. **Two obligations come with it**: the
      particle material must be `alphaMode: Blend` (any other alpha mode is
      refused by name at pipeline build), and the depth it reads is the
      **opaque** depth, so particles do not fade against each other
- [ ] 80.15 **Texture atlas support**, so many small effects share one texture
      and one draw
- [ ] 80.16 **Decide whether particle textures participate in lighting at all**,
      or are always unlit/emissive

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

---

## Absorbed from T0106 — its findings, kept verbatim

*The `106.x` numbers refer to its old subtask list; the mapping is in its
`## Descoped` table.*

### Inherited from T0111 / D25 (2026-08-05) — GPU particles will smear under TAA

**A real cost against D15, recorded before it is discovered visually.**

D25 makes TAA the engine's antialiasing. TAA reprojects the previous frame using
**motion vectors**, and anything without correct ones ghosts. **D15 makes
particles GPU-driven and cosmetic**, so they have no CPU-side previous position —
which means that unless the particle simulation writes motion vectors itself, every
particle smears behind its own motion.

Three options, all this ticket's to weigh:

1. **Write motion vectors from the simulation** — the compute pass knows last
   frame's position, so this is cheap if designed in and awkward if retrofitted.
2. **Use the reactive mask.** Diligent's temporal upscalers accept a per-pixel
   reactive mask *"useful for alpha-blended objects, particles, or areas with
   inaccurate motion vectors"*, recommended clamped to about 0.9. TAA proper has
   no equivalent input.
3. **Accept the smearing** on cosmetic particles, which D15's framing might allow.

**Also relevant: MSAA is out** (D25), which removes a cost this ticket was carrying.
106.5's soft particles need scene depth readable while drawing transparents, and
under MSAA that would have become a per-sample read. It stays a plain single-sample
depth read.

See [../completed/0111-anti-aliasing-and-render-scale.md](../completed/0111-anti-aliasing-and-render-scale.md).

**This reverses a decision in T0097.** That ticket says "texture *arrays* and
atlases are out of scope; Diligent has `DynamicTextureAtlas` if they are ever
wanted." It was written before VFX were considered, and flipbooks are precisely
the case that wants them: a sprite sheet *is* an atlas with regular spacing.
T0097 has been amended to point here rather than silently contradicting itself.

**Soft particles are the difference between "a game" and "broken".** Without a
depth fade, every smoke plume shows a hard straight line where it intersects the
ground. It is a small shader change with an awkward dependency: the transparent
pass must be able to *read* the depth buffer it is testing against, which is a
constraint on T0046's render-target management and is worth raising there before
the frame graph solidifies.

**Premultiplied alpha is worth the argument.** Standard alpha blending produces
dark halos around sprite edges and cannot mix additive and alpha content in one
texture; premultiplied handles both and lets a single effect have glowing cores
with soft edges. It costs a convention in the texture pipeline (T0097) and is
much cheaper to adopt before there is art than after.

**Lighting particles is a real fork (106.7).** Unlit/emissive is cheap, correct
for fire and magic, and wrong for smoke, which should darken in shadow. Lit
particles need normals per-particle and a shading path in the transparent queue.
Unlit-only is a defensible first answer; write down which one so smoke does not
get authored against an assumption that later changes.

**Do not build a general 2D sprite renderer.** The engine is 3D (D15).
Everything here is a camera-facing quad in a 3D scene. If HUD sprites are ever wanted, that
is T0069's problem and a different renderer.
