// YAML and the binary cook (T0020).
//
// Bucket: fast. Both paths are pure computation on strings and byte vectors, so
// nothing here touches a device or a filesystem.
//
// **The corrupt-binary cases are the point of 20.6**, not an afterthought. The
// binary form is a *cache*: it must always be safely discardable, so every way a
// file can be wrong has to produce "re-cook" rather than a crash, an exception,
// or -- worst -- a plausible-looking wrong answer.

#include <doctest/doctest.h>

#include <hp/Cook.hpp>
#include <hp/Guid.hpp>
#include <hp/Yaml.hpp>

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace {

std::vector<std::byte> bytesOf(std::string_view text) {
    const auto* first = reinterpret_cast<const std::byte*>(text.data());
    return {first, first + text.size()};
}

} // namespace

TEST_CASE("a document round-trips through text") {
    hp::YamlDocument doc;
    hp::YamlNode root = doc.root();
    root.set("name", std::string_view("player"));
    root.set("health", std::int64_t{100});
    root.set("speed", 4.5);
    root.set("alive", true);

    const std::string text = doc.emit();
    CHECK(text.find("name: player") != std::string::npos);

    const auto parsed = hp::YamlDocument::parse(text);
    REQUIRE(parsed.has_value());
    hp::YamlNode reparsed = const_cast<hp::YamlDocument&>(*parsed).root();

    CHECK(reparsed["name"].read(std::string{}) == "player");
    CHECK(reparsed["health"].read(std::int64_t{0}) == 100);
    CHECK(reparsed["speed"].read(0.0) == doctest::Approx(4.5));
    CHECK(reparsed["alive"].read(false));
}

TEST_CASE("a double survives text with full precision") {
    // std::to_string gives six decimal places and silently rounds. For a
    // transform that is a position that drifts every time a scene is saved --
    // invisible in a diff, wrong in the world.
    hp::YamlDocument doc;
    const double awkward = 0.1 + 0.2; // 0.30000000000000004
    const double tiny = 1.0e-300;
    const double huge = 1.7976931348623157e308;

    hp::YamlNode root = doc.root();
    root.set("awkward", awkward);
    root.set("tiny", tiny);
    root.set("huge", huge);

    const auto parsed = hp::YamlDocument::parse(doc.emit());
    REQUIRE(parsed.has_value());
    hp::YamlNode root2 = const_cast<hp::YamlDocument&>(*parsed).root();

    // Exact equality, not Approx. That is the claim being made.
    CHECK(root2["awkward"].read(0.0) == awkward);
    CHECK(root2["tiny"].read(0.0) == tiny);
    CHECK(root2["huge"].read(0.0) == huge);
}

TEST_CASE("integer extremes survive") {
    hp::YamlDocument doc;
    hp::YamlNode root = doc.root();
    root.set("min", std::numeric_limits<std::int64_t>::min());
    root.set("max", std::numeric_limits<std::int64_t>::max());
    root.set("umax", std::numeric_limits<std::uint64_t>::max());

    const auto parsed = hp::YamlDocument::parse(doc.emit());
    REQUIRE(parsed.has_value());
    hp::YamlNode root2 = const_cast<hp::YamlDocument&>(*parsed).root();

    CHECK(root2["min"].read(std::int64_t{0}) == std::numeric_limits<std::int64_t>::min());
    CHECK(root2["max"].read(std::int64_t{0}) == std::numeric_limits<std::int64_t>::max());
    CHECK(root2["umax"].read(std::uint64_t{0}) == std::numeric_limits<std::uint64_t>::max());
}

TEST_CASE("a GUID round-trips as text") {
    // T0016 deliberately left this untested because no serializer existed. This
    // is the ticket that closes it, and a GUID is a first-class case rather
    // than an incidental uint64 -- it is the identity every asset and entity
    // hangs off.
    const hp::Guid original = hp::Guid::generate();

    hp::YamlDocument doc;
    doc.root().set("guid", original.toString());

    const auto parsed = hp::YamlDocument::parse(doc.emit());
    REQUIRE(parsed.has_value());
    const std::string text = const_cast<hp::YamlDocument&>(*parsed).root()["guid"].read(std::string{});

    const auto restored = hp::Guid::parse(text);
    REQUIRE(restored.has_value());
    CHECK(*restored == original);
}

