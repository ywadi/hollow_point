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
