# T0052 — Audio engine

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Complex |
| **Phase** | 10 — Audio |
| **Order** | 820 |
| **Created** | 2026-08-03 |

> **Placeholder epic**, and the library is **not yet chosen**. Recorded to keep
> the integration points in mind. Break into real tickets when Phase 10 is
> reached.

## Why

No audio exists and Diligent provides none. Beyond the obvious, audio has one
integration point that has already been designed for: **animation event tracks**
(T0049 uses ozz's `track_triggering_job`) are how footsteps, weapon impacts and
vocalisations get triggered in sync with animation. That connection is worth
keeping in view while the animation runtime is built.

## The decision to make

| Option | Trade-off |
|---|---|
| **miniaudio** | Single header, public domain, no dependencies, trivially cross-compiles. Playback and basic 3D; DSP and occlusion are yours to write. |
| **SoLoud** | Permissive, easy API, more built-in features than miniaudio. Less actively developed. |
| **OpenAL Soft** | Mature 3D audio with HRTF. LGPL — worth checking against distribution plans. |
| **FMOD / Wwise** | Industry standard, superb tooling and authoring. Commercial licensing, and heavier to integrate and to ship. |
| **Steam Audio** | Excellent spatialisation/occlusion, sits *on top* of one of the above rather than replacing it. |

Deciding needs answers to: does the game need occlusion and reverb zones, or
just positional playback with volume falloff? Is there a sound designer who needs
authoring tools? And does the licence suit how this ships?

## Rough scope

- [ ] Choose the library — record it in the decision log
- [ ] Confirm it cross-compiles to `x86_64-windows-gnu` (see notes)
- [ ] Audio assets through the asset manager, by GUID (T0023)
- [ ] Audio source component; listener on the active camera
- [ ] 3D positional audio driven by entity transforms
- [ ] **Animation-event-driven playback** (T0049 track triggers)
- [ ] Streaming for music vs fully-decoded for short effects
- [ ] Buses/groups with independent volume, and a mute-all for the editor

## Notes / findings

**Cross-compilation is the first thing to check, not the last.** Audio libraries
bind to platform backends (WASAPI/DirectSound on Windows, ALSA/PulseAudio on
Linux), and this project has already hit three separate MinGW-versus-MSVC issues
(G2, G3, G4). miniaudio is the safest on that front — single header, no external
dependencies.

**Audio runs on its own callback thread, owned by the audio device**, not by
enkiTS. That thread has hard realtime constraints: no allocation, no locks, no
blocking. Mixing that up with the job system is a classic source of audible
glitches.

The **editor needs a global mute**, and play mode (T0037) needs audio to start
and stop with simulation — otherwise sounds leak across play sessions, which is
maddening during development.