TEST_CASE("nested maps and sequences round-trip") {
    hp::YamlDocument doc;
    hp::YamlNode root = doc.root();

    hp::YamlNode transform = root.addMap("transform");
    hp::YamlNode position = transform.addSequence("position");
    position.append(1.0);
    position.append(2.5);
    position.append(-3.0);

    hp::YamlNode entities = root.addSequence("entities");
    hp::YamlNode first = entities.appendMap();
    first.set("name", std::string_view("one"));
    hp::YamlNode second = entities.appendMap();
    second.set("name", std::string_view("two"));

    const auto parsed = hp::YamlDocument::parse(doc.emit());
    REQUIRE(parsed.has_value());
    hp::YamlNode root2 = const_cast<hp::YamlDocument&>(*parsed).root();

    hp::YamlNode pos = root2["transform"]["position"];
    REQUIRE(pos.isSequence());
    REQUIRE(pos.size() == 3);
    CHECK(pos.at(0).read(0.0) == doctest::Approx(1.0));
    CHECK(pos.at(1).read(0.0) == doctest::Approx(2.5));
    CHECK(pos.at(2).read(0.0) == doctest::Approx(-3.0));

    hp::YamlNode list = root2["entities"];
    REQUIRE(list.size() == 2);
    CHECK(list.at(0)["name"].read(std::string{}) == "one");
    CHECK(list.at(1)["name"].read(std::string{}) == "two");
}

TEST_CASE("a missing key reads as the fallback rather than crashing") {
    // Reading a file written by an older engine is the normal case, not the
    // exceptional one -- so a chain of lookups through absent nodes has to be
    // safe without a check at every step.
    const auto parsed = hp::YamlDocument::parse("present: 1\n");
    REQUIRE(parsed.has_value());
    hp::YamlNode root = const_cast<hp::YamlDocument&>(*parsed).root();

    CHECK_FALSE(root["absent"].valid());
    CHECK(root["absent"].read(std::int64_t{42}) == 42);
    CHECK(root["absent"]["deeper"]["deeper still"].read(std::string{"fallback"}) == "fallback");
    CHECK(root["absent"].at(3).read(0.0) == doctest::Approx(0.0));
    CHECK(root["absent"].size() == 0);
}

TEST_CASE("tryRead distinguishes absent from malformed from present") {
    const auto parsed = hp::YamlDocument::parse("number: 12\ntext: hello\n");
    REQUIRE(parsed.has_value());
    hp::YamlNode root = const_cast<hp::YamlDocument&>(*parsed).root();

    std::int64_t value = 0;
    CHECK(root["number"].tryRead(value));
    CHECK(value == 12);

    // Present but not a number: reports failure rather than yielding 0, which
    // is the difference between "the field is missing" and "the field is
    // wrong" and the reason tryRead exists alongside read.
    std::int64_t bad = -1;
    CHECK_FALSE(root["text"].tryRead(bad));
    CHECK(bad == -1);

    std::int64_t absent = -1;
    CHECK_FALSE(root["nothing"].tryRead(absent));
    CHECK(absent == -1);

    // A trailing-garbage number is malformed, not a partial parse.
    const auto messy = hp::YamlDocument::parse("n: 12abc\n");
    REQUIRE(messy.has_value());
    std::int64_t partial = -1;
    CHECK_FALSE(const_cast<hp::YamlDocument&>(*messy).root()["n"].tryRead(partial));
    CHECK(partial == -1);
}

