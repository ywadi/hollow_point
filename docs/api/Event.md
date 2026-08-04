# `<hp/Event.hpp>`

*Generated from `engine/include/hp/Event.hpp` — do not edit.*

```cpp
#include <hp/Event.hpp>
```

72 public declaration(s), 15 documented.

## `EventType`

```cpp
enum class EventType
```

| Enumerator | Value |
|---|---|
| `None` | 0 |
| `WindowClose` | 1 |
| `WindowResize` | 2 |
| `WindowFocusGained` | 3 |
| `WindowFocusLost` | 4 |
| `KeyPressed` | 5 |
| `KeyReleased` | 6 |
| `TextInput` | 7 |
| `MouseButtonPressed` | 8 |
| `MouseButtonReleased` | 9 |
| `MouseMoved` | 10 |
| `MouseScrolled` | 11 |

*No documentation comment.*

## `EventCategory`

```cpp
enum class EventCategory
```

| Enumerator | Value |
|---|---|
| `None` | 0 |
| `Window` | 1 |
| `Input` | 2 |
| `Keyboard` | 4 |
| `Mouse` | 8 |

 Broad grouping, so a layer can say "I care about input" without listing
 every type. A bitmask because an event can belong to several -- a key press
 is both `Keyboard` and `Input`.

## `operator|`

```cpp
EventCategory operator|(EventCategory a, EventCategory b)
```

*No documentation comment.*

## `hasCategory`

```cpp
bool hasCategory(EventCategory value, EventCategory wanted)
```

*No documentation comment.*

## `Event`

```cpp
class Event
```

 Base of every event.

 Events live on the stack for the duration of a dispatch and are never stored
 -- a layer that needs one later must copy what it needs. That is why there is
 no clone, no allocation and no ownership question: an event is a message
 passing through, not an object with a lifetime.

## `Event::type`

```cpp
EventType type() const
```

*No documentation comment.*

## `Event::categories`

```cpp
EventCategory categories() const
```

*No documentation comment.*

## `Event::name`

```cpp
std::string_view name() const
```

 Name for logging and debugging. Not a stable identifier -- do not switch
 on it, that is what `type()` is for.

## `Event::consume`

```cpp
void consume()
```

 Marks the event handled. Layers below this one will not see it.

## `Event::isConsumed`

```cpp
bool isConsumed() const
```

*No documentation comment.*

## `Event::isIn`

```cpp
bool isIn(EventCategory category) const
```

*No documentation comment.*

## `WindowCloseEvent`

```cpp
class WindowCloseEvent
```

*No documentation comment.*

## `WindowCloseEvent::type`

```cpp
EventType type() const
```

*No documentation comment.*

## `WindowCloseEvent::categories`

```cpp
EventCategory categories() const
```

*No documentation comment.*

## `WindowCloseEvent::name`

```cpp
std::string_view name() const
```

*No documentation comment.*

## `WindowResizeEvent`

```cpp
class WindowResizeEvent
```

*No documentation comment.*

## `WindowResizeEvent::WindowResizeEvent`

```cpp
WindowResizeEvent(int width, int height)
```

 @param width new width in **pixels**.
 @param height new height in **pixels**. Pixels rather than logical units
        because a swap chain is sized in pixels, and the two differ on a
        scaled display.

## `WindowResizeEvent::width`

```cpp
int width() const
```

*No documentation comment.*

## `WindowResizeEvent::height`

```cpp
int height() const
```

*No documentation comment.*

## `WindowResizeEvent::type`

```cpp
EventType type() const
```

*No documentation comment.*

## `WindowResizeEvent::categories`

```cpp
EventCategory categories() const
```

*No documentation comment.*

## `WindowResizeEvent::name`

```cpp
std::string_view name() const
```

*No documentation comment.*

## `WindowFocusEvent`

```cpp
class WindowFocusEvent
```

*No documentation comment.*

## `WindowFocusEvent::WindowFocusEvent`

```cpp
WindowFocusEvent(bool gained)
```

 @param gained true when the window took focus, false when it lost it.

## `WindowFocusEvent::gained`

```cpp
bool gained() const
```

*No documentation comment.*

## `WindowFocusEvent::type`

```cpp
EventType type() const
```

*No documentation comment.*

## `WindowFocusEvent::categories`

```cpp
EventCategory categories() const
```

*No documentation comment.*

## `WindowFocusEvent::name`

