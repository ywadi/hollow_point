# T0092 — Wet surfaces

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Complex |
| **Phase** | 11 — World & environment |
| **Order** | 860 |
| **Created** | 2026-08-03 |

## Why

**Wetness sells rain more than the particles do.** Falling droplets are a small
part of what makes rain read as rain; the world turning dark, glossy and
reflective is most of it. Rain without wet surfaces looks like particles in front
of a dry scene.

Physically, a water layer does four things, and all four are needed for it to look
right:

| Effect | Why |
|---|---|
| **Darkens albedo** | light is absorbed within the water layer — wet asphalt is much darker |
| **Reduces roughness** | water fills microscopic pits, making the surface mirror-like |
| **Adds Fresnel** | the water-air interface reflects strongly at grazing angles |
| **Pools in concavities** | puddles form where water collects |

## Done when

- [ ] A global wetness value, driven by weather (T0090), animates up and dries down
- [ ] Materials darken and smooth as wetness rises
- [ ] **Per-material porosity** — concrete soaks and darkens, metal and glass do not
- [ ] Puddles accumulate on up-facing surfaces rather than uniformly
- [ ] **Surfaces under cover stay dry** — see notes, this is the hard part
- [ ] Rain ripples animate on wet surfaces while it is actually raining
- [ ] Drying is gradual and plausible after rain stops
- [ ] Cost is a modest addition to the material shader, not a separate pass

## Subtasks

- [ ] 92.1 Global wetness parameter on the weather service (T0090)
- [ ] 92.2 Material response: albedo darkening and roughness reduction (T0060)
- [ ] 92.3 Porosity parameter per material, controlling how much wetness applies
- [ ] 92.4 Puddle mask — from world normal, plus a noise or authored mask
- [ ] 92.5 **Rain occlusion map** — a depth render from above, so covered surfaces
      stay dry
- [ ] 92.6 Animated ripple normals on puddles during rainfall
- [ ] 92.7 Drying curve after rain stops
- [ ] 92.8 Optional: water flow/streaks on vertical surfaces

## Notes / findings

**The occlusion map is what separates convincing from obviously fake.** Without
it, the floor under a bridge, inside a doorway or beneath a car gets just as wet as
open ground, and the effect stops reading immediately. The technique is a depth
map rendered orthographically from above — essentially a shadow map with the sky
as the light — sampled in the material to decide whether rain reaches that point.
It reuses the shadow infrastructure from T0086.

**Porosity matters more than expected.** Applying uniform wetness makes metal and
glass look wrong, because they do not absorb — they only gain a thin reflective
film. A per-material porosity value, authored alongside roughness, is the fix and
costs one parameter.

**Puddles from world normal alone are not enough** — every up-facing surface
becomes a puddle, including tabletops and roofs. Combine an up-facing test with
noise, an authored mask, or ideally a height/cavity signal so water collects in
low points.

**This is material work, not a post effect.** It belongs in the shading path
(T0060) as a modulation of existing parameters, which keeps it cheap. A screen-space
wetness pass would be both more expensive and less correct.

Ripples should only animate while rain is actively falling; leaving them running
on a drying surface is a small tell that breaks the illusion.
