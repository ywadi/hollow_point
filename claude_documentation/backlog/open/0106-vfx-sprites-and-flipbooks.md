# T0106 — VFX sprites, flipbooks and blend modes

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 545 |
| **Created** | 2026-08-03 |
| **Blocks** | T0080 |
| **Refs** | [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D15, T0060, T0097 |

## Why

T0080 designs emitters in detail — shapes, rates, bursts, curves over lifetime,
sorting, budgets — and subtask 80.4 draws them as "camera-facing quads". It
never says **what is on the quads**. That is the entire visual half of a VFX
system and it is currently undefined.

A muzzle flash *is* a sprite. An explosion is normally a **flipbook**: a grid of
frames in one texture, advanced over the particle's lifetime, so a single quad
shows fire igniting, expanding and dissipating. Smoke is a soft-edged sprite that
must fade where it meets geometry. None of these are emitter behaviour; they are
all texturing and blending.

Without this ticket the particle system can emit ten thousand correctly
simulated white squares.

## Done when

- [ ] A particle's material references a texture, and the quad samples it
- [ ] **Flipbook animation**: a sprite sheet with rows/columns, frame advanced by
      the particle's normalised age, so one emitter plays an explosion sequence
- [ ] Sub-frame blending between flipbook frames, so slow effects do not visibly
      step
- [ ] Blend modes selectable per effect: at minimum **additive** and **alpha**
- [ ] **Soft particles** — fade where the quad intersects scene depth, so smoke
      does not slice visibly through the floor
- [ ] Per-particle colour and opacity from T0080's curves modulate the sampled
      texture
- [ ] Works on both targets

## Subtasks

- [ ] 106.1 Sprite sheet asset shape: the texture plus rows, columns and frame
      count. Decide whether it is a distinct asset type or metadata on a texture
      import (T0097)
- [ ] 106.2 Flipbook UV computation in the vertex or compute stage from
      normalised particle age
- [ ] 106.3 Frame-to-frame blending (sample two frames, lerp) with a toggle —
      it costs a second sample and is not always wanted
- [ ] 106.4 Blend mode as material state: additive, alpha, and consider
      premultiplied alpha, which avoids the halo artefacts the other two produce
      at sprite edges
- [ ] 106.5 Soft particles: sample scene depth, fade over a configurable range.
      Requires depth readable while drawing transparents (T0046)
- [ ] 106.6 Texture atlas support so many small effects share one texture and
      one draw — see the T0097 note below
- [ ] 106.7 Decide whether particle textures participate in lighting at all, or
      are always unlit/emissive (see notes)

## Notes / findings

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
