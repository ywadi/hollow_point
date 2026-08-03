// Logging (T0054).
//
// Diligent provides only `DebugOutput.h` -- no levels, categories, sinks or file
// output -- and retrofitting logging across an existing codebase is grim, so it
// lands with the skeleton.
//
// Three ideas, and the interaction between them is the whole design:
//
//   Levels      trace..fatal, with a *compile-time* floor so verbose logging is
//               absent from a shipped build rather than merely skipped.
//   Categories  per subsystem, filterable independently at run time. With one
//               category the only realistic filter is "off", which means it gets
//               turned off, which means the logging was pointless.
//   Sinks       console, file, the editor console panel (T0066), Tracy messages
//               (T0029). One stream, many destinations.
//
// Formatting is `std::format` -- type-safe, standard, and verified working on
// both targets under the pinned toolchain (see T0054's evidence).
#pragma once

#include <hp/Api.hpp>

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace hp {

enum class LogLevel : std::uint8_t {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warning = 3,
    Error = 4,
    Fatal = 5,
    Off = 6,
};

HP_API std::string_view logLevelName(LogLevel level);

/// A subsystem's log channel.
///
/// Deliberately a *handle*, not an object holding its own state. The engine owns
/// the name and the level; this is a 16-bit id into that registry. Constructing
/// one with a name that already exists returns the same id, so a category can be
/// declared independently in several translation units -- or in a gameplay
/// module -- without duplicating state.
///
/// That matters for hot reload (T0048). If a category owned its own storage and
/// the engine held a pointer to it, unloading the module that declared it would
/// leave the engine holding a dangling pointer. An id into engine-owned storage
/// cannot dangle: the worst case after an unload is a category name nobody logs
/// to any more.
class HP_API LogCategory {
public:
    /// Registers `name`, or returns the existing id if already registered.
    ///
    /// @param name the channel name, e.g. "render" or "game.combat". Two
    ///        declarations of the same name -- in different translation units,
    ///        or in a gameplay module -- resolve to one channel.
    explicit LogCategory(std::string_view name);

    std::string_view name() const;
    LogLevel level() const;
    void setLevel(LogLevel level) const;

    std::uint16_t id() const { return id_; }

private:
    std::uint16_t id_;
};

/// One log line, as handed to every sink.
///
/// All views point at storage owned by the caller and valid only for the
/// duration of the `write` call. A sink that needs to keep a record -- the
/// editor console does -- must copy it. Stated here because the alternative
/// (allocating a string per line for sinks that do not need one) is a cost paid
/// on every log call for the benefit of one sink.
struct LogRecord {
    LogLevel level;
    std::string_view category;
    std::string_view message;
    std::string_view file;
    int line;
    std::uint64_t threadId;
};

/// A destination for log records.
///
/// Implementations must be thread-safe: the engine holds a lock while
/// dispatching, but a sink that also writes from elsewhere is on its own.
class HP_API ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void write(const LogRecord& record) = 0;

    virtual void flush() {}
};

/// Registers a sink. **Non-owning** -- the caller keeps ownership and must call
/// `logRemoveSink` before destroying it.
///
/// Non-owning on purpose. Passing a `unique_ptr` across the module boundary
/// would mean the engine freeing memory a gameplay module allocated, which the
/// conventions forbid: each library carries its own statically linked libc++
/// (see 06-engine-conventions.md, "the module boundary").
/// @param sink destination to add. Ignored if null, and registering the same
///        sink twice does not duplicate delivery.
HP_API void logAddSink(ILogSink* sink);
HP_API void logRemoveSink(ILogSink* sink);

/// Built-in sinks, created and owned by the engine so no allocation crosses the
/// boundary. Idempotent; calling twice does not duplicate output.
HP_API void logAddConsoleSink();
HP_API bool logAddFileSink(const char* path);

