# T0068 — Input mapping and action system

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 2 — Engine skeleton |
| **Order** | 170 |
| **Created** | 2026-08-03 |
| **Refs** | T0100, [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md), T0110, T0112 |

## Why

T0018 delivers raw key and mouse events. Gameplay wants **actions** — "Jump",
"Move", "Interact" — not scancodes. Without that layer, key checks get scattered
through gameplay code and rebinding becomes impossible without touching all of it.

## Done when

- [x] Actions defined as data, bound to keys, buttons or axes
- [x] Digital actions report pressed / released / held distinctly
- [x] Analog actions produce a normalised axis or vector — for keyboard composition; a *device* analog axis needs 68.5
- [ ] Gamepad supported alongside keyboard and mouse — **not done**, see below
- [ ] Bindings are serialized (T0020) and user-rebindable — **blocked on T0020.** The map is data so a loader is all that is missing, but there is no serializer to write one against
- [x] **Input contexts** so editor and game bindings do not collide
- [x] Gameplay never reads a raw key code — there is no API that would let it

## Subtasks

- [x] 68.1 Action and binding types, defined as data
- [x] 68.2 Map raw events (T0018) onto actions — consumed at frame phase 2
- [x] 68.3 Digital state edges: pressed, released, held — including a tap inside one step, and auto-repeat excluded
- [x] 68.4 Analog axes, including composing WASD into a 2D vector
- [ ] 68.5 Gamepad support, with hot-plug handling — **not done.** SDL3 supplies it (D16), but no engine event type carries gamepad input yet and there is no controller here to verify against
- [x] 68.6 Input contexts with priority, and consumption between them
- [ ] 68.7 Serialize bindings; support rebinding at runtime — **blocked on T0020**
- [x] 68.8 Dead zones and sensitivity for analog input

## Notes / findings


### Frame anatomy — phase 3a — input snapshot (T0100, D17)

The input snapshot is **phase 3a**, inside the fixed-step loop. Each fixed step
must see one unchanging view of input; sampling live mid-block makes two steps
in the same frame disagree about what the player did.

The full order is in [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md); the decision and what it rejected is **D17** in the
decision log. If this ticket needs a phase that does not exist, that is a change
to T0100's document and to D17 — not a new call bolted into `Application::run`.

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

### Cross-ticket obligations (2026-08-04, T0124 backfill)

- **T0112.3** names this ticket's key display names (rebinding UI) as an early
  consumer of the keys-vs-literals decision — check it before authoring any
  user-facing literal here, or those strings become part of the migration
  T0112 exists to prevent.

## Built (2026-08-04) — `hp::InputSystem`, and the decision the notes demanded

### The hot-reload hazard is decided: **actions are polled, never called back**

The notes said to decide this and "do not offer both silently". Decided: there
is **no callback registration API**, and the header says why.

A gameplay module registering a callback leaves a function pointer into its own
image, and T0048 now genuinely unmaps that image on reload — so the callback
dangles and the crash lands nowhere near the cause. Polling by `ActionId` is
safe across a reload because an id is a hash of a name, not a pointer. That is
the same reasoning that made component identity name-based in T0095, arrived at
from a different direction.

### Edges come from events, and that is testable

`pressed` and `released` are edges *within a step*, accumulated as events arrive
at phase 2 and published whole at the phase-3a snapshot. The case that decides
the design:

```
key down, key up, snapshot  ->  pressed=true, released=true, held=false
```

Sampling "is it down now" against "was it down last step" loses that entirely —
and loses it *more often as the frame rate rises*, so a fast machine drops
inputs a slow one catches. Auto-repeat is explicitly not an edge, or `pressed`
would mean `held` for anything firing on it.

### Contexts consume per input, not per context

A context binding only Escape blocks only Escape. Asserted both ways: a
consuming menu above gameplay stops gameplay's binding for the same key firing,
and a non-consuming overlay does not.

Priority beats push order; ties go to the newest, via `stable_sort`, so pushing
a modal context puts it above an existing one of equal priority without the
caller inventing a number.

### Two bugs the tests caught, both mine, both in the tests

Recorded because the implementation surviving is the point. The first expected
`x == -1` after releasing D while A **and W** were still down — but that is a
diagonal, so the normalised answer is `-1/sqrt(2)`. The test now asserts the
normalised value deliberately, because a "normalised" axis quietly ceasing to be
normalised is exactly the regression worth catching. The second asserted that a
non-consuming overlay makes `onEvent` return false; it returns true, because
gameplay *underneath* consumed it — which is the behaviour the case exists to
demonstrate.

### `Axis2D` is deliberately not Diligent's `float2`

T0056 chose Diligent's math, and the engine deliberately links **nothing** from
Diligent today. Naming a Diligent type in a public engine header would widen
every consumer's dependency surface silently — the hazard `engine/CMakeLists.txt`
already warns about. When T0025 links Diligent for the render layer this becomes
an alias and no call site changes.

### What is not done

- **68.5, gamepad — not started.** D16 settled that SDL3 supplies enumeration,
  the mapping database, hot-plug and rumble, so the platform work is gone. What
  remains is real but was not done: no engine event type carries gamepad input
  yet, so `Event.hpp` needs new types, `Window::pumpEvents` needs to translate
  them, and `InputMap` needs button and stick bindings. **And there is no
  controller on this machine to verify against**, so it would have shipped
  untested — which is worse than shipping it absent. Rumble is output rather
  than input and needs a place in the API that does not exist yet.
- **68.7, serialization — blocked on T0020.** The map is deliberately data, so
  this is a loader rather than a redesign; there is simply no serializer to
  write one against. Recorded on T0020.
- **Cursor control (hide / pin / custom image) — not done.** The 2026-08-03
  amendment adds it to this ticket. It needs `Window` API that does not exist,
  the context stack should own cursor state rather than whoever set it last, and
  relative mode must survive focus loss — which is T0110's policy. Left whole
  rather than half-built, and T0110 has the reference.
- **`reset()` exists but nothing calls it.** It is the focus-loss hook: a window
  that loses focus while a key is down never receives the key-up, so the action
  would stay held forever. Wiring it to a focus event is T0110's call about what
  focus loss *means*, so the hook is provided and the policy is not invented
  here.
- **Mouse motion and scroll are not bound to actions.** Only keys and buttons
  are. A look axis needs relative motion, which is the same cursor-capture work
  above.
