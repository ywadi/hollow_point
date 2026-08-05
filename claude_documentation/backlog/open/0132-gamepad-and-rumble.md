# T0132 — Gamepad, rumble and hot-plug

| | |
|---|---|
| **Status** | ⏸ BLOCKED |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 2 — Engine skeleton |
| **Order** | 172 |
| **Blocked by** | **Hardware, not a ticket.** There is no controller on this machine to verify against |
| **Created** | 2026-08-05 |
| **Refs** | [../completed/0068-input-mapping.md](../completed/0068-input-mapping.md), T0112, T0018, [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) (D16) |

## Why

Split out of **T0068** when that ticket closed on 2026-08-05. The action layer —
actions as data, edges, contexts with consumption, binding files, dead zones — is
built and verified. Gamepad is the one part of T0068's "Done when" that was never
ticked, and it was not deferred for a design reason: **there is no controller on
this machine to verify against**, and shipping input handling that has never seen
a device is worse than shipping it absent.

T0068 was closed rather than left parked on that, per the T0095 → T0105 pattern:
a working action layer should not sit behind hardware that may not arrive.

## Blocked on

**A physical controller.** Everything else this needs exists. This is the whole
blocker and it is not resolvable by any other ticket — do not pick this up
expecting to unblock it with code.

## Done when

- [ ] Gamepad supported alongside keyboard and mouse — the "Done when" T0068
      could not tick
- [ ] Buttons bind to digital actions, sticks and triggers to analog actions,
      through the same `InputMap` as keys
- [ ] Hot-plug: connecting or disconnecting a controller mid-session is handled,
      not a crash and not a silently dead input
- [ ] Dead zones and sensitivity (68.8, already built) apply to *device* analog
      axes, which is the case they were written for and never tested against
- [ ] Rumble has a home in the API, with the input/output asymmetry decided
      rather than bolted on
- [ ] Verified **on a real controller**, on both targets. This ticket does not
      close on a code review

## Subtasks

- [ ] 132.1 Engine event types for gamepad — `Event.hpp` carries nothing for
      gamepad today, so this is new surface, not a translation
- [ ] 132.2 `Window::pumpEvents` translates SDL's gamepad events into them
- [ ] 132.3 Button and stick/trigger bindings in `InputMap`, including the
      *named* variants — a gamepad binding must be writable to a binding file
      (68.7) or it cannot be rebound, which is the entire point of that format
- [ ] 132.4 Hot-plug: enumeration at startup, connect/disconnect mid-session
- [ ] 132.5 Rumble — decide where output-to-the-player lives (see below)
- [ ] 132.6 Verify dead zones and sensitivity against a real stick
- [ ] 132.7 Stable identifiers for gamepad buttons/axes in binding files, to the
      same rule as key names

## Notes / findings

### D16 already removed the hard part — do not redo the survey

T0068's architecture review (2026-08-03) concluded gamepad support was
**"entirely ours"**: XInput on Windows behind MinGW, and raw `/dev/input`
scanning on Linux because udev-based enumeration and hot-plug would need a
library the vendored sysroot does not provide (D3/D4).

**That conclusion is superseded and the work is gone.** D16 chose SDL3, which
supplies:

- gamepad enumeration,
- the **SDL_GameControllerDB mapping database**, so different physical
  controllers report one layout instead of every game shipping its own table,
- hot-plug events,
- **rumble** — `SDL_RumbleGamepad`, plus trigger rumble on hardware that has it.

Recorded because the rejected path is the expensive one: anybody re-deriving
"gamepad is entirely ours" will write a device layer that already exists. SDL3's
haptics support is also *why* SDL3 was chosen over GLFW — GLFW has no haptics API
at all — so reaching past it for gamepad would spend the reason for the decision.

So 132.1–132.4 are **wiring SDL's events into the action layer**, not platform
code.

### Rumble is a different shape from everything else here

Every other thing in T0068 and this ticket is *input from the player*. Rumble is
*output to the player*, and there is no place in the API for that today. It does
not belong on `InputMap`, which maps physical inputs to named actions — the
direction is wrong and the type is wrong.

Left undecided deliberately rather than guessed at. Whoever picks this up should
decide it as API design, not squeeze it into the nearest existing header. Note
that it will want the same *context* discipline as the rest: a rumble fired by
gameplay while a menu owns input is as wrong as a movement key firing.

### The rules this must not break, inherited from T0068

Three of T0068's decisions constrain this ticket, and all three are the kind that
are silently violated by an obvious implementation:

- **Actions are polled, never called back.** There is deliberately no callback
  registration API, because a gameplay module's function pointer dangles when
  T0048 unmaps its image on reload. A gamepad "button pressed" callback would
  reintroduce exactly that hazard. Poll by `ActionId`.
- **Gameplay never reads a raw input code** — there is no API that would let it,
  and adding one for gamepad buttons would be the first crack in the rule
  rebinding depends on.
- **Binding-file identifiers are a data format and must never change.** A
  gamepad button's stable name (`"South"`, `"LeftStick"`, …) has the same
  contract as `"Space"`: changing one silently invalidates that binding in every
  file a player saved, and the symptom is a control that stopped working with no
  error anywhere. Round-trip every name through its own table in a test, as the
  key table already does.

Naming is worth a moment: SDL3 names face buttons by **position** (South/East/
West/North) rather than by letter, precisely because A/B/X/Y differ between
vendors. Storing a vendor letter in a binding file bakes in a controller brand.

### Cross-ticket obligations

- **T0112.3** — what a rebinding UI *displays* for a gamepad button is a
  localised string, not the stable identifier. Same split as key names: the file
  stores `"South"`, the UI shows whatever T0112 resolves. Do not author a
  user-facing literal here.
