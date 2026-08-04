# T0129 — Display modes: fullscreen, resolution, DPI and monitors

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 4 — Render layer |
| **Order** | 376 |
| **Created** | 2026-08-04 |
| **Found by** | T0025 review — asked whether the engine supports fullscreen; it does not, and nothing owned it |
| **Refs** | [../completed/0015-window-platform-layer.md](../completed/0015-window-platform-layer.md), [../inprogress/0110-presentation-and-frame-pacing.md](../inprogress/0110-presentation-and-frame-pacing.md), [../inprogress/0025-render-layer.md](../inprogress/0025-render-layer.md), T0078, [../inprogress/0068-input-mapping.md](../inprogress/0068-input-mapping.md), T0119, [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D16 |

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

- [ ] Borderless-fullscreen and windowed, switchable **at runtime**, on both
      targets
- [ ] Runtime resolution change, applied without recreating the device — the
      swap chain resizes through the path 25.3 already built and tested
- [ ] DPI / display scale is queryable, and the difference between logical and
      pixel size is stated rather than assumed (the window already sets
      `SDL_WINDOW_HIGH_PIXEL_DENSITY`, so they already differ on a scaled
      display)
- [ ] Monitor enumeration, and choosing which monitor a window opens on
- [ ] **Exclusive fullscreen is decided, not skipped** — either implemented, or
      rejected with the reason recorded. T0015 deferred it "until evidence";
      name what evidence would change it
- [ ] Fullscreen state survives focus loss correctly, honouring whatever policy
      T0110 sets — alt-tabbing out of fullscreen and back is the case that
      reliably breaks
- [ ] Verified on both targets, with what was checked pasted in

## Subtasks

- [ ] 129.1 `WindowConfig` and `Window` API for mode, resolution and monitor.
      Decide whether mode is a creation parameter, a runtime call, or both —
      `openGLContext` is already a case where the answer had to be "creation
      only", and the reason should be visible here too
- [ ] 129.2 Borderless fullscreen ↔ windowed at runtime
- [ ] 129.3 Runtime resolution change, driving the existing swap-chain resize
- [ ] 129.4 DPI / display scale query, and logical-vs-pixel stated in the header
- [ ] 129.5 Monitor enumeration and selection
- [ ] 129.6 Exclusive fullscreen: implement or reject with reasons
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
