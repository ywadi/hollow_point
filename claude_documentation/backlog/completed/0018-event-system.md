# T0018 — Event system

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 2 — Engine skeleton |
| **Order** | 100 |
| **Created** | 2026-08-02 |

## Why

Layers and panels need to talk to each other without knowing about each other.
The concrete driver is the editor viewport: it has to receive the freshly
rendered texture from the render layer each frame, and neither should hold a
pointer to the other.

This has to exist before anything that depends on it — which is most of Phases
4-6 — so it is built with the skeleton rather than retrofitted.

## Done when

- [x] `Event` base with `consume()` / `isConsumed()` — named to the conventions' camelCase rather than the ticket's original spelling
- [x] Layers receive `onEvent`; propagation stops at the first consumer
- [x] `dispatchEvent<T>` casts safely and consumes on a true handler; layers get `Event&` and no handle on the stack, so they cannot reorder it
- [x] Input events reach layers from the window — key, text, mouse button, motion, wheel and focus, translated from SDL
- [x] Tests cover propagation order and consumption

## Subtasks

- [x] 18.1 `IEvent` base and a type discriminator (see notes on the approach)
- [x] 18.2 Event categories: key, mouse button, mouse move, scroll, window resize
- [x] 18.3 `OnEvent` on `ILayer`; dispatch top-down through the stack
- [x] 18.4 Dispatcher handle with send-only privileges, handed to layers
- [x] 18.5 Feed window/input events in from T0015
- [x] 18.6 Tests: a consumed event does not reach lower layers

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


## Findings

**Update goes bottom-up, events go top-down**, and the asymmetry is the design
rather than an accident. The world should simulate before the interface drawn
over it; the topmost thing on screen should get first refusal on a click. A
stack that walked both directions the same way would be wrong in one of them.
Both orderings are asserted separately.

**Not a callback/subscription system, deliberately.** A subscriber list has no
inherent order, so "who sees this first" becomes incidental — and a subscriber
is a pointer into a library that can unload (T0048). A layer stack has order by
construction and is walked rather than stored, so a module that goes away takes
its layer with it.

**`type()` is an enum, but `dispatchEvent` uses `dynamic_cast`.** Not a
contradiction: the enum is the *filter*, and events are the highest-frequency
thing in the engine after the frame itself. The cast is a cast, and RTTI across
the module boundary was measured working on both targets (T0095), so a checked
cast beats a `static_cast` in a switch arm that silently does not.

**Events are stack objects with no lifetime.** `pumpEvents` takes a callback
rather than returning a queue, because queueing polymorphic events would mean
heap-allocating them every frame purely to outlive the call.

**Physical scancodes, not layout-mapped keycodes.** A binding on "W" must stay
where W sits on a US layout even on AZERTY. Text entry is a separate event
because "what character did the user type" and "which key went down" are
different questions, and deriving either from the other is wrong.

**Close is dispatched before it is obeyed.** A layer that wants "really quit?"
consumes the `WindowCloseEvent` and asks; unconsumed means nobody objected.
That fell out of the consumption model rather than needing a mechanism.

**One thing I got wrong and caught before it compiled:** the first
`dispatchEvent` was a stub that returned false and required default-constructible
events. It would have silently dispatched nothing.

## Not done

**Non-ASCII text input is dropped.** `SDL_EVENT_TEXT_INPUT` hands over UTF-8 and
only ASCII code points are emitted; the rest are skipped rather than mis-decoded.
Proper decoding belongs with T0117 (fonts and text), and guessing at it now
would be a second implementation to remove later.

**`KeyCode` is partial** — letters, digits, function keys and the common
navigation keys. It is filled in as T0068's input mapping needs it; the enum
exists now so the event shape is settled and call sites do not change later.

**No gamepad events.** SDL provides them and D16 chose SDL partly for that, but
the action-mapping layer that gives them meaning is T0068.

## Evidence

```
$ zig build test -Dtest=all
[doctest] test cases:  27 |  27 passed | 0 failed | 0 skipped
[doctest] assertions: 114 | 114 passed | 0 failed |     (x2 -- both targets)
```

Cases: update bottom-up versus events top-down, a consumed event not reaching
lower layers, overlays staying above later pushes, attach/detach ordering,
`dispatchEvent` type safety, and the application receiving only what layers left
unconsumed. Also verified headless (`env -u DISPLAY`), which is the CI condition.
