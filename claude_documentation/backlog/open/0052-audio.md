# T0052 — Audio engine

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Complex |
| **Phase** | 10 — Audio |
| **Order** | 820 |
| **Created** | 2026-08-03 |
| **Refs** | T0100, [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md) |

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
- [ ] **Subtitles/captions**: playback events expose what is playing, with
      caption text/timing hooks (see the 2026-08-03 accessibility note)

## Notes / findings


### Frame anatomy — phase 8 — late update (T0100, D17)

Audio listener sync belongs in **phase 8 (late update)**, alongside cameras, so
it reads final transforms rather than whatever update order happened to produce.

The full order is in [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md); the decision and what it rejected is **D17** in the
decision log. If this ticket needs a phase that does not exist, that is a change
to T0100's document and to D17 — not a new call bolted into `Application::run`.

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


### Architecture decision (2026-08-03) — the backend question is answered; the engine question is not (D16)

SDL3 is the platform layer, and it includes audio. That removes the *backend*
blocker this placeholder epic carried: there is a cross-platform way to get
samples to the device on both targets without choosing another dependency.

It does **not** answer this ticket. SDL's audio is deliberately low-level — a
callback and a stream of samples. What a game needs is mixing, 3D positional
audio with attenuation and occlusion, buses and submixes, streaming for music,
and playback driven by animation events (T0049). That is an audio *engine*, and
it sits on top of SDL rather than being replaced by it.

So the choice narrows rather than closes: build a mixer over SDL audio, or
vendor a higher-level library (miniaudio is single-header and permissive and
would sit alongside SDL rather than conflict with it). Worth deciding before
Phase 10 rather than during it, because animation-event-driven playback (T0049)
and the message bus (T0075) both assume an API that does not exist yet.

### Accessibility note (2026-08-03) -- captions are scoped now, while the API is unshaped

From the design-gap survey (`documentation/07-design-gaps.md`, item 11):
`subtitle` had zero hits anywhere. Subtitles/captions belong in this epic's
scope because the event-driven playback this ticket already plans is exactly
where captions hook -- a played sound event that can carry "what text, when" is
a caption source for free. **Retrofitting caption timing into an audio API
that never considered it is the expensive version.** Nothing needs building
until this epic breaks into tickets; the scope bullet above is the whole fix
today. Caption *display* is T0069's side; caption text identity follows
T0112's string-key convention.