```cpp
std::string_view name() const
```

*No documentation comment.*

## `KeyCode`

```cpp
enum class KeyCode
```

| Enumerator | Value |
|---|---|
| `Unknown` | 0 |
| `Escape` | 1 |
| `Space` | 2 |
| `Enter` | 3 |
| `Tab` | 4 |
| `Backspace` | 5 |
| `Left` | 6 |
| `Right` | 7 |
| `Up` | 8 |
| `Down` | 9 |
| `A` | 10 |
| `B` | 11 |
| `C` | 12 |
| `D` | 13 |
| `E` | 14 |
| `F` | 15 |
| `G` | 16 |
| `H` | 17 |
| `I` | 18 |
| `J` | 19 |
| `K` | 20 |
| `L` | 21 |
| `M` | 22 |
| `N` | 23 |
| `O` | 24 |
| `P` | 25 |
| `Q` | 26 |
| `R` | 27 |
| `S` | 28 |
| `T` | 29 |
| `U` | 30 |
| `V` | 31 |
| `W` | 32 |
| `X` | 33 |
| `Y` | 34 |
| `Z` | 35 |
| `Num0` | 36 |
| `Num1` | 37 |
| `Num2` | 38 |
| `Num3` | 39 |
| `Num4` | 40 |
| `Num5` | 41 |
| `Num6` | 42 |
| `Num7` | 43 |
| `Num8` | 44 |
| `Num9` | 45 |
| `F1` | 46 |
| `F2` | 47 |
| `F3` | 48 |
| `F4` | 49 |
| `F5` | 50 |
| `F6` | 51 |
| `F7` | 52 |
| `F8` | 53 |
| `F9` | 54 |
| `F10` | 55 |
| `F11` | 56 |
| `F12` | 57 |

 Physical key, layout-independent.

 Deliberately not a character: a game binding "W" means the key where W sits
 on a US layout, and must stay there on AZERTY. Text entry is a separate
 event (`TextInputEvent`) precisely because the two are different questions.

## `KeyModifiers`

```cpp
struct KeyModifiers
```

 Chording state at the moment of the event.

## `KeyEvent`

```cpp
class KeyEvent
```

*No documentation comment.*

## `KeyEvent::KeyEvent`

```cpp
KeyEvent(KeyCode key, bool pressed, KeyModifiers mods, bool repeat)
```

 @param key the physical key.
 @param pressed true for a press, false for a release.
 @param mods modifier state at the time.
 @param repeat true when this is an auto-repeat rather than a fresh press.
        Text fields want repeats; a jump button does not, and a system
        that cannot tell them apart makes the character jump continuously.

## `KeyEvent::key`

```cpp
KeyCode key() const
```

*No documentation comment.*

## `KeyEvent::modifiers`

```cpp
KeyModifiers modifiers() const
```

*No documentation comment.*

## `KeyEvent::pressed`

```cpp
bool pressed() const
```

*No documentation comment.*

## `KeyEvent::isRepeat`

```cpp
bool isRepeat() const
```

*No documentation comment.*

## `KeyEvent::type`

```cpp
EventType type() const
```

*No documentation comment.*

## `KeyEvent::categories`

```cpp
EventCategory categories() const
```

*No documentation comment.*

## `KeyEvent::name`

```cpp
std::string_view name() const
```

*No documentation comment.*

## `TextInputEvent`

```cpp
class TextInputEvent
```

 A typed character, already through the platform's input method.

 Separate from `KeyEvent` because they answer different questions: a text
 field wants "what character did the user type", honouring layout, dead keys
 and IME composition; a game binding wants "which physical key went down".
 Deriving one from the other is wrong in both directions.

## `TextInputEvent::TextInputEvent`

```cpp
TextInputEvent(std::uint32_t codepoint)
```

 @param codepoint the Unicode code point entered.

## `TextInputEvent::codepoint`

```cpp
std::uint32_t codepoint() const
```

*No documentation comment.*

## `TextInputEvent::type`

```cpp
EventType type() const
```

*No documentation comment.*

## `TextInputEvent::categories`

```cpp
EventCategory categories() const
```

*No documentation comment.*

## `TextInputEvent::name`

```cpp
std::string_view name() const
```

*No documentation comment.*

## `MouseButton`

```cpp
enum class MouseButton
```

