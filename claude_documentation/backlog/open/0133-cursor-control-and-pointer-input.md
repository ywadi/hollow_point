# T0133 — Cursor control and pointer input as actions

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 2 — Engine skeleton |
| **Order** | 174 |
| **Blocked by** | Nothing. `Window` API has to be written, but nothing is waiting on another ticket |
| **Created** | 2026-08-05 |
| **Refs** | [../completed/0068-input-mapping.md](../completed/0068-input-mapping.md), [../completed/0110-presentation-and-frame-pacing.md](../completed/0110-presentation-and-frame-pacing.md), [../completed/0129-display-modes-and-window-control.md](../completed/0129-display-modes-and-window-control.md), [../completed/0023-asset-manager.md](../completed/0023-asset-manager.md), T0097, T0063 |

## Why

Split out of **T0068** when that ticket closed on 2026-08-05, together with
T0132. T0068 built the action layer; these are the pointer-shaped parts it left
whole rather than half-built.

Two things are missing and they are one job, because the second needs the first:

- **Cursor control** — hide, pin, custom image. `Window.hpp` has **no cursor API
  at all** (verified 2026-08-05, not assumed). T0068's 2026-08-03 amendment added
  this to its scope and it was never built.
- **Mouse motion and scroll are not bound to actions.** `InputMap` binds keys,
  mouse buttons and key-composed 2D axes only. A *look* axis needs relative
  motion, which is the same cursor-capture work — which is why these are one
  ticket and not two.

Without this there is no look input. T0063's fly camera, any third-person orbit
and most gameplay cameras need capture-and-hide, so this blocks camera work that
is otherwise ready to go.

## Done when

- [ ] Hide/show, pin (relative mode) and custom cursor image are each available
      through `Window`, and **hide and pin are independently controllable**
- [ ] Cursor state is owned by the input-context stack, not by whoever set it last
- [ ] Relative mode survives focus loss and regain — alt-tab out of a captured
      window and back leaves neither a trapped pointer nor a lost capture
- [ ] Mouse motion binds to an analog action, so a look axis exists without
      gameplay touching raw deltas
- [ ] Scroll binds to an action
- [ ] A custom cursor loads as an asset (T0023), not from a path the input system
      opens itself

## Subtasks

- [ ] 133.1 `Window` cursor API: show/hide, relative mode, custom image — three
      separate controls, not one mode enum
- [ ] 133.2 Context-stack ownership of cursor state, with the same consumption
      model as the rest of the input stack
- [ ] 133.3 Focus-loss/regain handling for relative mode
- [ ] 133.4 Mouse motion as an analog action source, including sensitivity
      (68.8's dead-zone/sensitivity code applies here)
- [ ] 133.5 Scroll as an action source
- [ ] 133.6 Custom cursor image through the asset pool (T0023) and import (T0097)

## Notes / findings

### Three features, and only one of them was ever noted

Carried from T0068's 2026-08-03 amendment. SDL3 provides all three (D16), so
this is **API surface to expose rather than platform code to write**:

- **Hide / show** — `SDL_HideCursor()` / `SDL_ShowCursor()`. Needed by any game
  drawing its own reticle, and by fullscreen play generally.
- **Pin** (relative mode) — `SDL_SetWindowRelativeMouseMode()`. The cursor stops
  moving and the game receives deltas instead.
- **A custom cursor image** — `SDL_CreateColorCursor()` from a surface, or
  `SDL_CreateSystemCursor()` for standard shapes. This one was **genuinely absent
  from the backlog** before that amendment, and it has a dependency the other two
  do not: a game-defined cursor is an *asset*. It must load through T0023 and
  import through T0097, so it is not purely an input concern and must not be
  designed as if it were.

### The three things that are not obvious from the API

**Hide and pin are independent, and conflating them is a common bug.** A menu may
want a visible cursor confined to the window; a first-person camera wants hidden
*and* relative. Expose them separately even though they are usually used
together — a single "capture mode" enum cannot express the menu case.

**Cursor state belongs to the input-context stack, not to whoever set it last.**
The context that owns input decides the cursor, so entering a menu while flying
restores the pointer and leaving it re-hides — without every camera and every
panel having to remember to undo what it did. This is the same consumption model
as the rest of T0068's input stack, applied to an *output*. The alternative,
last-writer-wins, fails the first time two things want the cursor and one of them
returns early.

**Relative mode must survive focus loss.** Alt-tabbing out of a captured game and
back reliably breaks: the cursor must be released on focus loss and re-captured
on focus gain, or the pointer is trapped in a window the user is not looking at.

The hook for this now exists and did not when the amendment was written.
T0110.3 wired focus into `Application::run` — `engine/src/Application.cpp:114`
calls `input_.reset()` on `WindowFocusLost`, with the focus decision made in the
application rather than in a layer that might consume the event. Cursor release
hangs there. **T0110 is closed, so its focus-loss policy is decided, not open.**

### Same failure site as fullscreen — check T0129 before designing

T0068's closing note (2026-08-04) records this: relative-mouse capture and
fullscreen **fail in the same place**, which is alt-tabbing out and back. T0129
built display modes and is closed; read what it found about focus transitions
before designing 133.3, rather than discovering the same edge independently.

Note also that T0068's amendment cited "T0015's display-modes note" as if it were
live work. It was not — T0015 closed without building any of it, and that work
became T0129. Do not follow the T0015 reference expecting to find a design.

### Stale reference corrected

T0068's "What is not done" listed `reset()` as existing with nothing calling it.
**That is no longer true** — T0110.3 wired it, as above. The remaining focus work
here is cursor release, not action reset.
