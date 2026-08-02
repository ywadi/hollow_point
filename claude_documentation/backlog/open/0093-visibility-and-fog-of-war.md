# T0093 — Visibility, vision cones and fog of war

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Very Complex |
| **Phase** | 4 — Render layer |
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
- [ ] Optional memory — explored areas remain dimly visible
- [ ] Cost is bounded and profiled (T0030)

## Subtasks

- [ ] 93.1 `VisionSource` component: cone angle, range, falloff, radial radius
- [ ] 93.2 Render vision occlusion maps, reusing the shadow path (T0086)
- [ ] 93.3 Expose the visibility factor as a shading input (T0060)
- [ ] 93.4 A standard visibility response — hide/dim, with parameters
- [ ] 93.5 Dither patterns (Bayer / blue noise) available to material shaders
- [ ] 93.6 Layer mask controlling what occludes vision (T0085)
- [ ] 93.7 Per-object visibility for culling entirely hidden objects (T0045)
- [ ] 93.8 Optional accumulation buffer for explored-area memory
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

**Explored memory changes the storage model.** Instant visibility is computed each
frame and thrown away. "I have seen this area before" needs an accumulation
texture, usually a top-down world-space projection, which persists and must be
saved with the game (T0083). Decide early whether memory is wanted, because it
changes this from a per-frame calculation into persistent state.

**Interaction with normal lighting needs deciding.** Is a visible-but-unlit area
dark, or is visibility purely an overlay independent of illumination? Both are
defensible and they look very different. This is a game-design decision that the
engine should not force — which is the argument for exposing the factor and
letting the material decide.
