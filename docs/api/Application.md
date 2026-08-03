# `<hp/Application.hpp>`

*Generated from `engine/include/hp/Application.hpp` — do not edit.*

```cpp
#include <hp/Application.hpp>
```

14 public declaration(s), 5 documented.

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

## `createApplication`

```cpp
std::unique_ptr<Application> createApplication(int argc, char ** argv)
```

 Defined by each application, not by the engine.

 Returns the concrete `Application`. `argc`/`argv` are passed through so an
 app can parse its own command line without the engine imposing a scheme.
