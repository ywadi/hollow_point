# `<hp/Window.hpp>`

*Generated from `engine/include/hp/Window.hpp` — do not edit.*

```cpp
#include <hp/Window.hpp>
```

12 public declaration(s), 4 documented.

## `WindowConfig`

```cpp
struct WindowConfig
```

*No documentation comment.*

## `NativeWindowHandles`

```cpp
struct NativeWindowHandles
```

 Native handles, in the shape Diligent's `NativeWindow` structs want.

 Deliberately opaque `void*`/`uint64_t` rather than including any platform
 header: this header is included by engine code that has no business seeing
 `Xlib.h` or `windows.h`, and `windows.h` in particular poisons a translation
 unit with macros like `min`, `max` and `CreateWindow`.

## `WindowEvents`

```cpp
struct WindowEvents
```

 What happened since the last pump. Deliberately a small value type rather
 than a callback: T0018 owns the real event system, and a callback API here
 would be the wrong thing to migrate away from later.

## `Window`

```cpp
class Window
```

*No documentation comment.*

## `Window::create`

```cpp
static std::unique_ptr<Window> create(const WindowConfig & config)
```

 @param config size, title and resizability of the window to open.
 @returns the window, or null on failure -- having already logged why, so
        a caller that only needs to abort does not have to ask.

## `Window::Window`

```cpp
Window(const Window &)
```

*No documentation comment.*

## `Window::operator=`

```cpp
Window & operator=(const Window &)
```

*No documentation comment.*

## `Window::pumpEvents`

```cpp
WindowEvents pumpEvents(const std::function<void (Event &)> & onEvent)
```

 Drains the platform event queue. Call once per frame, before update.

 @param onEvent called for each input event as it is translated, so it can
        be dispatched immediately. Passed as a callback rather than
        returned as a queue because an event is a message passing
        through, not an object with a lifetime -- queueing would mean
        heap-allocating polymorphic events every frame to keep them alive
        past this call, for no benefit.
 @returns close and resize state, which the loop acts on directly.

## `Window::width`

```cpp
int width() const
```

*No documentation comment.*

## `Window::height`

```cpp
int height() const
```

*No documentation comment.*

## `Window::title`

```cpp
const std::string & title() const
```

*No documentation comment.*

## `Window::nativeHandles`

```cpp
NativeWindowHandles nativeHandles() const
```

*No documentation comment.*
