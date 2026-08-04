// The virtual filesystem (T0103, D13).
//
// Bucket: integration. It creates real directories and real ZIP archives on
// disk, which is well past the fast bucket's "no filesystem" budget.
//
// **The archives are built here rather than checked in**, and that is
// deliberate: a binary fixture in the repository is a thing nobody can review,
// nobody updates, and which silently encodes whatever the tool that made it did
// that day. These are written byte by byte from a minimal ZIP writer below, so
// what is being tested is visible in the source.
//
// The case that matters most is `a later mount overrides an earlier one`. That
// is not a filesystem nicety -- it *is* the patch and DLC mechanism (D13), and
// if it silently stopped working the symptom would be a shipped patch that does
// not take effect.

#include <doctest/doctest.h>

#include <hp/Vfs.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

/// A scratch directory that removes itself.
///
/// Under the system temp directory, never the working directory -- a test that
/// writes into the repository root is how `hp_log_test_output.txt` ended up
/// committed twice.
class Scratch {
public:
    explicit Scratch(const char* name) {
        path_ = std::filesystem::temp_directory_path() / ("hp_vfs_test_" + std::string(name));
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
        std::filesystem::create_directories(path_, ec);
    }

    ~Scratch() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;

    [[nodiscard]] std::string str() const { return path_.string(); }
    [[nodiscard]] std::filesystem::path operator/(const std::string& child) const {
        return path_ / child;
    }

private:
    std::filesystem::path path_;
};

/// Writes a text file, creating parent directories.
void writeHostFile(const std::filesystem::path& path, const std::string& text) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    out << text;
}

void put32(std::vector<unsigned char>& out, std::uint32_t value) {
    out.push_back(static_cast<unsigned char>(value & 0xFF));
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
    out.push_back(static_cast<unsigned char>((value >> 16) & 0xFF));
    out.push_back(static_cast<unsigned char>((value >> 24) & 0xFF));
}

void put16(std::vector<unsigned char>& out, std::uint16_t value) {
    out.push_back(static_cast<unsigned char>(value & 0xFF));
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
}

/// CRC-32 as ZIP defines it. Written out rather than pulled from zlib because
/// the point of building these archives by hand is that nothing is hidden.
std::uint32_t crc32(const std::string& data) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const char ch : data) {
        crc ^= static_cast<unsigned char>(ch);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U & (~(crc & 1U) + 1U));
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

/// Writes a ZIP containing the given (path, contents) entries, stored
/// uncompressed.
///
/// Stored rather than deflated on purpose: compression is zlib's job and
/// PhysicsFS reads both, so deflating here would test zlib rather than the
/// mount path.
void writeZip(const std::filesystem::path& path,
              const std::vector<std::pair<std::string, std::string>>& entries) {
    std::vector<unsigned char> out;
    std::vector<std::uint32_t> offsets;
    std::vector<std::uint32_t> crcs;

    for (const auto& [name, contents] : entries) {
        offsets.push_back(static_cast<std::uint32_t>(out.size()));
        const std::uint32_t crc = crc32(contents);
        crcs.push_back(crc);

        put32(out, 0x04034B50); // local file header
        put16(out, 20);         // version needed
        put16(out, 0);          // flags
        put16(out, 0);          // method: stored
        put16(out, 0);          // mod time
        put16(out, 0);          // mod date
        put32(out, crc);
        put32(out, static_cast<std::uint32_t>(contents.size()));
        put32(out, static_cast<std::uint32_t>(contents.size()));
        put16(out, static_cast<std::uint16_t>(name.size()));
        put16(out, 0); // extra length
        out.insert(out.end(), name.begin(), name.end());
        out.insert(out.end(), contents.begin(), contents.end());
    }

    const std::uint32_t directoryStart = static_cast<std::uint32_t>(out.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& [name, contents] = entries[i];
        put32(out, 0x02014B50); // central directory header
        put16(out, 20);         // version made by
        put16(out, 20);         // version needed
        put16(out, 0);          // flags
        put16(out, 0);          // method: stored
        put16(out, 0);          // mod time
        put16(out, 0);          // mod date
        put32(out, crcs[i]);
        put32(out, static_cast<std::uint32_t>(contents.size()));
        put32(out, static_cast<std::uint32_t>(contents.size()));
        put16(out, static_cast<std::uint16_t>(name.size()));
        put16(out, 0); // extra
        put16(out, 0); // comment
        put16(out, 0); // disk number
        put16(out, 0); // internal attrs
        put32(out, 0); // external attrs
        put32(out, offsets[i]);
        out.insert(out.end(), name.begin(), name.end());
    }
    const std::uint32_t directorySize = static_cast<std::uint32_t>(out.size()) - directoryStart;

    put32(out, 0x06054B50); // end of central directory
    put16(out, 0);          // disk
    put16(out, 0);          // disk with directory
    put16(out, static_cast<std::uint16_t>(entries.size()));
    put16(out, static_cast<std::uint16_t>(entries.size()));
    put32(out, directorySize);
    put32(out, directoryStart);
    put16(out, 0); // comment length

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
}

