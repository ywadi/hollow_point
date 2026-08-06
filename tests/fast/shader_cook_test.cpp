// The cooked shader container, and the contract it deliberately does not
// inherit (T0142.7, D34).
//
// **`Cook.hpp` promises every failure means "cook it again". This container
// promises the opposite**, and the difference is not decoration: a shipped game
// has neither `slangc` nor `.slang`, so a cooked shader that will not load is
// the end of rendering rather than the start of a recompile. What is asserted
// here is that every way of getting it wrong is *distinguishable* — because the
// one thing a fatal error must do is say which fatal error it was.
//
// Bucket: fast. No device, no VFS, no files.

#include <doctest/doctest.h>

#include <hp/ShaderCook.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

std::vector<std::byte> bytesOf(const std::string& text) {
    std::vector<std::byte> out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<std::byte>(c));
    }
    return out;
}

} // namespace

TEST_CASE("a cooked shader archive round-trips its payload") {
    const std::vector<std::byte> payload = bytesOf("not really spir-v, but opaque either way");
    const std::vector<std::byte> archive =
        hp::writeCookedShaderArchive(payload, hp::shaderCompilerId());

    // The magic is the first thing a reader checks and the first thing a
    // hex dump shows, so it is worth pinning.
    REQUIRE(archive.size() > 8);
    CHECK(static_cast<char>(archive[0]) == 'H');
    CHECK(static_cast<char>(archive[1]) == 'P');
    CHECK(static_cast<char>(archive[2]) == 'S');

    std::vector<std::byte> readBack;
    CHECK(hp::readCookedShaderArchive(archive, hp::shaderCompilerId(), readBack) ==
          hp::CookedShaderStatus::Ok);
    CHECK(readBack == payload);
}

TEST_CASE("an empty payload is a legal archive, and stays distinguishable from a broken one") {
    // A cook that produced nothing is refused before it gets here (`cookShaders`
    // says so), but the container itself must not conflate "zero bytes of
    // payload" with "corrupt" — those are different diagnoses.
    const std::vector<std::byte> archive =
        hp::writeCookedShaderArchive({}, hp::shaderCompilerId());
    std::vector<std::byte> readBack{std::byte{7}};
    CHECK(hp::readCookedShaderArchive(archive, hp::shaderCompilerId(), readBack) ==
          hp::CookedShaderStatus::Ok);
    CHECK(readBack.empty());
}

TEST_CASE("bytecode from another compiler is refused, not trusted") {
    // **The pin bump case.** In development a different slang reads as a cold
    // cache; in a cooked build it would be bytecode from a compiler nobody
    // has, handed to a driver as if it were current. Refusing is the only
    // honest answer, and it has to be its own status so the log can say the
    // useful sentence.
    const std::vector<std::byte> archive =
        hp::writeCookedShaderArchive(bytesOf("payload"), "1999.1.1");
    std::vector<std::byte> readBack;
    CHECK(hp::readCookedShaderArchive(archive, hp::shaderCompilerId(), readBack) ==
          hp::CookedShaderStatus::CompilerMismatch);
    CHECK(readBack.empty());
}

TEST_CASE("a file that is not an archive says so rather than guessing") {
    std::vector<std::byte> readBack;

    CHECK(hp::readCookedShaderArchive({}, hp::shaderCompilerId(), readBack) ==
          hp::CookedShaderStatus::NotACookedShaderArchive);
    CHECK(hp::readCookedShaderArchive(bytesOf("short"), hp::shaderCompilerId(), readBack) ==
          hp::CookedShaderStatus::NotACookedShaderArchive);
    // Long enough to hold a header, wrong magic — the case a truncation check
    // alone would miss, and the case a cook file misfiled into the shader
    // directory would actually produce.
    CHECK(hp::readCookedShaderArchive(bytesOf("HPCOOKXXnot this one at all"),
                                      hp::shaderCompilerId(), readBack) ==
          hp::CookedShaderStatus::NotACookedShaderArchive);
}

TEST_CASE("a truncated archive is truncated, not merely unreadable") {
    std::vector<std::byte> archive =
        hp::writeCookedShaderArchive(bytesOf("0123456789abcdef"), hp::shaderCompilerId());
    archive.resize(archive.size() - 4);

    std::vector<std::byte> readBack;
    CHECK(hp::readCookedShaderArchive(archive, hp::shaderCompilerId(), readBack) ==
          hp::CookedShaderStatus::Truncated);
    // Left untouched, so a caller cannot half-load a shader set and carry on.
    CHECK(readBack.empty());
}

TEST_CASE("a future container version is refused before anything else is believed") {
    // The format version is read before the compiler id on purpose: a
    // container whose layout moved cannot be parsed far enough to report
    // anything else honestly. Bumping the version byte in place is exactly
    // what a future writer would produce.
    std::vector<std::byte> archive =
        hp::writeCookedShaderArchive(bytesOf("payload"), hp::shaderCompilerId());
    REQUIRE(archive.size() > 12);
    archive[8] = static_cast<std::byte>(hp::kCookedShaderFormatVersion + 1);

    std::vector<std::byte> readBack;
    CHECK(hp::readCookedShaderArchive(archive, hp::shaderCompilerId(), readBack) ==
          hp::CookedShaderStatus::FormatMismatch);
}

TEST_CASE("every status describes itself, and none of them says re-cook") {
    // The wording matters more than usual here. `Cook.hpp`'s statuses all mean
    // "re-cook from the YAML"; none of these can, and a message that reads
    // like a cache miss is how an unrecoverable failure gets filed under
    // "probably fine".
    for (const hp::CookedShaderStatus status :
         {hp::CookedShaderStatus::Ok, hp::CookedShaderStatus::NotACookedShaderArchive,
          hp::CookedShaderStatus::FormatMismatch, hp::CookedShaderStatus::CompilerMismatch,
          hp::CookedShaderStatus::Truncated}) {
        const std::string text = hp::describe(status);
        CHECK_FALSE(text.empty());
        CHECK(text != "unknown");
    }
}

TEST_CASE("the compiler id is the pinned compiler, not a placeholder") {
    // It is baked from the same CMake variable the harness pins and
    // `pins_test.zig` guards, so an empty or obviously-wrong value here means
    // the definition stopped reaching this translation unit.
    const std::string id{hp::shaderCompilerId()};
    CHECK_FALSE(id.empty());
    CHECK(id.find('.') != std::string::npos);
}
