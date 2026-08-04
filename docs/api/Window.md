# `<hp/Window.hpp>`

*Generated from `engine/include/hp/Window.hpp` — do not edit.*

```cpp
#include <hp/Window.hpp>
```

19 public declaration(s), 11 documented.

## `DisplayMode`

```cpp
enum class DisplayMode
```

| Enumerator | Value |
|---|---|
| `Windowed` | 0 |
| `BorderlessFullscreen` | 1 |

 How a window occupies the screen (T0129).

 **Exclusive fullscreen is deliberately absent**, and the reason is recorded
 rather than left as an omission: it changes the *display's* state rather
 than the window's, which is what makes alt-tab, multi-monitor and
 crash-recovery harder with it. What it buys — a true immediate present path
 on some drivers — is a latency optimisation nobody here has measured a need
 for. See T0129 for what evidence would reopen it.

## `DisplayInfo`

```cpp
struct DisplayInfo
```

 A display the window could be placed on.

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

## `Window::displayMode`

```cpp
DisplayMode displayMode() const
```

 @returns the current display mode.

## `Window::setDisplayMode`

```cpp
bool setDisplayMode(DisplayMode mode)
```

 Switches between windowed and borderless fullscreen at run time (129.2).

 The swap chain follows automatically: SDL emits a resize, which reaches
 the render layer through the normal event path (25.3). Nothing here
 touches the device.

 @param mode the mode to switch to. Setting the current mode is a no-op.
 @returns whether the switch succeeded; on failure the previous mode is
          retained and the reason is logged.

## `Window::setSize`

```cpp
bool setSize(int width, int height)
```

 Changes the windowed size at run time (129.3).

 Ignored while in fullscreen, where the size is the display's — silently,
 because a settings UI applying a saved resolution before restoring
 windowed mode is normal rather than an error.

 @param width new width in logical units.
 @param height new height in logical units.
 @returns whether the size was applied.

## `Window::displayScale`

```cpp
float displayScale() const
```

 @returns the content scale of the display this window is on — 1.0 at
          100%, 2.0 on a doubled display. This is why `width()` (logical)
          and the swap chain's size (pixels) are not the same number.

## `Window::displays`

```cpp
static std::vector<DisplayInfo> displays()
```

 @returns every display currently attached, in SDL's order. The first is
          the primary. Empty only if the video subsystem is unavailable.
