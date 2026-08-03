#include <hp/Log.hpp>

#include <hp/Profiling.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>

namespace hp {
namespace {

struct CategoryState {
    std::string name;
    std::atomic<LogLevel> level{LogLevel::Info};
};

/// All logging state, in one place and owned by the engine.
///
/// A function-local static rather than a namespace-scope one: this is
/// constructed on first use, which avoids the static initialisation order
/// problem when something logs from another static's constructor. Logging is
/// exactly the kind of thing that gets called from surprising places during
/// startup.
struct LogState {
    // Categories are append-only and never removed. Their storage must outlive
    // any module that registered one, which is why LogCategory is an id rather
    // than a pointer -- see the header.
    std::shared_mutex categoriesMutex;
    std::vector<std::unique_ptr<CategoryState>> categories;

    std::shared_mutex sinksMutex;
    std::vector<ILogSink*> sinks;

    // Owned built-in sinks, so nothing allocated here crosses the boundary.
    std::mutex builtinsMutex;
    std::vector<std::unique_ptr<ILogSink>> builtins;
};

LogState& state() {
    static LogState s;
    return s;
}

std::uint64_t currentThreadId() {
    return static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

/// Console sink. Warnings and worse go to stderr so they survive a pipe that
/// only captures stdout, which is how most CI logs are read.
class ConsoleSink final : public ILogSink {
public:
    void write(const LogRecord& record) override {
        std::FILE* out = record.level >= LogLevel::Warning ? stderr : stdout;
        std::fprintf(
            out, "[%-5.*s] %.*s: %.*s\n", static_cast<int>(logLevelName(record.level).size()),
            logLevelName(record.level).data(), static_cast<int>(record.category.size()),
            record.category.data(), static_cast<int>(record.message.size()), record.message.data());
    }

    void flush() override {
        std::fflush(stdout);
        std::fflush(stderr);
    }
};

/// File sink.
///
/// Deliberately relies on stdio buffering rather than flushing every line: a
/// synchronous write per log call is exactly the "mysterious frame spike" the
/// ticket warns about. It *does* flush on Error and Fatal, because the lines
/// worth having after a crash are the ones immediately before it.
class FileSink final : public ILogSink {
public:
    explicit FileSink(std::FILE* file) : file_(file) {}

    ~FileSink() override {
        if (file_ != nullptr) {
            std::fclose(file_);
        }
    }

    void write(const LogRecord& record) override {
        if (file_ == nullptr) {
            return;
        }
        std::fprintf(file_, "[%-5.*s] %.*s (%.*s:%d): %.*s\n",
                     static_cast<int>(logLevelName(record.level).size()),
                     logLevelName(record.level).data(), static_cast<int>(record.category.size()),
                     record.category.data(), static_cast<int>(record.file.size()),
                     record.file.data(), record.line, static_cast<int>(record.message.size()),
                     record.message.data());
        if (record.level >= LogLevel::Error) {
            std::fflush(file_);
        }
    }

    void flush() override {
        if (file_ != nullptr) {
            std::fflush(file_);
        }
    }

private:
    std::FILE* file_ = nullptr;
};

} // namespace

std::string_view logLevelName(LogLevel level) {
    switch (level) {
    case LogLevel::Trace:
        return "trace";
    case LogLevel::Debug:
        return "debug";
    case LogLevel::Info:
        return "info";
    case LogLevel::Warning:
        return "warn";
    case LogLevel::Error:
        return "error";
    case LogLevel::Fatal:
        return "fatal";
    case LogLevel::Off:
        return "off";
    }
    return "?";
}

LogCategory::LogCategory(std::string_view name) {
    auto& s = state();

    {
        std::shared_lock read(s.categoriesMutex);
        for (std::size_t i = 0; i < s.categories.size(); ++i) {
            if (s.categories[i]->name == name) {
                id_ = static_cast<std::uint16_t>(i);
                return;
            }
        }
    }

    std::unique_lock write(s.categoriesMutex);
    // Re-check: another thread may have registered it between the two locks.
    for (std::size_t i = 0; i < s.categories.size(); ++i) {
        if (s.categories[i]->name == name) {
            id_ = static_cast<std::uint16_t>(i);
            return;
        }
    }
    auto entry = std::make_unique<CategoryState>();
    entry->name = std::string(name);
    s.categories.push_back(std::move(entry));
    id_ = static_cast<std::uint16_t>(s.categories.size() - 1);
}

std::string_view LogCategory::name() const {
    auto& s = state();
    std::shared_lock read(s.categoriesMutex);
    return id_ < s.categories.size() ? std::string_view(s.categories[id_]->name)
                                     : std::string_view("?");
}

LogLevel LogCategory::level() const {
    auto& s = state();
    std::shared_lock read(s.categoriesMutex);
    return id_ < s.categories.size() ? s.categories[id_]->level.load(std::memory_order_relaxed)
                                     : LogLevel::Off;
}

void LogCategory::setLevel(LogLevel level) const {
    auto& s = state();
    std::shared_lock read(s.categoriesMutex);
    if (id_ < s.categories.size()) {
        s.categories[id_]->level.store(level, std::memory_order_relaxed);
    }
}

bool logEnabled(const LogCategory& category, LogLevel level) {
    return level >= category.level();
}

void logWrite(const LogCategory& category, LogLevel level, std::string_view file, int line,
              std::string_view message) {
    HP_PROFILE_ZONE();

    const std::string_view categoryName = category.name();
    const LogRecord record{level, categoryName, message, file, line, currentThreadId()};

    auto& s = state();
    std::shared_lock read(s.sinksMutex);
    for (ILogSink* sink : s.sinks) {
        sink->write(record);
    }
}

void logAddSink(ILogSink* sink) {
    if (sink == nullptr) {
        return;
    }
    auto& s = state();
    std::unique_lock write(s.sinksMutex);
    if (std::find(s.sinks.begin(), s.sinks.end(), sink) == s.sinks.end()) {
        s.sinks.push_back(sink);
    }
}

void logRemoveSink(ILogSink* sink) {
    auto& s = state();
    std::unique_lock write(s.sinksMutex);
    s.sinks.erase(std::remove(s.sinks.begin(), s.sinks.end(), sink), s.sinks.end());
}

void logAddConsoleSink() {
    auto& s = state();
    std::lock_guard guard(s.builtinsMutex);
    for (const auto& existing : s.builtins) {
        if (dynamic_cast<ConsoleSink*>(existing.get()) != nullptr) {
            return; // idempotent
        }
    }
    auto sink = std::make_unique<ConsoleSink>();
    logAddSink(sink.get());
    s.builtins.push_back(std::move(sink));
}

bool logAddFileSink(const char* path) {
    if (path == nullptr) {
        return false;
    }
    std::FILE* file = std::fopen(path, "w");
    if (file == nullptr) {
        return false;
    }
    auto& s = state();
    std::lock_guard guard(s.builtinsMutex);
    auto sink = std::make_unique<FileSink>(file);
    logAddSink(sink.get());
    s.builtins.push_back(std::move(sink));
    return true;
}

void logFlush() {
    auto& s = state();
    std::shared_lock read(s.sinksMutex);
    for (ILogSink* sink : s.sinks) {
        sink->flush();
    }
}

void logSetGlobalLevel(LogLevel level) {
    auto& s = state();
    std::shared_lock read(s.categoriesMutex);
    for (auto& category : s.categories) {
        category->level.store(level, std::memory_order_relaxed);
    }
}

std::uint16_t logCategoryCount() {
    auto& s = state();
    std::shared_lock read(s.categoriesMutex);
    return static_cast<std::uint16_t>(s.categories.size());
}

LogCategory logCategoryAt(std::uint16_t index) {
    auto& s = state();
    std::string name;
    {
        std::shared_lock read(s.categoriesMutex);
        name = index < s.categories.size() ? s.categories[index]->name : std::string("?");
    }
    return LogCategory(name);
}

} // namespace hp