/// Brings the VFS up for one case and takes it down afterwards.
///
/// PhysicsFS state is global, so a case that left a mount behind would change
/// the result of whichever case ran next -- an order-dependent suite that passes
/// alone and fails together.
class VfsFixture {
public:
    VfsFixture() { REQUIRE(hp::Vfs::init(nullptr)); }
    ~VfsFixture() { hp::Vfs::shutdown(); }

    VfsFixture(const VfsFixture&) = delete;
    VfsFixture& operator=(const VfsFixture&) = delete;
};

} // namespace

TEST_CASE("the filesystem comes up and goes down, twice over") {
    // Global state, so double-init and double-shutdown are reachable in real
    // code -- a module reload, a test harness, an editor restarting a project.
    CHECK(hp::Vfs::init(nullptr));
    CHECK(hp::Vfs::ready());
    CHECK(hp::Vfs::init(nullptr));
    CHECK(hp::Vfs::ready());

    hp::Vfs::shutdown();
    CHECK_FALSE(hp::Vfs::ready());
    hp::Vfs::shutdown();
    CHECK_FALSE(hp::Vfs::ready());
}

TEST_CASE("nothing resolves before init") {
    hp::Vfs::shutdown();
    CHECK_FALSE(hp::Vfs::exists("anything"));
    CHECK(hp::Vfs::kind("anything") == hp::PathKind::Missing);
    CHECK_FALSE(hp::Vfs::read("anything").has_value());
    CHECK(hp::Vfs::mounts().empty());
    CHECK_FALSE(hp::Vfs::mount("/tmp"));
}

TEST_CASE("a loose directory and an archive resolve identically") {
    // The "Done when" this exists for: dev and shipped builds differ only in
    // what is mounted, never in the code that reads.
    const VfsFixture vfs;
    const Scratch scratch("same");

    writeHostFile(scratch / "loose/data/greeting.txt", "hello");
    writeZip(scratch / "packed.zip", {{"data/greeting.txt", "hello"}});

    SUBCASE("from the directory") {
        REQUIRE(hp::Vfs::mount((scratch / "loose").string()));
        REQUIRE(hp::Vfs::exists("data/greeting.txt"));
        CHECK(hp::Vfs::readText("data/greeting.txt").value() == "hello");
        CHECK(hp::Vfs::kind("data") == hp::PathKind::Directory);
    }

    SUBCASE("from the archive, by the same path") {
        REQUIRE(hp::Vfs::mount((scratch / "packed.zip").string()));
        REQUIRE(hp::Vfs::exists("data/greeting.txt"));
        CHECK(hp::Vfs::readText("data/greeting.txt").value() == "hello");
        CHECK(hp::Vfs::kind("data") == hp::PathKind::Directory);
    }
}

