# T0051 — Physics engine integration (Jolt)

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Very Complex |
| **Phase** | 9 — Physics |
| **Order** | 800 |
| **Created** | 2026-08-03 |
| **Refs** | T0100, [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md), T0116 |

> **Placeholder epic.** Recorded now so the architecture accounts for it, not
> because it is ready to start. Break into real tickets when Phase 9 is reached —
> the subtasks below are scope markers, not a plan.

## Why

A character-driven game with skeletal animation at its core needs collision,
character movement and rigid body simulation. Diligent provides none of this — it
is a rendering abstraction only.

**Jolt Physics** is the intended choice: C++17, permissive licence, CMake,
deterministic, multi-threaded, and proven at scale (Horizon Forbidden West). It
should cross-compile to both targets without drama.

Capturing it now matters because two earlier decisions have to accommodate it —
the fixed-timestep loop and the job system — and both are cheaper to get right
than to retrofit.

## Rough scope

- [ ] Vendor Jolt; confirm it cross-compiles to `x86_64-windows-gnu`
- [ ] **Fixed-timestep physics with render interpolation** — see notes
- [ ] Rigid body and collision shape components, referencing assets by GUID
- [ ] Collision filtering via the shared layer definitions (T0085), not a
      separate physics-only layer concept
- [ ] Collision shape generation at import (convex hulls, meshes)
- [ ] **Character controller** — the piece that matters most here
- [ ] Back Jolt's job system with enkiTS rather than a second thread pool
- [ ] Debug draw of collision shapes through the render stack
- [ ] Physics/animation interaction: root motion vs simulated movement
- [ ] **Ragdoll: decide yes/no, and powered or pure** (see the 2026-08-03 note)

## Notes / findings


### Frame anatomy — phases 3c and 3d — inside the fixed-step loop (T0100, D17)

The physics step and post-physics resolution are **phases 3c and 3d**, *inside*
the fixed-step loop. Once per fixed step, not once per frame — the loop may run
zero times or several in a single frame.

The full order is in [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md); the decision and what it rejected is **D17** in the
decision log. If this ticket needs a phase that does not exist, that is a change
to T0100's document and to D17 — not a new call bolted into `Application::run`.

**Jolt has its own `JobSystem` interface, and it is an interface for exactly this
reason** — it can be backed by an existing scheduler. Backing it with enkiTS
(T0026) avoids two thread pools fighting over cores, which is a real and common
performance problem. Worth doing from the first integration, not later.

**Fixed timestep is non-negotiable and affects the main loop** (T0014). Physics
must step at a fixed rate for stability and determinism while rendering runs at
whatever rate it can, which means accumulating time and interpolating transforms
between the last two physics states for rendering. Retrofitting this into a
variable-timestep loop touches everything that reads a transform, so T0014 should
at least not preclude it.

**Root motion versus physics is the hard part for animated characters** (T0049).
Animation wants to drive movement from the clip; physics wants to drive it from
forces and collisions. Reconciling them — usually the character controller
consuming root motion as a desired velocity — is where character movement
actually gets difficult. Expect this to be the bulk of the work.

Determinism is worth deciding on early if networking is ever a possibility, since
it constrains float usage and threading.

### Note (2026-08-03) -- ragdoll was the unnamed half of the seam

The design-gap survey (`documentation/07-design-gaps.md`, item 14) noticed
that root motion versus simulation is called out in T0049, in this ticket
*and* in the README's Very Complex list -- yet ragdoll, the other big feature
on exactly that animation/physics seam, was never named anywhere. The scope
bullet above makes the absence a decision to take when this epic breaks into
tickets: **no ragdoll** (death animations only -- legitimate and much cheaper),
**pure ragdoll** on death (Jolt's ragdoll support driving the ozz skeleton's
joints), or **powered/blended** (animation-driven targets with physics take-
over, the expensive one). The game decision is T0044's; the mechanism, if
wanted, lands here.

### Cross-ticket obligations (2026-08-04, T0124 backfill)

- **T0116.6** authors collision shapes in the editor — a resizable primitive
  with no render mesh. This epic's collision components must be able to
  reference an *authored* shape, not only ones generated at import; the scope
  bullet "collision shape generation at import" is half the story, and T0116
  was filed because the authoring half had no owner.
