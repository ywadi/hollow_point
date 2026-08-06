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

### Surveyed 2026-08-06 — scattering is **not** fully blocked behind compute (T0150)

The natural assumption is that `EpipolarLightScattering` waits for the compute
subsystem this engine does not have. **Measured, only its high-quality path
does.**

| Needs compute | Does not |
|---|---|
| LUT precompute — 6 passes, **one-time**, only for `SINGLE_SCTR_MODE_LUT` and `MULTIPLE_SCTR_MODE_UNOCCLUDED/OCCLUDED` | ray march, coordinate texture, coarse inscattering, min/max shadow map, luminance, sun disc, ambient sky light |
| `RefineSampleLocations` — **per-frame**, only under `LIGHT_SCTR_TECHNIQUE_EPIPOLAR_SAMPLING` | — |

Everything in the right column is `InitializeFullScreenTriangleTechnique`, i.e. a
pixel shader, **including the min/max shadow-map steps despite their names**.

**So a reduced configuration ships before T0150**: `LIGHT_SCTR_TECHNIQUE_BRUTE_FORCE`
+ `SINGLE_SCTR_MODE_INTEGRATION` + `MULTIPLE_SCTR_MODE_NONE` needs **zero**
compute, at a real cost — no epipolar acceleration, no multi-scattering. Full
quality needs T0150 first. That is a sequencing option this ticket did not have
before, and 88.1 should decide which it targets rather than inheriting the
assumption that it is blocked.

The full settings surface — ~40 struct fields and 6 technique enums, the largest
of anything surveyed — is inventoried in
[../../documentation/12-vendored-capabilities.md](../../documentation/12-vendored-capabilities.md).

**From D32 (2026-08-06):** when the sky shader is built, it is authored
against an interface a game can implement — the same default-methods shape as
`IHpMaterial` — not as a sealed engine file. One design constraint now; the
retrofit D27 exists to prevent, later.

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