TEST_CASE("a later mount overrides an earlier one, per file") {
    // **This is the patch and DLC mechanism** (D13), not a filesystem nicety.
    // If it stopped working the symptom would be a shipped patch that does not
    // take effect, which is invisible until a player reports the bug it was
    // supposed to fix.
    const VfsFixture vfs;
    const Scratch scratch("override");

    writeZip(scratch / "base.zip", {
                                       {"data/config.txt", "base"},
                                       {"data/untouched.txt", "original"},
                                   });
    writeZip(scratch / "patch.zip", {{"data/config.txt", "patched"}});

    REQUIRE(hp::Vfs::mount((scratch / "base.zip").string()));
    REQUIRE(hp::Vfs::mount((scratch / "patch.zip").string(), {}, hp::MountOrder::Prepend));

    // The patched file wins.
    CHECK(hp::Vfs::readText("data/config.txt").value() == "patched");
    // **Per file, not per pack.** A file the patch does not carry still comes
    // from the base -- which is what makes a patch a patch rather than a
    // replacement.
    CHECK(hp::Vfs::readText("data/untouched.txt").value() == "original");

    // And the diagnostic says which copy won, because when this goes wrong it
    // is otherwise very hard to see.
    CHECK(hp::Vfs::resolvedSource("data/config.txt")
          == (scratch / "patch.zip").string());
    CHECK(hp::Vfs::resolvedSource("data/untouched.txt")
          == (scratch / "base.zip").string());
}

TEST_CASE("append does not override, which is the other half of the decision") {
    // A DLC pack that *adds* content must not shadow the base game by accident,
    // so the default is append. Getting this backwards is a pack that silently
    // replaces files it only meant to sit beside.
    const VfsFixture vfs;
    const Scratch scratch("append");

    writeZip(scratch / "base.zip", {{"data/config.txt", "base"}});
    writeZip(scratch / "dlc.zip", {
                                      {"data/config.txt", "dlc"},
                                      {"data/extra.txt", "new content"},
                                  });

    REQUIRE(hp::Vfs::mount((scratch / "base.zip").string()));
    REQUIRE(hp::Vfs::mount((scratch / "dlc.zip").string(), {}, hp::MountOrder::Append));

    CHECK(hp::Vfs::readText("data/config.txt").value() == "base");
    CHECK(hp::Vfs::readText("data/extra.txt").value() == "new content");
}

TEST_CASE("a loose directory can shadow a pack, which is the dev workflow") {
    // An edited file on disk must beat the packed copy, or iterating means
    // repacking. Same mechanism, different mount order -- which is the whole
    // argument for one mechanism.
    const VfsFixture vfs;
    const Scratch scratch("dev");

    writeZip(scratch / "game.zip", {{"data/tuning.txt", "shipped"}});
    writeHostFile(scratch / "working/data/tuning.txt", "being edited");

    REQUIRE(hp::Vfs::mount((scratch / "game.zip").string()));
    REQUIRE(hp::Vfs::mount((scratch / "working").string(), {}, hp::MountOrder::Prepend));

    CHECK(hp::Vfs::readText("data/tuning.txt").value() == "being edited");
}

TEST_CASE("directories merge across mounts") {
    const VfsFixture vfs;
    const Scratch scratch("merge");

    writeZip(scratch / "a.zip", {{"shared/one.txt", "1"}});
    writeHostFile(scratch / "b/shared/two.txt", "2");

    REQUIRE(hp::Vfs::mount((scratch / "a.zip").string()));
    REQUIRE(hp::Vfs::mount((scratch / "b").string()));

    const auto entries = hp::Vfs::list("shared");
    CHECK(entries.size() == 2);
    bool sawOne = false;
    bool sawTwo = false;
    for (const auto& entry : entries) {
        sawOne = sawOne || entry == "one.txt";
        sawTwo = sawTwo || entry == "two.txt";
    }
    CHECK(sawOne);
    CHECK(sawTwo);
}

