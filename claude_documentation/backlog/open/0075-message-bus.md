# T0075 — Message bus with addressing

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 3 — Data model |
| **Order** | 330 |
| **Created** | 2026-08-03 |
| **Refs** | T0100, [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md), T0074, T0077 |

## Why

Some events have **no known audience**: an explosion that should affect whatever
is nearby, an alert that should reach every enemy, a wave-started announcement.
Direct calls and per-entity signals (T0072) both require the publisher to know
who is listening — which is exactly what these cases lack.

The valuable part is **addressing**: publish to one entity, to a tag, to a radius,
or to everything.

This is deliberately **one of three mechanisms**, not the only one — see the
layering note below, and D10 in the decision log.

## Done when

- [ ] Publish to: a single entity, a tag (T0074), a radius, or all
- [ ] Messages are **typed** — no untyped variant payloads
- [ ] Subscribers register by message type, optionally filtered by tag
- [ ] Dispatch is deferred to a defined drain point, avoiding reentrancy
- [ ] Publishing does not allocate on the hot path
- [ ] Subscriptions survive a gameplay hot reload (T0048)
- [ ] Every message is visible in Tracy when profiling is on
- [ ] Dispatch order is deterministic

## Subtasks

- [ ] 75.1 `Target` — Entity / Tag / Radius / All
- [ ] 75.2 Typed publish and subscribe, keyed on message type
- [ ] 75.3 Tag dispatch via the tag index (T0074)
- [ ] 75.4 Radius dispatch via spatial query (T0073)
- [ ] 75.5 Deferred queue with an explicit drain point in the frame
- [ ] 75.6 Pooled message storage — no per-publish allocation
- [ ] 75.7 Subscriptions stored as GUID + type so hot reload survives
- [ ] 75.8 Tracy events per message when profiling (T0029)
- [ ] 75.9 Deterministic ordering — see notes
- [ ] 75.10 A debug view listing recent messages and their subscribers

## Notes / findings


### Frame anatomy — phase 6 — deferred drain (T0100, D17)

The message bus drains at **phase 6**, drain-until-empty with the iteration cap
this ticket already describes. This is what makes the phase-12 assertion
possible, and it is the fix for the in-flight-payload hazard in this ticket's
review note.

The full order is in [../../documentation/08-frame-anatomy.md](../../documentation/08-frame-anatomy.md); the decision and what it rejected is **D17** in the
decision log. If this ticket needs a phase that does not exist, that is a change
to T0100's document and to D17 — not a new call bolted into `Application::run`.

**Keep messages typed.** Untyped payloads (`std::any`, variants) are the usual way
a bus is built and they discard compile-time checking, which is a poor trade in a
C++ engine. `Publish<DamageMsg>(Target::Tag("enemy"), {10})` keeps the checking
and costs nothing.

**Deferred dispatch is the default for a reason**, but it has a real cost worth
stating: a chain of `damage → death → loot → UI` takes one frame per hop. If that
becomes a problem, the fix is draining the queue repeatedly within a frame until
empty (with an iteration cap), *not* switching to immediate dispatch — immediate
reintroduces the reentrancy problem where a handler destroys entities mid-iteration.

**Determinism matters more than it looks.** Once behaviour flows through a bus,
message order *is* semantics. Order by (publish order, subscriber registration
order) and keep it stable, or the same inputs produce different outcomes across
runs — the worst class of bug to chase, and fatal if replay or networking (T0070)
ever matters.

**The known weakness, stated honestly:** a bus makes "who handles this?"
unanswerable from the call graph, which is why it is not the only mechanism. Two
mitigations are built in: 75.10's debug view, and Tracy integration — a message
log is a genuine debugging asset, and partly buys back what the call stack loses.

**Mechanism guidance** (see D10):
- reading state → **direct component access**, no messaging
- authored 1:N with a known partner → **signals** (T0072)
- unknown audience, broadcast, tag or radius → **this bus**

### Architecture review (2026-08-03) — module-defined message types across reload

75.7 covers *subscriptions* surviving reload but not the two harder halves of
the same problem, both consequences of messages being typed:

- **Type identity.** "Keyed on message type" means a type ID. For a message
  struct defined in the gameplay module, a `typeid`/pointer-identity key breaks
  across reload (and across the module boundary entirely — see T0095). The key
  must be a stable name-based ID, registered the same way behaviour types are
  (T0062's `RegisterTypes`), not a raw C++ type identity.
- **In-flight payloads.** A deferred queue can hold messages whose type — and
  destructor code — lives in the module being unloaded. Reload must happen at
  a point where the queue is drained (the natural place: the same
  between-frames point T0048 already needs), and that invariant should be
  asserted, not assumed.

### Note (2026-08-03) -- platform integration would subscribe here

Store-platform integration -- achievements, rich presence -- wants a gameplay
event source, and this bus is exactly that. Nothing has to be decided for that
to stay true: no ticket owns platform integration, and none needs to, because
the rule below preserves the option **for free**. If such integration is ever
built it is an engine capability with its own ticket. The rule:
**platform integration subscribes to gameplay events; gameplay stays ignorant
of the platform.** No gameplay code ever calls a Steamworks-shaped API -- a
platform layer, if one ever exists, listens to the same typed messages
everything else publishes. Recorded so nobody wires an achievement call into a
behaviour "just for now". (Design-gap survey item 10.)

### Cross-ticket obligations (2026-08-04, T0124 backfill)

- **T0077.1** decides whether radius/tag dispatch sees all resident scenes or
  only the active one (its second-pass note names this bus directly). Consume
  that decision; a per-mechanism answer here is what it exists to prevent.
