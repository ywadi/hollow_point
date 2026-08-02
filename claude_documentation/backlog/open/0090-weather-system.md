# T0090 — Weather system

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Low |
| **Complexity** | Complex |
| **Phase** | 11 — World & environment |
| **Created** | 2026-08-03 |

## Why

Weather is not one feature — it is a coordinator over several existing systems.
Rain is particles plus fog plus lighting changes plus audio plus surface wetness,
and it only reads as weather when they change *together*.

Listed late deliberately: it depends on particles (T0080), lighting (T0079), sky
(T0088), fog (T0089) and audio (T0052). Building it before those exist means
building stubs.

## Done when

- [ ] Named weather states (clear, overcast, rain, storm, snow, fog) as data
- [ ] **Smooth transitions between states**, not instant switches
- [ ] Precipitation via the particle system, camera-relative (see notes)
- [ ] Weather modulates sun intensity, ambient and fog coherently
- [ ] Wind as a global value that particles and foliage can read
- [ ] Weather-driven audio: rain loops, thunder, wind (T0052)
- [ ] Gameplay can query and set weather
- [ ] Cost is bounded — heavy weather must not halve the frame rate

## Subtasks

- [ ] 90.1 Weather state as an authorable asset — a set of parameters
- [ ] 90.2 Weather service as a project-scoped autoload (T0076)
- [ ] 90.3 Blend all parameters on transition, over a duration
- [ ] 90.4 Precipitation emitters that follow the camera
- [ ] 90.5 Wind as a global vector, sampled by particles and shaders
- [ ] 90.6 Modulate sun, ambient and fog from the weather state
- [ ] 90.7 Audio hooks
- [ ] 90.8 Drive the global wetness value consumed by T0092
- [ ] 90.9 Editor: preview and scrub weather without entering play mode

## Notes / findings

**Precipitation follows the camera; it is not simulated across the world.** A
rain volume around the camera, with particles spawned above and recycled below, is
how this is done — simulating rain over a whole level is enormously wasteful and
looks identical. The particles must be *camera-relative* or fast camera movement
leaves visible gaps.

**Transitions are what make weather convincing**, and they are the part usually
skipped. Every parameter — sun intensity, fog density and colour, particle rate,
audio volume — must blend over seconds. An instant switch reads as a bug. This is
why weather is a coordinator with one blend, rather than each system reacting
independently.

**It is mostly a modulation layer, not new rendering.** Almost everything it does
is adjusting parameters other systems already expose. That is the design to aim
for: if weather needs its own rendering path, something below it is missing a
parameter it should have had anyway.

**Surface wetness is what sells rain, more than the particles** — it is now T0092,
which is where the material work lives. Weather's job is only to drive the global
wetness value and let T0092 apply it.
