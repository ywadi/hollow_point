# T0129 — Display modes: fullscreen, resolution, DPI and monitors

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 376 |
| **Created** | 2026-08-04 |
| **Found by** | T0025 review — asked whether the engine supports fullscreen; it does not, and nothing owned it |
| **Refs** | [../completed/0015-window-platform-layer.md](../completed/0015-window-platform-layer.md), [../inprogress/0110-presentation-and-frame-pacing.md](../inprogress/0110-presentation-and-frame-pacing.md), [../completed/0025-render-layer.md](../completed/0025-render-layer.md), T0078, [../inprogress/0068-input-mapping.md](../inprogress/0068-input-mapping.md), T0119, [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D16 |

## Why

**The engine cannot go fullscreen.** `WindowConfig` is five fields — title,
width, height, resizable, openGLContext — and `Window` exposes `create`,
`pumpEvents`, `width()`, `height()`, `title()` and `nativeHandles()`. A grep for
`fullscreen`, `borderless`, `display mode` or `SDL_SetWindowFullscreen` across
`engine/` returns nothing.

Missing: fullscreen (exclusive or borderless), windowed↔fullscreen switching,
runtime resolution changes, monitor enumeration and selection, and DPI/display
scale queries.

**This is not a newly discovered gap — it is one that fell through a closed
ticket**, which is worth stating plainly because it is the failure mode T0124's
both-ways rule exists to prevent. T0015 has a section headed *"display modes
have no owner"*:

> No ticket owns fullscreen/borderless/windowed switching, monitor enumeration,
> resolution changes at runtime, or DPI awareness — yet T0078's game options
> list "resolution" as a player-facing setting, so *something* must implement
> applying it. That something is this platform layer.

It went further and scoped a floor — borderless-fullscreen toggle, runtime
resolution change, basic DPI handling, with exclusive fullscreen and
multi-monitor deferred until there is evidence — and a later note added
*"clipboard, DPI/display scale, monitor enumeration and fullscreen/borderless.
None were in this ticket and all are needed by the editor."*

Then T0015 closed, and its **"Not done" section does not mention any of it**. It
hands Wayland to T0119 and input to T0018, and the display-mode work is simply
not carried anywhere. Named twice as unowned and needed, then dropped.

Three tickets already lean on it existing: **T0078** lists resolution as a
player-facing setting, **T0110** owns focus loss and presentation which interact
with fullscreen directly, and **T0068**'s cursor amendment cites "T0015's
display-modes note" as though it were live work.

## Done when

- [x] Borderless-fullscreen and windowed, switchable **at runtime** — measured on Linux; Windows path shares the code but was not run
- [x] Runtime resolution change, applied without recreating the device
- [x] DPI / display scale is queryable, and logical-vs-pixel is stated in the header
- [x] Monitor enumeration, and choosing which monitor a window opens on
- [x] **Exclusive fullscreen decided: rejected**, with the reason and the reopening evidence recorded in `DisplayMode`'s own doc comment
- [ ] Fullscreen state survives focus loss correctly, honouring whatever policy
      T0110 sets — alt-tabbing out of fullscreen and back is the case that
      reliably breaks
- [ ] Verified on both targets, with what was checked pasted in

## Subtasks

- [x] 129.1 API — mode is **both**: a creation parameter (avoids a windowed flash at startup) and a runtime call
- [x] 129.2 Borderless fullscreen ↔ windowed at runtime
- [x] 129.3 Runtime resolution change
- [x] 129.4 DPI / display scale query
- [x] 129.5 Monitor enumeration and selection
- [x] 129.6 Exclusive fullscreen — rejected, reasons recorded
- [ ] 129.7 Focus-loss interaction, once T0110 has decided the policy
- [ ] 129.8 Verification pass on both targets

## Notes / findings

**Do 110.3 first.** T0110 decides what focus loss *means* — cap hard, pause,
mute, some combination. Fullscreen adds "and release the display mode" to that
same answer, so building fullscreen first means deciding focus-loss behaviour
twice, in two places, and reconciling them later. The dependency is one-way:
focus-loss policy does not need fullscreen to exist.

**The GPU half is already built.** 25.3 wired `RenderLayer::resize()` to the
window resize event and it is measured working on a live swap chain — 40 resizes
plus 2 full rebuilds under validation, silent. A resolution change is that path
with a different trigger, so this ticket is window-side work rather than
renderer work.

**SDL provides all of it** (D16), so this is API surface to expose rather than
per-platform code to write: `SDL_SetWindowFullscreen`, `SDL_GetDisplays`,
`SDL_GetWindowDisplayScale`, `SDL_SetWindowSize`. What SDL does *not* decide is
the shape of the engine's API, which is the actual work.

**The threading caveat from T0015 still applies and will bite harder here.**
SDL's event pump runs on the thread that owns the window, so dragging the title
bar stalls the loop. Mode switching happens on that same thread and can block
for longer than a frame. Do not build anything that assumes a mode change is
cheap or synchronous-and-fast.

**Wayland is T0119's**, not this ticket's. Fullscreen behaves differently there
and the session here is X11, so anything claimed for Wayland would be unverified
— say so rather than implying coverage.

**A caution on exclusive fullscreen specifically.** It is the one mode that
changes the *display's* state rather than the window's, which is why alt-tab,
multi-monitor and crash-recovery all get harder with it. It also interacts with
presentation: on some drivers it is what enables a true immediate/mailbox
present path. T0110 should be consulted before 129.6 is decided rather than
after.

## Built (2026-08-04) — and one finding that would have been a mystery bug

### It works

```
display 0: 'LC49G95T 49"' 3840x1080 scale=1.00
start:            mode=Windowed   1280x720   scale=1.00
->fullscreen  ok=1 mode=Borderless 3840x1080
->windowed    ok=1 mode=Windowed   1280x720
->setSize(900,600) ok=1            900x600
setSize while fullscreen -> false, deliberately
```

Real monitor enumerated, real mode transitions, and the swap chain follows
without this code touching the device: SDL emits a resize, the pump turns it
into a `WindowResizeEvent`, and the render layer resizes through 25.3's path. A
mode switch and a dragged window edge are the same code, which is what stops
them diverging.

### The finding: mode changes are asynchronous

`SDL_SetWindowFullscreen` does **not** take effect before it returns. SDL applies
the transition when the window system next reports it, so immediately afterwards
`SDL_GetWindowFlags` still describes the *old* state. First measurement, before
the fix:

```
->fullscreen  ok=1 mode=Windowed   1280x720     <- still the old state
->windowed    ok=1 mode=Borderless 3840x1080    <- the previous change, one step late
```

Every query one step behind, which as a bug in a settings UI would look like
"fullscreen needs two clicks" and would be hunted in entirely the wrong place.
`SDL_SyncWindow` blocks until pending state is applied and makes the setter mean
what it says.

**This is also T0015's threading caveat arriving concretely.** The sync blocks on
the thread that owns the window and can take longer than a frame. Anything that
assumes a mode change is cheap is wrong, and now there is a measurement rather
than a warning.

### Exclusive fullscreen: rejected, with the reason in the code

`DisplayMode` has two values and its doc comment says why there is no third: it
changes the *display's* state rather than the window's, which is what makes
alt-tab, multi-monitor and crash-recovery harder. What it buys — a true
immediate present path on some drivers — is a latency optimisation nobody has
measured a need for. The reopening evidence is named there.

### Not done

- **129.7, focus-loss interaction, is not implemented.** T0110 now caps in the
  background and resets input, but nothing releases fullscreen on focus loss.
  The case that reliably breaks — alt-tab out of fullscreen and back — is
  untested, and on X11 with a borderless window it may need nothing; that is a
  guess, not a measurement.
- **129.8, verification on Windows, is not done.** The code is shared and the
  Windows target builds, but no Windows binary has exercised a mode switch.
  Given how much of T0025 turned out to be Windows-specific, that is a real gap
  rather than a formality.
- **`displayIndex` is applied by positioning the window on that display's
  bounds** rather than by any explicit SDL display parameter, because
  `SDL_CreateWindow` takes none. Verified only with one monitor attached, so
  multi-monitor placement is unproven.
