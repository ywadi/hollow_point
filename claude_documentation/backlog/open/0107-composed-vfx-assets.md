# T0107 — Composed VFX assets: an effect is more than an emitter

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 555 |
| **Created** | 2026-08-03 |
| **Refs** | T0080, T0106, T0108, T0059, T0079, T0052 |

## Why

Ask what an explosion is and the answer is not "an emitter". It is:

- a fire flipbook, additive, over ~0.4s
- a smoke plume, alpha and soft, lasting several seconds and drifting
- embers with gravity, sparse and long-lived
- a **light** that flashes bright and decays within ~0.2s
- a scorch **decal** on the ground that persists (T0108)
- a **sound** (T0052)
- optionally screen distortion for the shockwave

Seven components, three different subsystems, one authored thing. T0080 gives
one emitter with one set of curves. Nothing composes them, and nothing owns the
question of what happens when the entity that spawned the effect is destroyed
half a second in — which is *always*, because the thing that exploded stops
existing.

Without this, every effect in the game is assembled by hand in gameplay code,
which is the "each effect becomes bespoke rendering code" outcome T0080 exists
to prevent — just moved up a level.

## Done when

- [ ] A VFX asset bundles multiple emitters, each with its own timing offset
- [ ] It can also carry a light, a decal and a sound, with their own lifetimes
- [ ] Spawning is one call: play this effect at this transform
- [ ] **The effect outlives its spawner.** Destroying the emitting entity does
      not cut the effect off mid-play
- [ ] It cleans itself up when the longest-lived component finishes; nothing
      leaks if it is spawned ten thousand times
- [ ] Authored and previewed in the editor without entering play mode
- [ ] One-shot and looping effects both work, and looping ones can be stopped

## Subtasks

- [ ] 107.1 The asset shape. **Consider whether this is a prefab (T0059) with
      VFX components rather than a new asset type** — see notes; this is the
      main design decision
- [ ] 107.2 Per-component delay and duration relative to effect start
- [ ] 107.3 Detached lifetime: the effect is parented to a transform at spawn
      but does not die with the spawner
- [ ] 107.4 Automatic retirement when every component has finished
- [ ] 107.5 A budget on concurrent effects, and a policy for what happens at the
      cap (oldest retired, or newest refused — they look different in play)
- [ ] 107.6 Light component integration (T0079), including whether effect lights
      are real shadow-casting lights or cheap unshadowed ones
- [ ] 107.7 Editor preview with a timeline scrub, insofar as GPU simulation
      allows it (see T0080's note — backwards scrubbing means re-simulating)
- [ ] 107.8 A stop/fade-out path for looping effects, distinct from destroy —
      cutting a looping smoke plume dead looks wrong

## Notes / findings

**107.1 is the decision that shapes the rest.** A VFX asset looks a lot like a
prefab: a small tree of entities with components and a transform. If prefabs
(T0059) can already express "several entities, one root, spawn as a unit", then
a VFX asset may be a prefab with a Lifetime component and nothing more — which
is far less machinery and reuses the editor's existing authoring. The risk is
that effects want things prefabs do not have, notably per-component start
offsets and self-retirement. **Answer this before building a parallel asset
type**, because two spawnable-tree concepts in one engine is a lasting tax.

**Detached lifetime (107.3) is the requirement everyone forgets.** The unit that
exploded is destroyed on the frame the explosion starts. If the effect is
parented to it in the usual way, it vanishes instantly and the bug is reported
as "explosions don't work sometimes". Effects must copy the spawn transform
rather than reference the spawner, unless deliberately attached (a torch flame
follows its torch, and *should* die with it) — so both modes are needed and the
default matters.

**The budget interacts with D15's fixed particle buffer.** T0080's GPU budget is
a fixed-size allocation; this ticket's budget is on *effects*. Under fire, both
saturate at once, and if effects keep spawning while particles are exhausted the
result is silent, invisible explosions — worse than a visible degradation. The
two caps should be aware of each other.

**Sound is listed but Phase 10 is a placeholder epic** (T0052, library
undecided). Design the asset so a sound reference *can* exist without blocking
on the audio engine; do not wait for it.

### 2026-08-08 — considered for merge into T0080, and **declined** (T0106 was merged; this was not)

[T0106](../completed/0106-vfx-sprites-and-flipbooks.md) **was** merged into
[T0080](0080-particles.md) the same day, because it existed only to supply a half
that T0080.4 had under-specified — and it carried `Blocks: T0080`, a ticket
blocking the ticket it was half of. **This ticket is not that shape.**

**It spans four subsystems T0080 does not touch**: lights (T0079), decals
(T0108), audio (T0052) and prefabs (T0059). Merging it would make the particle
system blocked on the audio ticket, which is the same failure the merge pass was
run to remove.

**And its central question is not a particle question.** 107.1 asks whether a
composed effect is *a prefab (T0059) with VFX components* rather than a new asset
type — and if the answer is yes, most of this ticket dissolves into T0059, not
into T0080.

**What the review did fix — a real overlap, now assigned rather than duplicated:**

- **T0080 owns the *particle* budget and self-retirement** (80.6, 80.8) — a
  fixed-size GPU buffer and one-shot emitters cleaning themselves up.
- **This ticket owns the *effect* budget and lifetime** (107.4, 107.5) — how many
  composed effects may be live at once, what happens at the cap, and the
  detached lifetime that lets an explosion outlive the entity that exploded,
  which is *always*, because the thing that exploded stops existing.

Those are two different budgets over two different units, and the tickets now say
so.
