# T0093 — Visibility, vision cones and fog of war

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Very Complex |
| **Phase** | 4 — Render layer |
| **Order** | 540 |
| **Created** | 2026-08-03 |

## Why

A required capability, not a stylistic extra: the game developer must be able to
build **vision-based visibility** — a wedge of forward vision plus peripheral
awareness around the player, occluded by geometry, where what is out of sight is
hidden or dimmed.

Think Escape from Duckov's field of view: a spot cone ahead and an omni radius
around the character, both casting shadows off walls, with everything in shadow
invisible and everything lit visible — plus authorable dimming and **dithered**
transitions rather than a hard edge.

The key realisation: this uses the **shadow map machinery to compute visibility
rather than illumination.** Occlusion is the point; light is only the mechanism.

## The architectural constraint this imposes

**Materials must receive the raw visibility factor, not a finished shaded colour.**
If shading is a sealed pipeline that consumes lights and emits pixels, none of
this is expressible and it ends up bolted on as a post-process hack. So:

- the shading path exposes a per-pixel **visibility** value to material shaders
- custom material shaders (T0060) can consume it — dim, desaturate, dither, hide
- it is a first-class input alongside albedo and roughness, not a special case

This is recorded here because it constrains T0060, T0079 and T0086, all of which
are earlier in the plan.

## Done when

- [ ] **Vision source** components — cone (spot) and radial (point) — separate
      from lights used for illumination
- [ ] Vision sources cast shadows off geometry, so walls occlude sight
- [ ] A per-pixel visibility factor is available in material shaders
- [ ] Authorable response: hidden, dimmed by an amount, or fully custom
- [ ] **Dithered transitions** are expressible, not just smooth falloff
- [ ] Which layers occlude vision is configurable (T0085)
- [ ] Objects entirely outside vision can be skipped, not just shaded dark
- [ ] The **primitives** for explored-area memory are exposed, so a game can
      build it — the engine does not implement the policy (see notes)
- [ ] Cost is bounded and profiled (T0030)

## Subtasks

- [ ] 93.1 `VisionSource` component: cone angle, range, falloff, radial radius
- [ ] 93.2 Render vision occlusion maps, reusing the shadow path (T0086)
- [ ] 93.3 Expose the visibility factor as a shading input (T0060)
- [ ] 93.4 A standard visibility response — hide/dim, with parameters
- [ ] 93.5 Dither patterns (Bayer / blue noise) available to material shaders
- [ ] 93.6 Layer mask controlling what occludes vision (T0085)
- [ ] 93.7 Per-object visibility for culling entirely hidden objects (T0045)
- [ ] 93.8 Expose visibility as a sampleable resource so a game can accumulate
      it itself (T0094) — do not build fog-of-war memory into the engine
- [ ] 93.9 Multiple vision sources combined (several characters, or shared team vision)
- [ ] 93.10 Debug view of the visibility buffer (T0061)

## Notes / findings

**Vision sources are not lights, even though they use the same machinery.** They
should be their own component type: a vision cone that also illuminated the scene
would be wrong, and a light that also granted sight would be equally wrong. Keep
them separate and let the developer place both if they want both.

**Per-pixel and per-object are both needed, for different reasons.** Per-pixel
gives correct silhouettes and soft/dithered edges. Per-object lets a fully hidden
enemy be skipped entirely — cheaper, and often *required* so that hidden entities
are not merely dark but genuinely not rendered. Do both: per-object as a coarse
cull, per-pixel for the visual.

**Dithering needs the raw factor plus screen-space coordinates.** A dithered edge
is a threshold against a Bayer or blue-noise pattern in screen space. That means
material shaders need both the visibility value and the screen position — so
expose them as documented shader inputs rather than something a shader has to
reconstruct.

## Decisions (2026-08-03)

**Visibility is independent of illumination.** A visible area renders normally
regardless of what lights reach it; visibility is an overlay, not a multiplier on
lighting. So a dark room inside the vision cone is *visible and dark* — lit by
whatever lights exist — rather than hidden because no light reaches it. Vision and
lighting are two separate concerns that happen to share the shadow machinery.

Practically: the visibility factor is applied as its own term in the material,
after shading, not folded into the lighting accumulation.

**Explored-area memory is NOT an engine feature.** The engine exposes the
primitives; the game builds the policy in an autoload (T0076). Whether explored
areas stay dimly visible, fade over time, are per-player or shared by a team, or
reset on death, are all game-design questions with different right answers — and
baking one in would be wrong.

What the engine must therefore provide (T0094):

- the visibility result as a **sampleable resource**, not just an internal value
- **persistent render targets ownable by gameplay**, surviving across frames
- the ability to run a **custom pass** that accumulates into one
- the ability to **sample that texture in a material shader**
- readback/serialization so it can be saved with the game (T0083)

That list is the actual deliverable of this decision. Without it, "the developer
can build it" is not true, and they would end up patching the engine — which is
precisely the outcome being avoided.
