# `<hp/Log.hpp>`

*Generated from `engine/include/hp/Log.hpp` — do not edit.*

```cpp
#include <hp/Log.hpp>
```

22 public declaration(s), 12 documented.

## `LogLevel`

```cpp
enum class LogLevel
```

| Enumerator | Value |
|---|---|
| `Trace` | 0 |
| `Debug` | 1 |
| `Info` | 2 |
| `Warning` | 3 |
| `Error` | 4 |
| `Fatal` | 5 |
| `Off` | 6 |

*No documentation comment.*

## `logLevelName`

```cpp
std::string_view logLevelName(LogLevel level)
```

*No documentation comment.*

## `LogCategory`

```cpp
class LogCategory
```

 A subsystem's log channel.

 Deliberately a *handle*, not an object holding its own state. The engine owns
 the name and the level; this is a 16-bit id into that registry. Constructing
 one with a name that already exists returns the same id, so a category can be
 declared independently in several translation units -- or in a gameplay
 module -- without duplicating state.

 That matters for hot reload (T0048). If a category owned its own storage and
 the engine held a pointer to it, unloading the module that declared it would
 leave the engine holding a dangling pointer. An id into engine-owned storage
 cannot dangle: the worst case after an unload is a category name nobody logs
 to any more.

## `LogCategory::LogCategory`

```cpp
LogCategory(std::string_view name)
```

 Registers `name`, or returns the existing id if already registered.

 @param name the channel name, e.g. "render" or "game.combat". Two
        declarations of the same name -- in different translation units,
        or in a gameplay module -- resolve to one channel.

## `LogCategory::name`

```cpp
std::string_view name() const
```

*No documentation comment.*

## `LogCategory::level`

```cpp
LogLevel level() const
```

*No documentation comment.*

## `LogCategory::setLevel`

```cpp
void setLevel(LogLevel level) const
```

*No documentation comment.*

## `LogCategory::id`

```cpp
std::uint16_t id() const
```

*No documentation comment.*

## `LogRecord`

```cpp
struct LogRecord
```

 One log line, as handed to every sink.

 All views point at storage owned by the caller and valid only for the
 duration of the `write` call. A sink that needs to keep a record -- the
 editor console does -- must copy it. Stated here because the alternative
 (allocating a string per line for sinks that do not need one) is a cost paid
 on every log call for the benefit of one sink.

## `ILogSink`

```cpp
class ILogSink
```

 A destination for log records.

 Implementations must be thread-safe: the engine holds a lock while
 dispatching, but a sink that also writes from elsewhere is on its own.

## `ILogSink::write`

```cpp
void write(const LogRecord & record)
```

*No documentation comment.*

## `ILogSink::flush`

```cpp
void flush()
```

*No documentation comment.*

## `logAddSink`

```cpp
void logAddSink(ILogSink * sink)
```

 Registers a sink. **Non-owning** -- the caller keeps ownership and must call
 `logRemoveSink` before destroying it.

 Non-owning on purpose. Passing a `unique_ptr` across the module boundary
 would mean the engine freeing memory a gameplay module allocated, which the
 conventions forbid: each library carries its own statically linked libc++
 (see 06-engine-conventions.md, "the module boundary").
 @param sink destination to add. Ignored if null, and registering the same
        sink twice does not duplicate delivery.

## `logRemoveSink`

```cpp
void logRemoveSink(ILogSink * sink)
```

*No documentation comment.*

## `logAddConsoleSink`

```cpp
void logAddConsoleSink()
```

 Built-in sinks, created and owned by the engine so no allocation crosses the
 boundary. Idempotent; calling twice does not duplicate output.

## `logAddFileSink`

```cpp
bool logAddFileSink(const char * path)
```

*No documentation comment.*

## `logFlush`

```cpp
void logFlush()
```

 Flushes every sink. Call before aborting -- a crash with the last twenty
 lines still in a buffer is a bug report with the interesting part missing.

## `logSetGlobalLevel`

```cpp
void logSetGlobalLevel(LogLevel level)
```

 Sets the level on every registered category at once.

 @param level the new minimum for every category. Blunt by design -- it is
        for "quiet everything" rather than for tuning.

## `logCategoryCount`

```cpp
std::uint16_t logCategoryCount()
```

 Enumerates categories, for an editor filter UI (T0066).

 @returns the number of registered categories. Only ever grows.

## `logCategoryAt`

```cpp
LogCategory logCategoryAt(std::uint16_t index)
```

 @param index a value in `[0, logCategoryCount())`.
 @returns the category at `index`. Out-of-range returns a category named "?".

## `logWrite`

```cpp
void logWrite(const LogCategory & category, LogLevel level, std::string_view file, int line, std::string_view message)
```

 The formatted-message entry point. Prefer the macros, which check the level
 before formatting.

 @param category channel the record belongs to.
 @param level severity of this record.
 @param file source file, normally `__FILE__`.
 @param line source line, normally `__LINE__`.
 @param message the already-formatted text. Not copied -- sinks that keep a
        record must copy it, because the view dies with this call.

## `logEnabled`

```cpp
bool logEnabled(const LogCategory & category, LogLevel level)
```

 True when a record at `level` in `category` would reach at least one sink.
 The macros check this *before* formatting, because formatting is the
 expensive part and a filtered-out line should cost a comparison.

 @param category channel to test.
 @param level severity to test.
 @returns true when a record at `level` would be delivered.