TEST_CASE("a mount point puts an archive somewhere other than the root") {
    const VfsFixture vfs;
    const Scratch scratch("mountpoint");

    writeZip(scratch / "dlc.zip", {{"level.txt", "extra level"}});
    REQUIRE(hp::Vfs::mount((scratch / "dlc.zip").string(), "dlc/pack1"));

    CHECK(hp::Vfs::exists("dlc/pack1/level.txt"));
    CHECK(hp::Vfs::readText("dlc/pack1/level.txt").value() == "extra level");
    // Not at the root, so a pack cannot collide with the base game unless it
    // was mounted to collide.
    CHECK_FALSE(hp::Vfs::exists("level.txt"));
}

TEST_CASE("mounting something that is not there fails and changes nothing") {
    const VfsFixture vfs;
    const Scratch scratch("missing");

    const auto before = hp::Vfs::mounts().size();
    CHECK_FALSE(hp::Vfs::mount((scratch / "no-such-directory").string()));
    CHECK_FALSE(hp::Vfs::mount((scratch / "no-such-archive.zip").string()));
    CHECK(hp::Vfs::mounts().size() == before);
}

TEST_CASE("a file that is not there fails the same way from either source") {
    // 103.5's last clause. A missing file must not be distinguishable by how it
    // is missing, or callers start depending on which mount kind they are on.
    const VfsFixture vfs;
    const Scratch scratch("absent");

    writeZip(scratch / "packed.zip", {{"present.txt", "here"}});
    writeHostFile(scratch / "loose/present.txt", "here");

    REQUIRE(hp::Vfs::mount((scratch / "packed.zip").string()));
    REQUIRE(hp::Vfs::mount((scratch / "loose").string()));

    CHECK_FALSE(hp::Vfs::exists("absent.txt"));
    CHECK(hp::Vfs::kind("absent.txt") == hp::PathKind::Missing);
    CHECK_FALSE(hp::Vfs::read("absent.txt").has_value());
    CHECK_FALSE(hp::Vfs::readText("absent.txt").has_value());
    CHECK(hp::Vfs::resolvedSource("absent.txt").empty());
}

TEST_CASE("unmounting removes the override and restores what was underneath") {
    const VfsFixture vfs;
    const Scratch scratch("unmount");

    writeZip(scratch / "base.zip", {{"data/config.txt", "base"}});
    writeZip(scratch / "patch.zip", {{"data/config.txt", "patched"}});

    REQUIRE(hp::Vfs::mount((scratch / "base.zip").string()));
    REQUIRE(hp::Vfs::mount((scratch / "patch.zip").string(), {}, hp::MountOrder::Prepend));
    REQUIRE(hp::Vfs::readText("data/config.txt").value() == "patched");

    REQUIRE(hp::Vfs::unmount((scratch / "patch.zip").string()));
    CHECK(hp::Vfs::readText("data/config.txt").value() == "base");

    CHECK_FALSE(hp::Vfs::unmount((scratch / "never-mounted.zip").string()));
}

