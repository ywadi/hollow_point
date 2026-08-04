# `<hp/Application.hpp>`

*Generated from `engine/include/hp/Application.hpp` — do not edit.*

```cpp
#include <hp/Application.hpp>
```

19 public declaration(s), 9 documented.

## `ApplicationConfig`

```cpp
struct ApplicationConfig
```

*No documentation comment.*

## `Application`

```cpp
class Application
```

*No documentation comment.*

## `Application::Application`

```cpp
Application(ApplicationConfig config)
```

*No documentation comment.*

## `Application::Application`

```cpp
Application(const Application &)
```

*No documentation comment.*

## `Application::operator=`

```cpp
Application & operator=(const Application &)
```

*No documentation comment.*

## `Application::run`

```cpp
int run()
```

 Runs until something asks to stop. Returns the process exit code.

## `Application::requestExit`

```cpp
void requestExit(int exitCode)
```

 Asks the loop to finish after the current frame. Safe to call from a
 frame hook; the loop is not torn down underneath the caller.

 @param exitCode value `run()` returns, and therefore the process exit
        code. Defaults to 0.

## `Application::dispatch`

```cpp
void dispatch(Event & event)
```

 Sends an event top-down through the layers, then to `onEvent` if nothing
 consumed it. Public so a test or a tool can inject one.

 @param event the event to deliver.

## `Application::config`

```cpp
const ApplicationConfig & config() const
```

*No documentation comment.*

## `Application::clock`

```cpp
Clock & clock()
```

 The game clock. Scaled and pausable -- see T0057. The editor will own a
 second, unscaled clock when it needs one.

## `Application::clock`

```cpp
const Clock & clock() const
```

*No documentation comment.*

## `Application::frame`

```cpp
std::uint64_t frame() const
```

*No documentation comment.*

## `Application::window`

```cpp
Window * window()
```

 Null in a headless run.

## `Application::window`

```cpp
const Window * window() const
```

*No documentation comment.*

## `Application::layers`

```cpp
LayerStack & layers()
```

 The layer stack. Layers are where features live; `Application` stays
 thin on purpose (T0014).

## `Application::layers`

```cpp
const LayerStack & layers() const
```

*No documentation comment.*

## `Application::modules`

```cpp
ModuleHost & modules()
```

 The gameplay modules this application hosts (T0048).

 Load them in `onStartup`. The loop reloads changed ones for you at the
 end-of-frame safe point — frame phase 12 — which is the only moment
 replacing code out from under a running process is safe. That is why
 reloading is wired in here rather than left to the editor: a host that
 polls whenever it likes gets it wrong exactly once, in a way that
 presents as memory corruption.

## `Application::modules`

```cpp
const ModuleHost & modules() const
```

 The gameplay modules this application hosts, read-only.

## `createApplication`

```cpp
std::unique_ptr<Application> createApplication(int argc, char ** argv)
```

 Defined by each application, not by the engine.

 @param argc argument count, straight from `main`.
 @param argv argument vector, straight from `main`. Passed through so an app
        can parse its own command line without the engine imposing a scheme.
 @returns the concrete application. Returning null makes the process exit
        with code 1 without running a frame.