| Enumerator | Value |
|---|---|
| `Unknown` | 0 |
| `Left` | 1 |
| `Right` | 2 |
| `Middle` | 3 |
| `X1` | 4 |
| `X2` | 5 |

*No documentation comment.*

## `MouseButtonEvent`

```cpp
class MouseButtonEvent
```

*No documentation comment.*

## `MouseButtonEvent::MouseButtonEvent`

```cpp
MouseButtonEvent(MouseButton button, bool pressed, float x, float y)
```

 @param button which button.
 @param pressed true for a press, false for a release.
 @param x cursor x in pixels, relative to the window.
 @param y cursor y in pixels, relative to the window.

## `MouseButtonEvent::button`

```cpp
MouseButton button() const
```

*No documentation comment.*

## `MouseButtonEvent::pressed`

```cpp
bool pressed() const
```

*No documentation comment.*

## `MouseButtonEvent::x`

```cpp
float x() const
```

*No documentation comment.*

## `MouseButtonEvent::y`

```cpp
float y() const
```

*No documentation comment.*

## `MouseButtonEvent::type`

```cpp
EventType type() const
```

*No documentation comment.*

## `MouseButtonEvent::categories`

```cpp
EventCategory categories() const
```

*No documentation comment.*

## `MouseButtonEvent::name`

```cpp
std::string_view name() const
```

*No documentation comment.*

## `MouseMovedEvent`

```cpp
class MouseMovedEvent
```

*No documentation comment.*

## `MouseMovedEvent::MouseMovedEvent`

```cpp
MouseMovedEvent(float x, float y, float deltaX, float deltaY)
```

 @param x absolute cursor x in pixels, relative to the window.
 @param y absolute cursor y in pixels.
 @param deltaX movement since the last event, in pixels.
 @param deltaY movement since the last event, in pixels. Carried
        alongside the absolute position because a captured cursor
        (T0068's relative mode) has deltas but no meaningful position,
        and a camera wants the delta even when the cursor is free.

## `MouseMovedEvent::x`

```cpp
float x() const
```

*No documentation comment.*

## `MouseMovedEvent::y`

```cpp
float y() const
```

*No documentation comment.*

## `MouseMovedEvent::deltaX`

```cpp
float deltaX() const
```

*No documentation comment.*

## `MouseMovedEvent::deltaY`

```cpp
float deltaY() const
```

*No documentation comment.*

## `MouseMovedEvent::type`

```cpp
EventType type() const
```

*No documentation comment.*

## `MouseMovedEvent::categories`

```cpp
EventCategory categories() const
```

*No documentation comment.*

## `MouseMovedEvent::name`

```cpp
std::string_view name() const
```

*No documentation comment.*

## `MouseScrolledEvent`

```cpp
class MouseScrolledEvent
```

*No documentation comment.*

## `MouseScrolledEvent::MouseScrolledEvent`

```cpp
MouseScrolledEvent(float offsetX, float offsetY)
```

 @param offsetX horizontal scroll, in wheel steps.
 @param offsetY vertical scroll, in wheel steps. Fractional on trackpads,
        which is why these are not integers.

## `MouseScrolledEvent::offsetX`

```cpp
float offsetX() const
```

*No documentation comment.*

## `MouseScrolledEvent::offsetY`

```cpp
float offsetY() const
```

*No documentation comment.*

## `MouseScrolledEvent::type`

```cpp
EventType type() const
```

*No documentation comment.*

## `MouseScrolledEvent::categories`

```cpp
EventCategory categories() const
```

*No documentation comment.*

## `MouseScrolledEvent::name`

```cpp
std::string_view name() const
```

*No documentation comment.*

## `dispatchEvent`

```cpp
bool dispatchEvent(Event & event, Handler && handler)
```

 Calls `handler` if `event` is of type `T`, and consumes it when the handler
 returns true.

 The alternative -- a switch on `type()` with a static_cast in each arm -- is
 where the casts go wrong silently. This makes the type and the cast agree by
 construction.

     dispatchEvent<KeyEvent>(event, [](const KeyEvent& e) {
         return e.key() == KeyCode::Escape;   // true consumes it
     });

 @param event the event to test; consumed in place when the handler returns
        true, so a consumed event stops descending the layer stack.
 @param handler invoked with `const T&` only when the event is of type `T`.
        Returning true consumes the event; false leaves it for other layers.
 @returns whether the handler ran, which is not the same as whether the event
          was consumed -- a handler that returns false still ran.