TEST_CASE("writes go to the write directory and cannot escape it") {
    const VfsFixture vfs;
    const Scratch scratch("writes");

    // Nothing can be written before a write directory is chosen. That is the
    // default on purpose: a build that has not chosen one cannot scatter files
    // into the working directory.
    CHECK_FALSE(hp::Vfs::writeText("early.txt", "no"));

    REQUIRE(hp::Vfs::setWriteDirectory((scratch / "saves").string()));
    // Computed into a bool first: doctest decomposes a binary comparison and
    // refuses anything more complex, so an `a == b || a == c` inside CHECK is a
    // compile error rather than a test.
    //
    // The trailing separator is why there are two options at all -- PhysicsFS
    // reports the write directory with one on some platforms and not others,
    // and pinning either spelling would fail on the other target.
    const std::string reported = hp::Vfs::writeDirectory();
    const std::string expected = (scratch / "saves").string();
    const bool matches = reported == expected || reported == expected + "/"
                         || reported == expected + "\\";
    CHECK(matches);

    REQUIRE(hp::Vfs::writeText("slot0.sav", "player state"));
    // The write directory is not automatically readable, so mount it -- which is
    // the same mechanism again rather than a special case.
    REQUIRE(hp::Vfs::mount((scratch / "saves").string(), {}, hp::MountOrder::Prepend));
    CHECK(hp::Vfs::readText("slot0.sav").value() == "player state");
    CHECK(std::filesystem::exists(scratch / "saves/slot0.sav"));

    SUBCASE("an escaping path is refused") {
        // The reason this matters: a save name can come from a player, and a
        // player-supplied "../../.bashrc" must not be a write primitive.
        CHECK_FALSE(hp::Vfs::writeText("../escaped.txt", "no"));
        CHECK_FALSE(hp::Vfs::writeText("../../escaped.txt", "no"));
        CHECK_FALSE(hp::Vfs::writeText("saves/../../escaped.txt", "no"));
        CHECK_FALSE(hp::Vfs::writeText("/etc/passwd", "no"));
        CHECK_FALSE(hp::Vfs::writeText("C:/windows/system32/x.dll", "no"));
        CHECK_FALSE(hp::Vfs::writeText("..\\escaped.txt", "no"));

        // Nothing landed anywhere near the parent.
        CHECK_FALSE(std::filesystem::exists(scratch / "escaped.txt"));
        CHECK_FALSE(std::filesystem::exists(scratch / "../escaped.txt"));
    }

    SUBCASE("subdirectories work, and separate saves from logs") {
        // 103.4's layout: saves/, logs/ and crash/ are kept apart so a future
        // cloud-sync root can target saves alone.
        REQUIRE(hp::Vfs::createDirectory("saves"));
        REQUIRE(hp::Vfs::createDirectory("logs"));
        REQUIRE(hp::Vfs::writeText("saves/slot1.sav", "state"));
        REQUIRE(hp::Vfs::writeText("logs/session.log", "lines"));
        CHECK(std::filesystem::exists(scratch / "saves/saves/slot1.sav"));
        CHECK(std::filesystem::exists(scratch / "saves/logs/session.log"));
    }

    SUBCASE("deleting works and reports honestly") {
        REQUIRE(hp::Vfs::writeText("temp.txt", "x"));
        CHECK(hp::Vfs::remove("temp.txt"));
        CHECK_FALSE(std::filesystem::exists(scratch / "saves/temp.txt"));
        CHECK_FALSE(hp::Vfs::remove("temp.txt"));
    }
}

TEST_CASE("an empty file reads as empty, not as missing") {
    // Different answers -- "not there" and "there and says nothing" -- and
    // conflating them has bitten asset pipelines before.
    const VfsFixture vfs;
    const Scratch scratch("empty");

    writeHostFile(scratch / "loose/empty.txt", "");
    REQUIRE(hp::Vfs::mount((scratch / "loose").string()));

    const auto bytes = hp::Vfs::read("empty.txt");
    REQUIRE(bytes.has_value());
    CHECK(bytes->empty());

    const auto text = hp::Vfs::readText("empty.txt");
    REQUIRE(text.has_value());
    CHECK(text->empty());

    CHECK(hp::Vfs::exists("empty.txt"));
}

TEST_CASE("binary content survives a round trip through an archive") {
    // Text helpers are a convenience; the byte path is the real one, and a
    // NUL in the middle is where a naive strlen-based implementation fails.
    const VfsFixture vfs;
    const Scratch scratch("binary");

    const std::string payload("\x01\x00\x02\xFF\x00\x7F", 6);
    writeZip(scratch / "bin.zip", {{"blob.bin", payload}});
    REQUIRE(hp::Vfs::mount((scratch / "bin.zip").string()));

    const auto bytes = hp::Vfs::read("blob.bin");
    REQUIRE(bytes.has_value());
    REQUIRE(bytes->size() == 6);
    CHECK(static_cast<unsigned char>((*bytes)[1]) == 0x00);
    CHECK(static_cast<unsigned char>((*bytes)[3]) == 0xFF);
    CHECK(static_cast<unsigned char>((*bytes)[5]) == 0x7F);
}

