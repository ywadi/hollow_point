# T0085 — Object layers and masks

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 430 |
| **Created** | 2026-08-03 |

## Inherited from T0081 (2026-08-05)

**The `Camera` component already carries a layer mask field and nothing filters
on it.** T0081 closed with 81.5 unbuilt and moved here, because filtering would
have meant inventing the vocabulary this ticket owns — what an object layer *is*
— inside a camera ticket.

So this ticket owes the camera path a definition and a filter: `parseScene`
(`hp/DrawSubmission.hpp`) is where the filter belongs, since its output is
already the explicit list a cull pass is meant to be inserted around.

## Why

A cross-cutting filter used in the hottest paths of three subsystems:

| Consumer | Uses it for |
|---|---|
| **Camera** | culling mask — which objects this camera renders at all |
| **Lighting** | light mask — which objects a light illuminates, and which cast its shadows |
| **Physics** | collision matrix — which layers collide with which |

Concretely: put an object on layer `Player` and a light on a mask excluding it,
and that light does not affect the player. Put the first-person weapon on
`Viewmodel` and only the weapon camera renders it.

Without this, the only tools are creating separate scenes or hacking per-object
flags into each subsystem — exactly the situation to avoid.

## Done when

- [ ] Every renderable entity has a layer
- [ ] Cameras carry a culling mask; objects outside it are rejected during culling
- [ ] Lights carry a mask for illumination, and separately for shadow casting
- [ ] Masks are applied during culling, not per-pixel — see notes
- [ ] Layers are **named in project settings** (T0078), not bare numbers
- [ ] The inspector shows names and a multi-select mask editor, never a raw integer
- [ ] Physics collision layers use the same definitions (T0051)
- [ ] Debug view showing what a given camera or light actually affects

## Subtasks

- [ ] 85.1 Layer definition in project settings — a fixed small set with names
- [ ] 85.2 Layer field on the renderable/transform component
- [ ] 85.3 Camera culling mask, applied in T0045's culling pass
- [ ] 85.4 Light illumination mask, applied during per-object light selection (T0079)
- [ ] 85.5 Separate shadow-casting mask — an object can be lit but not cast (T0086)
- [ ] 85.6 Mask editor widget in the inspector, via reflection (T0053)
- [ ] 85.7 Physics collision matrix using the same layers (T0051)
- [ ] 85.8 Debug visualisation

## Notes / findings

**Layers are not gameplay tags (T0074), and conflating them is a real mistake.**

| | Layers | Tags |
|---|---|---|
| Count | small fixed set (32) | unlimited |
| Structure | flat bitmask | hierarchical |
| Purpose | engine filtering in hot loops | gameplay classification |
| Cost | one AND per test | a lookup |

Layers exist because culling and light selection run over every object every frame
and need a single-instruction test. Tags exist because gameplay wants to say
`enemy.flying.boss`. Use layers for what the *engine* filters, tags for what the
*game* means.

**Filter during culling, not in the shader.** A per-pixel mask test wastes all the
work of drawing the object. The mask belongs in T0045's culling pass and in
per-object light selection, so excluded work is never submitted.

**32 layers is the conventional limit** because a `uint32` mask is one register.
That is almost always enough, and going wider costs in the hottest loop in the
renderer. Name them in project settings so `layer 7` never appears in code.

Separate illumination and shadow masks matter more than expected — "lit by this
light but does not cast its shadow" is a common requirement for characters and
foliage.
