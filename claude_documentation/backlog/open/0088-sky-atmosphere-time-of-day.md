# T0088 — Sky, atmosphere and time of day

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Complex |
| **Phase** | 11 — World & environment |
| **Order** | 830 |
| **Created** | 2026-08-03 |

## Why

T0087 gives a static skybox and image-based ambient — enough for PBR to look
correct, and the right foundation. This is the dynamic layer on top: a sun that
moves, a sky that responds, and ambient light that changes with it.

The enabler already exists: **DiligentFX ships `EpipolarLightScattering`**, a
physically-based atmospheric scattering implementation, and the previous
`terrain_lab` app used it. So a procedural sky is plumbing rather than research.

## Done when

- [ ] A procedural sky driven by sun direction, not a fixed cubemap
- [ ] The sun is a directional light whose direction, colour and intensity follow
      time of day
- [ ] Sunset/sunrise reddening falls out of scattering rather than being faked
- [ ] **Ambient/IBL updates as the sky changes** — see notes, this is the hard part
- [ ] Time of day is controllable: paused, scrubbed, or advancing at a set rate
- [ ] Night: moon or a night sky, and ambient that does not go pure black
- [ ] Cost is bounded — sky and probe updates must not spike the frame

## Subtasks

- [ ] 88.1 Wire `EpipolarLightScattering` into the render stack
- [ ] 88.2 Time-of-day service, with rate control and scrubbing
- [ ] 88.3 Sun direction from time, latitude and date
- [ ] 88.4 Sun colour and intensity from elevation
- [ ] 88.5 **Dynamic environment probe capture and prefilter, amortised**
- [ ] 88.6 Night sky and moon
- [ ] 88.7 Editor: scrub time of day and see it update live
- [ ] 88.8 Profiling zones — sky and probe updates are easy to make expensive

## Notes / findings

**Dynamic ambient is the expensive part, not the sky itself.** Drawing a
procedural sky is cheap. Keeping image-based ambient in sync means re-capturing
the environment and re-running the prefilter chain (T0087) as the sun moves — and
doing that every frame is far too slow.

The standard answer is amortisation: update the probe every N frames, or spread
the mip chain across frames, and interpolate between two prefiltered sets. Decide
this early, because "recompute the probe each frame" works fine in a test scene
and collapses in a real one.

**Time of day is a service, not a component** — it is global state, so it belongs
in a project-scoped autoload (T0076). Systems read from it rather than each
tracking their own notion of time.

Do not let night go fully black. Physically it nearly does; visually it makes the
game unplayable. A moonlight floor and raised ambient are deliberate art
decisions, not bugs to fix.
