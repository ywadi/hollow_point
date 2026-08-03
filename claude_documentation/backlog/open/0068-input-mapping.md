# T0068 — Input mapping and action system

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 2 — Engine skeleton |
| **Order** | 170 |
| **Created** | 2026-08-03 |

## Why

T0018 delivers raw key and mouse events. Gameplay wants **actions** — "Jump",
"Move", "Interact" — not scancodes. Without that layer, key checks get scattered
through gameplay code and rebinding becomes impossible without touching all of it.

## Done when

- [ ] Actions defined as data, bound to keys, buttons or axes
- [ ] Digital actions report pressed / released / held distinctly
- [ ] Analog actions produce a normalised axis or vector
- [ ] Gamepad supported alongside keyboard and mouse
- [ ] Bindings are serialized (T0020) and user-rebindable
- [ ] **Input contexts** so editor and game bindings do not collide
- [ ] Gameplay never reads a raw key code

## Subtasks

- [ ] 68.1 Action and binding types, defined as data
- [ ] 68.2 Map raw events (T0018) onto actions
- [ ] 68.3 Digital state edges: pressed, released, held
- [ ] 68.4 Analog axes, including composing WASD into a 2D vector
- [ ] 68.5 Gamepad support, with hot-plug handling
- [ ] 68.6 Input contexts with priority, and consumption between them
- [ ] 68.7 Serialize bindings; support rebinding at runtime
- [ ] 68.8 Dead zones and sensitivity for analog input

## Notes / findings

**Input contexts are the part people leave out and regret.** While the editor has
focus, the game must not receive input; while a menu is open, gameplay bindings
must be suppressed; play mode needs game input while the editor keeps its stop
shortcut. A stack of contexts with consumption — mirroring the event system's
semantics (T0018) — handles all of it uniformly.

Gamepad support has no obvious answer in Diligent's platform layer, so check what
`NativeApp` exposes before assuming a library is needed. If one is, prefer
something small and permissive that cross-compiles cleanly to MinGW, given G2/G3/G4.

Edge detection needs care with variable frame rates: a key pressed and released
within one frame must still register, which means consuming events rather than
polling state at frame boundaries.

### Architecture review (2026-08-03)

- **Gamepad: verified absent.** Grepped DiligentTools — `NativeApp` has no
  gamepad, joystick or XInput code at all (the only input translation Diligent
  has lives in DiligentSamples' InputController, which is keyboard/mouse and
  off-limits anyway). So gamepad is entirely ours: XInput on Windows is a
  header away under MinGW; on Linux, raw `evdev`/`js` device reads need no
  extra libraries, but **udev-based enumeration/hot-plug would need a library
  the vendored sysroot does not provide** (D3/D4) — prefer plain
  `/dev/input` scanning, or extend the sysroot deliberately.
- **Relative mouse / capture mode is missing from the subtasks.** Fly camera
  (T0063), any third-person orbit and most gameplay cameras need
  capture-and-hide with relative deltas (and it interacts with the OS pump —
  T0015). Small, but add it here rather than discovering it in T0063.
- **Hot-reload hazard:** action *callbacks* registered by the gameplay module
  dangle on reload (same as every module-owned function pointer — T0048).
  Either gameplay polls actions by name/handle (safe), or callback
  registration must be rebuilt on reload like behaviours. Decide which; do not
  offer both silently.


### Architecture decision (2026-08-03) — SDL3 supplies the device layer (D16)

The review above concluded gamepad support was "entirely ours": XInput on
Windows, and raw `/dev/input` scanning on Linux because udev-based hot-plug
would need a library the vendored sysroot does not provide. **That work
disappears.** SDL3 provides gamepad enumeration, the SDL_GameControllerDB
mapping database so different controllers report one layout, hot-plug events,
and **rumble** (`SDL_RumbleGamepad`, plus trigger rumble on hardware that has
it).

What this ticket still owns, and it is the valuable half:

- The **action mapping** layer — physical inputs to named actions — which is
  engine design, not platform code, and nothing off the shelf provides
- **Input contexts with consumption**, which the notes above correctly identify
  as the part people leave out and regret
- **Edge detection across variable frame rates** — a key pressed and released
  within one frame must still register, so events are consumed rather than
  state polled at frame boundaries
- **Relative mouse capture**, flagged above as missing from the subtasks. SDL
  provides the mechanism; this ticket owns when it is engaged
- The **hot-reload hazard** for action callbacks registered by the gameplay
  module, which is unaffected by any of this

68.5 ("gamepad support, with hot-plug handling") shrinks from a platform
implementation to wiring SDL's events into the action layer. **Rumble is now
in scope** and was not before — it needs a place in the action/feedback API,
and output-to-the-player is a different shape from input-from-the-player.


### Amendment (2026-08-03) — cursor control is three features, and only one was noted

The review above flags relative mouse capture as missing from the subtasks. It
is one of three related things a game needs, and the other two are not mentioned
anywhere in the backlog. SDL3 provides all three (D16), so this is API surface to
expose rather than platform code to write:

- **Hide / show the cursor** — `SDL_HideCursor()` / `SDL_ShowCursor()`. Needed
  by any game that draws its own reticle, and by fullscreen play generally.
- **Pin the cursor** (relative mode) — `SDL_SetWindowRelativeMouseMode()`. The
  cursor stops moving and the game receives deltas instead. This is the one
  already flagged: fly cameras (T0063), orbit cameras and most gameplay cameras
  need capture-and-hide together, and it interacts with the OS pump (T0015).
- **A custom cursor image** — `SDL_CreateColorCursor()` from a surface, or
  `SDL_CreateSystemCursor()` for standard shapes. **Genuinely absent from the
  backlog**, and the one with a dependency the others do not have: a
  game-defined cursor is an *asset*. It has to be loaded (T0023) and imported
  (T0097), so it is not purely an input concern and should not be designed as if
  it were.

Three things worth getting right, none of which is obvious from the API:

**Hide and pin are independent**, and conflating them is a common bug. A menu
may want a visible cursor that is confined to the window; a first-person camera
wants hidden *and* relative. Expose them separately even though they are usually
used together.

**Cursor state belongs to the input-context stack**, not to whoever set it last.
The context that owns input decides the cursor, so entering a menu while flying
restores the pointer and leaving it re-hides — without every camera and every
panel having to remember to undo what it did. This is the same consumption model
as the rest of this ticket, applied to an output.

**Relative mode must survive focus loss.** Alt-tabbing out of a captured game
and back is a case that reliably breaks: the cursor must be released on focus
loss and re-captured on focus gain, or the pointer is trapped in a window the
user is not looking at. This interacts with the focus-loss policy T0110 owns.