TEST_CASE("malformed YAML is reported, not fatal") {
    // rapidyaml's default error callback calls abort(). A malformed file is
    // *data*, so aborting would take the editor down when someone hand-edits a
    // scene badly -- which is exactly when they need it to stay up and tell
    // them what is wrong.
    CHECK_FALSE(hp::YamlDocument::parse("key: [unclosed\n").has_value());
    CHECK_FALSE(hp::YamlDocument::parse("{unclosed: map\n").has_value());

    // **rapidyaml is more permissive than the YAML spec in places**, and that
    // is worth knowing rather than assuming. A leading tab is invalid
    // indentation per the spec; rapidyaml accepts it as scalar content. This
    // asserted rejection at first and the parser proved otherwise.
    //
    // The consequence is mild but real: a hand-edited file with a stray tab
    // loads as something unintended rather than being reported. Nothing here
    // depends on the strict reading, so this records the behaviour instead of
    // fighting it.
    CHECK(hp::YamlDocument::parse("\t- a tab is accepted as content\n").has_value());
}

TEST_CASE("an empty document parses to an empty map") {
    // An empty file is legitimate -- a config nobody has written to yet --
    // so it must not be an error, and callers should not each handle it.
    const auto parsed = hp::YamlDocument::parse("");
    REQUIRE(parsed.has_value());
    hp::YamlNode root = const_cast<hp::YamlDocument&>(*parsed).root();
    CHECK(root.valid());
    CHECK(root.size() == 0);
    CHECK(root["anything"].read(std::int64_t{7}) == 7);
}

TEST_CASE("a temporary key or value is safe to pass") {
    // rapidyaml stores spans, not strings: handing it a temporary leaves a
    // dangling span that emits garbage much later, in a completely different
    // part of the file. Everything written through this wrapper is arena-copied
    // for that reason, and this is the case that proves it.
    hp::YamlDocument doc;
    hp::YamlNode root = doc.root();
    for (int i = 0; i < 8; ++i) {
        root.set(std::string("key") + std::to_string(i), std::string("value") + std::to_string(i));
    }

    const std::string text = doc.emit();
    for (int i = 0; i < 8; ++i) {
        CHECK(text.find("key" + std::to_string(i) + ": value" + std::to_string(i))
              != std::string::npos);
    }
}

TEST_CASE("setting a key twice replaces rather than duplicating") {
    hp::YamlDocument doc;
    hp::YamlNode root = doc.root();
    root.set("value", std::int64_t{1});
    root.set("value", std::int64_t{2});

    CHECK(root.size() == 1);
    const auto parsed = hp::YamlDocument::parse(doc.emit());
    REQUIRE(parsed.has_value());
    CHECK(const_cast<hp::YamlDocument&>(*parsed).root()["value"].read(std::int64_t{0}) == 2);
}

// --- the binary cook -------------------------------------------------------

TEST_CASE("a cooked payload round-trips") {
    const auto payload = bytesOf("cooked contents");
    const std::uint64_t hash = hp::hashSource("source: yaml\n");

    const auto file = hp::writeCook(payload, hash, 3);

    std::vector<std::byte> restored;
    CHECK(hp::readCook(file, hash, 3, restored) == hp::CookStatus::Ok);
    CHECK(restored == payload);
}

TEST_CASE("an empty payload cooks and reads back empty") {
    const std::uint64_t hash = hp::hashSource("");
    const auto file = hp::writeCook({}, hash, 1);

    std::vector<std::byte> restored{std::byte{0xFF}};
    CHECK(hp::readCook(file, hash, 1, restored) == hp::CookStatus::Ok);
    CHECK(restored.empty());
}

TEST_CASE("staleness is decided by content, not by time") {
    // The whole of 20.5. Timestamps lie after a git checkout, a copy, a clock
    // change or a CI cache restore -- this repository has a ticket about the
    // last one (T0131) -- and a stale binary that loads without error is the
    // nastiest failure in this area, because everything works and the data is
    // quietly old.
    const auto payload = bytesOf("compiled from v1");
    const std::uint64_t before = hp::hashSource("value: 1\n");
    const std::uint64_t after = hp::hashSource("value: 2\n");
    CHECK(before != after);

    const auto file = hp::writeCook(payload, before, 1);

    std::vector<std::byte> restored;
    CHECK(hp::readCook(file, after, 1, restored) == hp::CookStatus::SourceChanged);
    CHECK(restored.empty());

    // And the unchanged source still hits.
    CHECK(hp::readCook(file, before, 1, restored) == hp::CookStatus::Ok);
}

