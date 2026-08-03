# T0094 — Gameplay-extensible rendering

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 4 — Render layer |
| **Order** | 510 |
| **Created** | 2026-08-03 |

## Why

Game code has to be able to extend the renderer without modifying the engine.
The immediate driver is explored-area fog of war (T0093): the engine deliberately
does **not** implement that policy, so the game must be able to own a persistent
texture, accumulate visibility into it each frame, and sample it back in a
material.

That is not a niche case. Minimaps, portal and mirror views, security-camera
monitors, custom post effects, decal buffers and player-drawn markers all need the
same three capabilities. Without them, every one of these becomes an engine patch
— exactly the outcome this architecture exists to prevent.

## Done when

- [ ] Gameplay can create and own a **persistent render target** that survives frames
- [ ] Gameplay can implement a render layer and insert it into the RenderStack (T0027)
- [ ] Custom passes can read engine resources — visibility (T0093), depth, GBuffer
- [ ] A gameplay-owned texture is bindable as a material parameter (T0060)
- [ ] Ordering relative to engine layers is explicit and controllable
- [ ] Render targets can be read back to CPU, for saving (T0083)
- [ ] **All of it survives a gameplay hot reload** (T0048) — see notes
- [ ] Misuse fails loudly in debug rather than corrupting the frame

## Subtasks

- [ ] 94.1 `RenderTexture` resource ownable by gameplay, with explicit lifetime
- [ ] 94.2 Allow `IRenderLayer` implementations from the gameplay module
- [ ] 94.3 Insertion and ordering API for custom layers
- [ ] 94.4 Documented read access to engine resources — visibility, depth, colour
- [ ] 94.5 Bind a gameplay texture as a material parameter
- [ ] 94.6 Async readback to CPU
- [ ] 94.7 **Hot-reload safety** — unregister and re-register custom layers
- [ ] 94.8 Debug validation: writing a target being read, using a freed target
- [ ] 94.9 Worked example: accumulate visibility into a persistent texture

## Notes / findings

**Hot reload is the constraint that makes this non-trivial**, and it is the same
problem as T0062. A render layer implemented in the gameplay module has a vtable
pointing into that module; on reload the RenderStack holds a dangling pointer and
the next frame crashes — during rendering, which is a miserable place to debug.

The fix mirrors behaviours: custom layers are unregistered before unload and
re-registered after, with their state serialized across. **Render targets
themselves must be engine-owned** and merely *referenced* by the module, or the
GPU resource dies with the module and takes the accumulated fog of war with it.

**Resource state is the other hazard.** Transitions are not thread-safe (T0050),
and a custom pass that reads a target the engine is writing this frame produces
undefined results. Validate in debug builds — a clear assert beats a driver-level
mystery.

**Do not expose Diligent types directly** across the gameplay boundary if it can
be avoided. It couples game code to the RHI and makes replacing or upgrading
Diligent much harder. A thin engine-owned wrapper is worth the indirection here.

The worked example in 94.9 is not optional documentation — this is a capability
whose whole point is being usable by someone who did not write the engine.


### Amendment (2026-08-03) — the submission seam decides whether game draws can be profiled

Add to this ticket's acceptance: **a GPU zone can be scoped around work
submitted by gameplay code, and it appears correctly attributed in a Tracy
capture.**

This is not a profiling nicety that belongs only to T0030. It is a constraint on
*this* ticket's API shape, because a submission interface that has no place to
put a zone scope cannot gain one later without changing every call site. Game
code does not own the device or the command list, so if this seam does not
carry the zone, game draws are attributed to whichever engine pass wrapped them
— which hides the cost of exactly the code a game developer is trying to
profile.

See T0030's amendment for the mechanism and T0029's for why the Tracy client
lives in the engine shared library.
