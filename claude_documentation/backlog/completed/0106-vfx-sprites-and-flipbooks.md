# T0106 — VFX sprites, flipbooks and blend modes

| | |
|---|---|
| **Status** | ❌ SUPERSEDED |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 545 |
| **Created** | 2026-08-03 |
| **Superseded** | 2026-08-08 by **[T0080](../open/0080-particles.md)** — the particle and VFX system it was always one half of. **Nothing was dropped**: the mapping is below and this ticket's `## Notes / findings` is preserved **verbatim** in T0080 under *Absorbed from T0106* |

## Why this was absorbed rather than left open

**This ticket existed because T0080.4 was under-specified, and both tickets said
so.**

- **T0080.4** read: *"Batched rendering as camera-facing quads… Sprite/flipbook
  texturing and blend modes are **T0106**, which this subtask covers only the
  geometry half of."*
- **This ticket's `## Why`** read: *"T0080 designs emitters in detail… **It never
  says what is on the quads.** That is the entire visual half of a VFX system and
  it is currently undefined."*

Two tickets, each describing the other as its missing half, and a subtask split
down the middle between them.

**And the dependency pointed the wrong way.** This ticket carried
`Blocks: T0080` — a ticket blocking the ticket it is one half of. A dependency
edge between two halves of one job is a seam, not a sequence.

**D15 is why there can only be one implementation.** Particles are GPU-only and
purely cosmetic with no CPU fallback, so simulation and texturing share the same
compute dispatch, the same particle buffer and the same draw call. **106.2's
flipbook UV is explicitly *"in the vertex or compute stage from normalised
particle age"* — a line inside 80.2's dispatch.** Kept apart, the two tickets
could only produce two partial designs of one shader.

## Where everything went

| From here | Now |
|---|---|
| Sprite-sheet asset shape — texture, rows, columns, frame count; distinct asset type or texture-import metadata (106.1) | **T0080.10** |
| Flipbook UV from normalised particle age (106.2) | **T0080.11** |
| Frame-to-frame blending behind a toggle (106.3) | **T0080.12** |
| Blend mode as material state — additive, alpha, premultiplied (106.4) | **T0080.13** |
| Soft particles (106.5) | **T0080.14**, with T0147/D37's built depth read and **both** of its obligations carried: `alphaMode: Blend` is required and refused by name otherwise, and the depth read is the **opaque** depth so particles do not fade against each other |
| Texture atlas support (106.6) | **T0080.15** |
| Whether particle textures are lit at all (106.7) | **T0080.16** |
| Every Done-when | **T0080's `### Texturing — absorbed from T0106`** section, unchanged |
| The T0111/D25 finding that **GPU particles will smear under TAA** for want of motion vectors | preserved verbatim in T0080 — a real cost against D15, recorded before it is discovered visually |

## The sentence worth keeping

*"Without this ticket the particle system can emit ten thousand correctly
simulated white squares."* That is why the texturing half is not a follow-up:
it is the half a player sees.