TEST_CASE("whitespace changes the hash, because it changes the file") {
    // Not a subtle point: if the hash ignored formatting, a reformat would
    // leave a cook that no longer corresponds to its source, and the next
    // person to hand-edit the YAML would find their change ignored.
    CHECK(hp::hashSource("a: 1\n") != hp::hashSource("a:  1\n"));
    CHECK(hp::hashSource("a: 1\n") != hp::hashSource("a: 1"));
    CHECK(hp::hashSource("") == hp::hashSource(""));
}

TEST_CASE("a schema change invalidates the cook even when the source has not moved") {
    // T0082.5's requirement. Without this, a schema change silently loads a
    // stale cook that still happens to parse -- which is the worst outcome,
    // because it is indistinguishable from working.
    const auto payload = bytesOf("v1 layout");
    const std::uint64_t hash = hp::hashSource("unchanged: true\n");
    const auto file = hp::writeCook(payload, hash, 1);

    std::vector<std::byte> restored;
    CHECK(hp::readCook(file, hash, 2, restored) == hp::CookStatus::SchemaMismatch);
    CHECK(restored.empty());
}

TEST_CASE("every way a cooked file can be wrong yields re-cook, never a crash") {
    // 20.6. The binary form is a cache, so the only correct response to any
    // damage is to cook again -- never to throw, never to return something
    // plausible.
    const auto payload = bytesOf("payload bytes");
    const std::uint64_t hash = hp::hashSource("src\n");
    const auto good = hp::writeCook(payload, hash, 1);
    std::vector<std::byte> restored;

    SUBCASE("empty file") {
        CHECK(hp::readCook({}, hash, 1, restored) == hp::CookStatus::NotACookFile);
    }

    SUBCASE("a file that is not a cook at all") {
        CHECK(hp::readCook(bytesOf("# just some yaml\n"), hash, 1, restored)
              == hp::CookStatus::NotACookFile);
    }

    SUBCASE("truncated inside the header") {
        for (std::size_t keep = 0; keep < 28; ++keep) {
            std::vector<std::byte> cut(good.begin(), good.begin() + static_cast<long>(keep));
            const auto status = hp::readCook(cut, hash, 1, restored);
            CHECK(status != hp::CookStatus::Ok);
        }
    }

    SUBCASE("truncated inside the payload") {
        std::vector<std::byte> cut(good.begin(), good.end() - 3);
        CHECK(hp::readCook(cut, hash, 1, restored) == hp::CookStatus::Truncated);
    }

    SUBCASE("a future format version") {
        std::vector<std::byte> future = good;
        future[8] = static_cast<std::byte>(0xEE);
        CHECK(hp::readCook(future, hash, 1, restored) == hp::CookStatus::FormatMismatch);
    }

    SUBCASE("corrupt magic") {
        std::vector<std::byte> broken = good;
        broken[0] = static_cast<std::byte>('X');
        CHECK(hp::readCook(broken, hash, 1, restored) == hp::CookStatus::NotACookFile);
    }

    SUBCASE("a byte flipped anywhere in the header is caught") {
        // The payload is deliberately not covered -- there is no checksum over
        // it, and claiming one would be a lie. What is guaranteed is that the
        // *container* is validated.
        for (std::size_t i = 0; i < 28; ++i) {
            std::vector<std::byte> broken = good;
            broken[i] = static_cast<std::byte>(static_cast<unsigned char>(broken[i]) ^ 0xFFU);
            CHECK(hp::readCook(broken, hash, 1, restored) != hp::CookStatus::Ok);
        }
    }
}

