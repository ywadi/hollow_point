# T0072 — Entity signals and messaging

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 3 — Data model |
| **Order** | 320 |
| **Created** | 2026-08-03 |
| **Refs** | T0100, [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md), T0062, [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D23, [../../documentation/09-gameplay-authoring.md](../../documentation/09-gameplay-authoring.md) |

## Why

Entities need to tell each other that something happened — a switch was pressed,
a character died, a trigger volume was entered. Without a mechanism, every
interaction becomes a direct call, and entities end up knowing about each other's
concrete types. That coupling is what makes gameplay code resistant to change.

Godot solves this with signals. The event system (T0018) is for *layers* and does
not address entity-level communication at all.

## Done when

- [ ] An entity can declare signals it emits
- [ ] Another entity can connect a handler to a signal
- [ ] Emitting invokes all connected handlers
- [ ] Connections are severed automatically when either end is destroyed
- [ ] Connections serialize with the scene where authored in the editor
- [ ] Emission has no allocation on the hot path
- [ ] It survives a gameplay hot reload (T0048/T0062)

## Subtasks

- [ ] 72.1 Signal declaration and typed payloads
- [ ] 72.2 Connect / disconnect, with a connection handle
- [ ] 72.3 Automatic disconnection on destruction of either party
- [ ] 72.4 Emit without heap allocation
- [ ] 72.5 Decide: immediate dispatch or deferred to a queue — see notes
- [ ] 72.6 Serialize authored connections; reconnect on load
- [ ] 72.7 Survive hot reload by storing connections as GUID + signal name
- [ ] 72.8 Optional: editor UI to wire signals without code

## Notes / findings


### Frame anatomy — phase 6 — deferred drain (T0100, D17)

Signals drain at **phase 6**, drain-until-empty with an iteration cap because a
handler may post more work. Draining at one fixed point is what lets phase 12
assert the queue is empty before a module reload.

The full order is in [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md); the decision and what it rejected is **D17** in the
decision log. If this ticket needs a phase that does not exist, that is a change
to T0100's document and to D17 — not a new call bolted into `Application::run`.

**Immediate versus deferred dispatch is the real decision.** Immediate is simpler
and easier to debug, but a handler that destroys entities or emits further signals
during iteration causes reentrancy bugs — classically, an entity destroyed inside
a handler while the emitter is still iterating. Deferred (queue and drain at a
defined point) avoids that entirely at the cost of a frame of latency and harder
debugging. **Deferred is usually the safer default for gameplay**; if immediate is
chosen, iteration must be reentrancy-safe.

**Connections must not store raw pointers**, for the same reason as T0071:
entities die and behaviours are recreated on hot reload. Store target GUID +
handler identity and resolve on emit.

Automatic disconnection is what makes this safe to use. Requiring manual
disconnect in every destructor guarantees a leak or a crash eventually.

**This is deliberately not a global event bus** — see D10. Signals cover authored
1:N relationships with a known partner, where keeping the connection visible and
serializable is the whole point. Broadcast to an unknown audience is the message
bus's job (T0075), and reading another entity's state needs no messaging at all.

The three-way split matters: a bus makes "who listens?" unanswerable from the call
graph, which is an acceptable cost for genuinely broadcast events and an
unnecessary one for a door and its switch.

### From T0062 / D23 (2026-08-05) — connections must auto-disconnect

D23 uses `hp::Signal<T>` as a member of a behaviour (`Door::opened`,
`Door::closed`) with other behaviours connecting to it. **A connection keyed to
an `hp::Behaviour` must be dropped automatically when that behaviour is
destroyed or its module unloads** — no `disconnect` in gameplay code.

This is not convenience. It is the fix for the dangling-callback hazard T0068's
review note already records for action callbacks, and it is sharper here because
T0048 genuinely unloads the module the handler lives in.
