// Logging (T0054).
//
// Bucket: integration. It links the engine shared library and writes a real file
// to disk, which is more than the fast bucket's "no filesystem, no subprocesses"
// budget allows.
//
// What is worth testing is the behaviour that would otherwise be discovered in
// production: that filtering happens *before* formatting (so a filtered line
// costs a comparison, not a string), that categories filter independently, and
// that a sink sees exactly what it should.

#include <doctest/doctest.h>

#include <hp/Log.hpp>

#include <cstdio>
#include <string>
#include <vector>

namespace {

/// Captures records so a test can assert on them. Copies every view, because
/// `LogRecord`'s views are only valid for the duration of the `write` call --
/// which is itself worth pinning down, since getting it wrong produces a test
/// that passes and a use-after-free in the editor console.
class CapturingSink final : public hp::ILogSink {
public:
    struct Entry {
        hp::LogLevel level;
        std::string category;
        std::string message;
        int line;
    };

    void write(const hp::LogRecord& record) override {
        entries.push_back(Entry{record.level, std::string(record.category),
                                std::string(record.message), record.line});
    }

    std::vector<Entry> entries;
};

/// Records that it was called. Used to prove filtering happens before
/// formatting -- if the argument is evaluated, the counter moves.
int g_formatCalls = 0;

int expensiveValue() {
    ++g_formatCalls;
    return 7;
}

} // namespace

TEST_CASE("a sink receives level, category and message") {
    CapturingSink sink;
    hp::logAddSink(&sink);

    const hp::LogCategory cat("test.basic");
    cat.setLevel(hp::LogLevel::Trace);

    HP_LOG_INFO(cat, "hello {}", 42);
    HP_LOG_ERROR(cat, "bad {}", "thing");

    hp::logRemoveSink(&sink);

    REQUIRE(sink.entries.size() == 2);
    CHECK(sink.entries[0].level == hp::LogLevel::Info);
    CHECK(sink.entries[0].category == "test.basic");
    CHECK(sink.entries[0].message == "hello 42");
    CHECK(sink.entries[1].level == hp::LogLevel::Error);
    CHECK(sink.entries[1].message == "bad thing");
    CHECK(sink.entries[0].line > 0);
}

TEST_CASE("a filtered-out record is not formatted") {
    // The point of checking the level before building the string: formatting is
    // the expensive part, and a trace call in a hot loop must cost a comparison
    // when trace is off.
    CapturingSink sink;
    hp::logAddSink(&sink);

    const hp::LogCategory cat("test.filter");
    cat.setLevel(hp::LogLevel::Warning);

    g_formatCalls = 0;
    HP_LOG_INFO(cat, "value {}", expensiveValue());
    CHECK_MESSAGE(g_formatCalls == 0,
                  "a suppressed log call still evaluated its arguments -- filtering is "
                  "happening after formatting rather than before");
    CHECK(sink.entries.empty());

    HP_LOG_ERROR(cat, "value {}", expensiveValue());
    CHECK(g_formatCalls == 1);
    CHECK(sink.entries.size() == 1);

    hp::logRemoveSink(&sink);
}

TEST_CASE("categories filter independently") {
    // The property that makes categories worth having. With one shared level the
    // only realistic filter is "off".
    CapturingSink sink;
    hp::logAddSink(&sink);

    const hp::LogCategory noisy("test.noisy");
    const hp::LogCategory quiet("test.quiet");
    noisy.setLevel(hp::LogLevel::Trace);
    quiet.setLevel(hp::LogLevel::Error);

    HP_LOG_DEBUG(noisy, "from noisy");
    HP_LOG_DEBUG(quiet, "from quiet");

    hp::logRemoveSink(&sink);

    REQUIRE(sink.entries.size() == 1);
    CHECK(sink.entries[0].category == "test.noisy");
}

TEST_CASE("the same category name resolves to the same channel") {
    // Two translation units -- or an engine and a gameplay module -- declaring
    // the same category must share one level, not two.
    const hp::LogCategory a("test.shared");
    const hp::LogCategory b("test.shared");
    CHECK(a.id() == b.id());

    a.setLevel(hp::LogLevel::Fatal);
    CHECK(b.level() == hp::LogLevel::Fatal);
    a.setLevel(hp::LogLevel::Info);
}

TEST_CASE("removing a sink stops delivery") {
    // The lifetime rule that keeps non-owning registration safe: a sink that is
    // removed must not be written to again, or a stack-allocated sink going out
    // of scope becomes a dangling pointer in the engine.
    CapturingSink sink;
    const hp::LogCategory cat("test.remove");
    cat.setLevel(hp::LogLevel::Trace);

    hp::logAddSink(&sink);
    HP_LOG_INFO(cat, "delivered");
    hp::logRemoveSink(&sink);
    HP_LOG_INFO(cat, "not delivered");

    REQUIRE(sink.entries.size() == 1);
    CHECK(sink.entries[0].message == "delivered");
}

TEST_CASE("registering the same sink twice does not duplicate delivery") {
    CapturingSink sink;
    const hp::LogCategory cat("test.dup");
    cat.setLevel(hp::LogLevel::Trace);

    hp::logAddSink(&sink);
    hp::logAddSink(&sink);
    HP_LOG_INFO(cat, "once");
    hp::logRemoveSink(&sink);

    CHECK(sink.entries.size() == 1);
}

TEST_CASE("the file sink writes and flushes on error") {
    const char* path = "hp_log_test_output.txt";
    std::remove(path);

    REQUIRE(hp::logAddFileSink(path));

    const hp::LogCategory cat("test.file");
    cat.setLevel(hp::LogLevel::Trace);
    HP_LOG_ERROR(cat, "written to disk");
    // Error flushes, so the content must be readable without closing the sink.

    std::FILE* f = std::fopen(path, "r");
    REQUIRE(f != nullptr);
    std::string contents;
    char buffer[512];
    while (std::fgets(buffer, sizeof buffer, f) != nullptr) {
        contents += buffer;
    }
    std::fclose(f);

    CHECK(contents.find("written to disk") != std::string::npos);
    CHECK(contents.find("test.file") != std::string::npos);
    CHECK(contents.find("error") != std::string::npos);

    std::remove(path);
}