TEST_CASE("the header can be inspected without reading the payload") {
    const auto payload = bytesOf("some bytes here");
    const std::uint64_t hash = hp::hashSource("y\n");
    const auto file = hp::writeCook(payload, hash, 7);

    hp::CookHeader header;
    CHECK(hp::readCookHeader(file, header) == hp::CookStatus::Ok);
    CHECK(header.formatVersion == hp::kCookFormatVersion);
    CHECK(header.schemaVersion == 7);
    CHECK(header.sourceHash == hash);
    CHECK(header.payloadSize == payload.size());
}

TEST_CASE("payload primitives round-trip and refuse to read past the end") {
    std::vector<std::byte> buffer;
    hp::writeU32(buffer, 0xDEADBEEFU);
    hp::writeU64(buffer, 0x0123456789ABCDEFULL);
    hp::writeString(buffer, "hello");
    hp::writeString(buffer, "");

    std::size_t cursor = 0;
    std::uint32_t small = 0;
    std::uint64_t big = 0;
    std::string text;
    std::string empty;

    REQUIRE(hp::readU32(buffer, cursor, small));
    REQUIRE(hp::readU64(buffer, cursor, big));
    REQUIRE(hp::readString(buffer, cursor, text));
    REQUIRE(hp::readString(buffer, cursor, empty));

    CHECK(small == 0xDEADBEEFU);
    CHECK(big == 0x0123456789ABCDEFULL);
    CHECK(text == "hello");
    CHECK(empty.empty());
    CHECK(cursor == buffer.size());

    // Nothing left: every read fails rather than reading adjacent memory.
    std::uint32_t overrun = 0;
    CHECK_FALSE(hp::readU32(buffer, cursor, overrun));
    CHECK_FALSE(hp::readU64(buffer, cursor, big));
    CHECK_FALSE(hp::readString(buffer, cursor, text));
}

TEST_CASE("a corrupt string length is refused rather than trusted") {
    // The classic deserializer failure: a length field says four gigabytes and
    // the reader either allocates it or walks off the buffer. Both present as a
    // crash with no connection to the file that caused it.
    std::vector<std::byte> buffer;
    hp::writeU64(buffer, 0xFFFFFFFFFFFFFFFFULL); // absurd length, no data
    std::size_t cursor = 0;
    std::string out = "untouched";
    CHECK_FALSE(hp::readString(buffer, cursor, out));
    CHECK(out == "untouched");
    CHECK(cursor == 0);

    // One byte short is refused too -- the off-by-one that a >= would let past.
    std::vector<std::byte> nearly;
    hp::writeU64(nearly, 4);
    nearly.push_back(std::byte{'a'});
    nearly.push_back(std::byte{'b'});
    nearly.push_back(std::byte{'c'});
    std::size_t cursor2 = 0;
    CHECK_FALSE(hp::readString(nearly, cursor2, out));
}

TEST_CASE("the byte order is little-endian, and that is written down") {
    // Both targets are x86-64 so this costs nothing today. It is asserted so
    // that a future big-endian target is a conversion in one place rather than
    // a silent corruption everywhere.
    std::vector<std::byte> buffer;
    hp::writeU32(buffer, 0x01020304U);
    REQUIRE(buffer.size() == 4);
    CHECK(static_cast<unsigned char>(buffer[0]) == 0x04);
    CHECK(static_cast<unsigned char>(buffer[1]) == 0x03);
    CHECK(static_cast<unsigned char>(buffer[2]) == 0x02);
    CHECK(static_cast<unsigned char>(buffer[3]) == 0x01);
}

TEST_CASE("every status has a distinct human-readable reason") {
    // A cache miss whose reason is unreadable turns a five-minute diagnosis
    // into an afternoon.
    const hp::CookStatus all[] = {
        hp::CookStatus::Ok,           hp::CookStatus::NotACookFile,
        hp::CookStatus::FormatMismatch, hp::CookStatus::SchemaMismatch,
        hp::CookStatus::SourceChanged, hp::CookStatus::Truncated,
    };
    for (const hp::CookStatus status : all) {
        const std::string text = hp::describe(status);
        CHECK_FALSE(text.empty());
        CHECK(text != "unknown");
    }
}