/// Flushes every sink. Call before aborting -- a crash with the last twenty
/// lines still in a buffer is a bug report with the interesting part missing.
HP_API void logFlush();

/// Sets the level on every registered category at once.
///
/// @param level the new minimum for every category. Blunt by design -- it is
///        for "quiet everything" rather than for tuning.
HP_API void logSetGlobalLevel(LogLevel level);

/// Enumerates categories, for an editor filter UI (T0066).
///
/// @returns the number of registered categories. Only ever grows.
HP_API std::uint16_t logCategoryCount();

/// @param index a value in `[0, logCategoryCount())`.
/// @returns the category at `index`. Out-of-range returns a category named "?".
HP_API LogCategory logCategoryAt(std::uint16_t index);

/// The formatted-message entry point. Prefer the macros, which check the level
/// before formatting.
///
/// @param category channel the record belongs to.
/// @param level severity of this record.
/// @param file source file, normally `__FILE__`.
/// @param line source line, normally `__LINE__`.
/// @param message the already-formatted text. Not copied -- sinks that keep a
///        record must copy it, because the view dies with this call.
HP_API void logWrite(const LogCategory& category, LogLevel level, std::string_view file, int line,
                     std::string_view message);

/// True when a record at `level` in `category` would reach at least one sink.
/// The macros check this *before* formatting, because formatting is the
/// expensive part and a filtered-out line should cost a comparison.
///
/// @param category channel to test.
/// @param level severity to test.
/// @returns true when a record at `level` would be delivered.
HP_API bool logEnabled(const LogCategory& category, LogLevel level);

} // namespace hp

// --- compile-time floor ------------------------------------------------------
//
// Levels below HP_LOG_MIN_LEVEL are removed by the preprocessor: no branch, no
// symbol, and -- as with the profiling macros (T0019) -- **no evaluation of the
// arguments**. `HP_LOG_TRACE(cat, "{}", expensiveDescription())` must not call
// expensiveDescription() in a build where trace is compiled out. That is the
// difference between a log statement being absent and being skipped.
#ifndef HP_LOG_MIN_LEVEL
#define HP_LOG_MIN_LEVEL 0
#endif

#define HP_LOG_IMPL(category, lvl, ...)                                                            \
    do {                                                                                           \
        if (::hp::logEnabled((category), (lvl))) {                                                 \
            ::hp::logWrite((category), (lvl), __FILE__, __LINE__, std::format(__VA_ARGS__));       \
        }                                                                                          \
    } while (false)

#if HP_LOG_MIN_LEVEL <= 0
#define HP_LOG_TRACE(category, ...) HP_LOG_IMPL(category, ::hp::LogLevel::Trace, __VA_ARGS__)
#else
#define HP_LOG_TRACE(category, ...)
#endif

#if HP_LOG_MIN_LEVEL <= 1
#define HP_LOG_DEBUG(category, ...) HP_LOG_IMPL(category, ::hp::LogLevel::Debug, __VA_ARGS__)
#else
#define HP_LOG_DEBUG(category, ...)
#endif

#if HP_LOG_MIN_LEVEL <= 2
#define HP_LOG_INFO(category, ...) HP_LOG_IMPL(category, ::hp::LogLevel::Info, __VA_ARGS__)
#else
#define HP_LOG_INFO(category, ...)
#endif

#if HP_LOG_MIN_LEVEL <= 3
#define HP_LOG_WARN(category, ...) HP_LOG_IMPL(category, ::hp::LogLevel::Warning, __VA_ARGS__)
#else
#define HP_LOG_WARN(category, ...)
#endif

// Error and fatal are never compiled out. A build that cannot report an error
// is not a build anyone can support.
#define HP_LOG_ERROR(category, ...) HP_LOG_IMPL(category, ::hp::LogLevel::Error, __VA_ARGS__)
#define HP_LOG_FATAL(category, ...) HP_LOG_IMPL(category, ::hp::LogLevel::Fatal, __VA_ARGS__)
