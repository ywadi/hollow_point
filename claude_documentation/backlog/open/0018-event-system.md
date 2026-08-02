# T0018 — Event system

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Phase** | 2 — Engine skeleton |
| **Created** | 2026-08-02 |

## Why

Layers and panels need to talk to each other without knowing about each other.
The concrete driver is the editor viewport: it has to receive the freshly
rendered texture from the render layer each frame, and neither should hold a
pointer to the other.

This has to exist before anything that depends on it — which is most of Phases
4-6 — so it is built with the skeleton rather than retrofitted.

## Done when

- [ ] `IEvent` base with `Consume()` / `IsConsumed()`
- [ ] Layers receive `OnEvent`; propagation stops at the first consumer
- [ ] A dispatcher scoped so layers can *send* but not reorder the stack
- [ ] Input events reach layers from the window (T0015)
- [ ] Tests cover propagation order and consumption

## Subtasks

- [ ] 18.1 `IEvent` base and a type discriminator (see notes on the approach)
- [ ] 18.2 Event categories: key, mouse button, mouse move, scroll, window resize
- [ ] 18.3 `OnEvent` on `ILayer`; dispatch top-down through the stack
- [ ] 18.4 Dispatcher handle with send-only privileges, handed to layers
- [ ] 18.5 Feed window/input events in from T0015
- [ ] 18.6 Tests: a consumed event does not reach lower layers

## Notes / findings

**Top-down propagation with consumption is the important part.** It is what lets
the editor UI take a click before the game world does, and what lets an ImGui
panel with keyboard focus swallow key events. Get this wrong and input handling
becomes a pile of special cases later.

Avoid a heap allocation per event on the input path — events fire every frame at
input rate. A tagged union or a small polymorphic type with stack storage is
worth the small extra design effort here.

The custom "new frame rendered" event (T0028) is sent only by the render layer
and is what the viewport panel listens for. That is the first real consumer, so
design the API against that use case rather than in the abstract.