TEST_CASE("mounts are reported in search order") {
    // The order is the DLC semantics, so it has to be inspectable.
    const VfsFixture vfs;
    const Scratch scratch("order");

    writeZip(scratch / "first.zip", {{"a.txt", "a"}});
    writeZip(scratch / "second.zip", {{"b.txt", "b"}});

    REQUIRE(hp::Vfs::mount((scratch / "first.zip").string()));
    REQUIRE(hp::Vfs::mount((scratch / "second.zip").string(), {}, hp::MountOrder::Prepend));

    const auto paths = hp::Vfs::mounts();
    REQUIRE(paths.size() >= 2);
    CHECK(paths[0] == (scratch / "second.zip").string());
    CHECK(paths[1] == (scratch / "first.zip").string());
}

TEST_CASE("concurrent reads from many threads return correct data") {
    // 103.7, and the ticket is explicit that this must be a measurement rather
    // than an assumption. PhysicsFS is a C library with global init state, and
    // its concurrency guarantees were not confirmed when D13 was taken. The job
    // system (T0026) and threading model (T0050) will want async asset loading,
    // and discovering a problem at *that* point is expensive.
    //
    // **What this proves is correctness, not scalability.** Every thread gets
    // the right bytes; whether they are serialised behind one internal lock is
    // not answered here and does not need to be yet -- "reads are serialised and
    // that is fine" is an acceptable outcome, but it must be a known one. What
    // would *not* be acceptable is corruption, and that is what this rules out.
    const VfsFixture vfs;
    const Scratch scratch("threads");

    std::vector<std::pair<std::string, std::string>> entries;
    for (int i = 0; i < 16; ++i) {
        entries.emplace_back("asset" + std::to_string(i) + ".txt",
                             std::string(256 + i * 7, static_cast<char>('a' + (i % 26))));
    }
    writeZip(scratch / "assets.zip", entries);
    REQUIRE(hp::Vfs::mount((scratch / "assets.zip").string()));

    constexpr int kThreads = 8;
    constexpr int kRounds = 40;
    std::atomic<int> mismatches{0};
    std::atomic<int> failures{0};

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&entries, &mismatches, &failures, t] {
            for (int round = 0; round < kRounds; ++round) {
                // Deliberately overlapping: every thread reads every file, so
                // the same archive entry is being decoded concurrently rather
                // than each thread having its own.
                const std::size_t index = static_cast<std::size_t>((round + t) % entries.size());
                const auto text = hp::Vfs::readText(entries[index].first);
                if (!text) {
                    ++failures;
                } else if (*text != entries[index].second) {
                    ++mismatches;
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    MESSAGE("concurrent reads: " << kThreads << " threads x " << kRounds << " reads, "
                                 << failures.load() << " failures, " << mismatches.load()
                                 << " mismatches");

    // A mismatch would mean two readers sharing a file handle or a decode
    // buffer -- silent corruption, and the worst possible outcome because the
    // bytes would still look like data.
    CHECK(mismatches.load() == 0);
    CHECK(failures.load() == 0);
}

TEST_CASE("a preference directory is per organisation and application") {
    const VfsFixture vfs;

    const std::string dir = hp::Vfs::preferenceDirectory("HollowPointTest", "VfsCase");
    REQUIRE_FALSE(dir.empty());
    CHECK(std::filesystem::exists(dir));
    CHECK(dir.find("VfsCase") != std::string::npos);

    // Left behind deliberately would be rude on a developer's machine.
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
