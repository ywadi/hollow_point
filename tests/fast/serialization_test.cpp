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
#include <hp/Light.hpp>
#include <hp/Scene.hpp>
#include <hp/Serialize.hpp>
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

// --- subtree capture and graft (T0022's D23 blob) --------------------------
//
// `emitSubtree` and `graft` are a pair and are tested as one: the property that
// matters is not what either produces in isolation but that the second undoes
// the first. They exist so a component this build cannot interpret survives a
// save-after-load, which is a data-loss bug when it does not work and silent
// when it half-works.

TEST_CASE("a subtree survives being emitted and grafted back") {
    auto source = hp::YamlDocument::parse(R"(components:
  PlayerController:
    speed: 4.5
    waypoints: [a, b]
)");
    REQUIRE(source.has_value());

    const std::string fragment =
        source->root()["components"]["PlayerController"].emitSubtree();
    // The key comes with it — that is what makes a fragment self-describing
    // enough to be stored on its own and grafted back without reattaching it.
    CHECK(fragment.find("PlayerController") != std::string::npos);
    CHECK(fragment.find("4.5") != std::string::npos);

    hp::YamlDocument destination;
    hp::YamlNode components = destination.root().addMap("components");
    REQUIRE(components.graft(fragment));

    // Re-parsed rather than string-compared: the emitter's exact whitespace is
    // not the contract, and asserting on it makes this test fail for a
    // rapidyaml upgrade that changed nothing that matters.
    auto reloaded = hp::YamlDocument::parse(destination.emit());
    REQUIRE(reloaded.has_value());
    hp::YamlNode controller = reloaded->root()["components"]["PlayerController"];
    REQUIRE(controller.isMap());
    CHECK(controller["speed"].read(0.0) == doctest::Approx(4.5));
    REQUIRE(controller["waypoints"].isSequence());
    CHECK(controller["waypoints"].at(1).read(std::string{}) == "b");
}

TEST_CASE("a grafted fragment outlives the text it was parsed from") {
    // **The failure this guards is not a compile error.** rapidyaml duplicates
    // nodes by copying spans, not characters, so a grafted node points into the
    // buffer it was parsed from — and if the document does not take ownership,
    // the emitted text is garbage produced long after the call that caused it.
    hp::YamlDocument destination;
    hp::YamlNode components = destination.root().addMap("components");
    {
        std::string fragment = "PlayerController:\n  speed: 4.5\n";
        REQUIRE(components.graft(fragment));
        fragment.assign(4096, 'x');   // reuse the storage, if it were shared
        fragment.clear();
        fragment.shrink_to_fit();
    }

    auto reloaded = hp::YamlDocument::parse(destination.emit());
    REQUIRE(reloaded.has_value());
    CHECK(reloaded->root()["components"]["PlayerController"]["speed"].read(0.0)
          == doctest::Approx(4.5));
}

TEST_CASE("grafting appends rather than replacing") {
    hp::YamlDocument document;
    hp::YamlNode components = document.root().addMap("components");
    components.addMap("Transform").set("x", std::int64_t{1});

    REQUIRE(components.graft("PlayerController:\n  speed: 4.5\n"));
    REQUIRE(components.graft("Inventory:\n  slots: 8\n"));

    auto reloaded = hp::YamlDocument::parse(document.emit());
    REQUIRE(reloaded.has_value());
    hp::YamlNode result = reloaded->root()["components"];
    CHECK(result.size() == 3);
    CHECK(result["Transform"]["x"].read(std::int64_t{0}) == 1);
    CHECK(result["PlayerController"]["speed"].read(0.0) == doctest::Approx(4.5));
    CHECK(result["Inventory"]["slots"].read(std::int64_t{0}) == 8);
}

TEST_CASE("a graft that cannot work is refused rather than corrupting the document") {
    hp::YamlDocument document;
    hp::YamlNode map = document.root().addMap("map");

    SUBCASE("a sequence into a mapping") {
        // Both bits set on one node produces something the emitter cannot
        // write, and it would fail at emit time — far from the cause.
        CHECK_FALSE(map.graft("- one\n- two\n"));
    }
    SUBCASE("a bare scalar, which has no children and no key") {
        CHECK_FALSE(map.graft("just a scalar\n"));
    }
    SUBCASE("text that is not YAML") {
        CHECK_FALSE(map.graft("\t: not: yaml: ["));
    }
    SUBCASE("nothing at all") {
        CHECK_FALSE(map.graft(""));
    }

    // Refused means unchanged, not partially applied.
    auto reloaded = hp::YamlDocument::parse(document.emit());
    REQUIRE(reloaded.has_value());
    CHECK(reloaded->root()["map"].size() == 0);
}

TEST_CASE("a sequence-valued key round-trips through the pair") {
    // The pair's contract is `parent.graft(child.emitSubtree())`, so the graft
    // target is the *parent* of the captured node. A keyed sequence therefore
    // grafts into a mapping — its key needs somewhere to live — and the case
    // below covers the other direction.
    auto source = hp::YamlDocument::parse("items: [1, 2, 3]\n");
    REQUIRE(source.has_value());

    hp::YamlDocument destination;
    REQUIRE(destination.root().graft(source->root()["items"].emitSubtree()));

    auto reloaded = hp::YamlDocument::parse(destination.emit());
    REQUIRE(reloaded.has_value());
    REQUIRE(reloaded->root()["items"].isSequence());
    CHECK(reloaded->root()["items"].size() == 3);
    CHECK(reloaded->root()["items"].at(2).read(std::int64_t{0}) == 3);
}

TEST_CASE("sequence elements graft into a sequence") {
    hp::YamlDocument document;
    hp::YamlNode items = document.root().addSequence("items");
    items.append(std::int64_t{1});
    REQUIRE(items.graft("- 2\n- 3\n"));

    auto reloaded = hp::YamlDocument::parse(document.emit());
    REQUIRE(reloaded.has_value());
    REQUIRE(reloaded->root()["items"].isSequence());
    CHECK(reloaded->root()["items"].size() == 3);
    CHECK(reloaded->root()["items"].at(2).read(std::int64_t{0}) == 3);
}

TEST_CASE("emitting an invalid node yields nothing rather than crashing") {
    hp::YamlDocument document;
    CHECK(document.root()["absent"].emitSubtree().empty());
    CHECK(hp::YamlNode{}.emitSubtree().empty());
    CHECK_FALSE(hp::YamlNode{}.graft("a: 1\n"));
}

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

// --- reflection-derived serialization (20.3) -------------------------------

TEST_CASE("a reflected component round-trips through YAML with no per-type code") {
    // **The whole point of 20.3.** Nothing below knows what a Transform is.
    // Serialization is derived from T0053's property enumeration, so a
    // component that registers its properties is serializable by that fact --
    // there is no second mechanism to keep in step, which is the "four
    // switches" failure T0053 exists to prevent.
    hp::adoptMetaContext();
    hp::registerCoreComponents();

    hp::Transform original;
    original.position = hp::float3(1.5F, -2.0F, 3.25F);
    original.rotation = hp::Quaternion(0.1F, 0.2F, 0.3F, 0.9F);
    original.scale = hp::float3(2.0F, 2.0F, 2.0F);

    hp::YamlDocument doc;
    REQUIRE(hp::writeProperties(doc.root(), entt::forward_as_meta(original)));

    const std::string text = doc.emit();
    // Readable: the names come from reflection, which is what the T0053 fix
    // made possible.
    CHECK(text.find("position:") != std::string::npos);
    CHECK(text.find("rotation:") != std::string::npos);
    CHECK(text.find("scale:") != std::string::npos);

    const auto parsed = hp::YamlDocument::parse(text);
    REQUIRE(parsed.has_value());

    hp::Transform restored;
    REQUIRE(hp::readProperties(const_cast<hp::YamlDocument&>(*parsed).root(),
                               entt::forward_as_meta(restored)));

    CHECK(restored.position.x == doctest::Approx(original.position.x));
    CHECK(restored.position.y == doctest::Approx(original.position.y));
    CHECK(restored.position.z == doctest::Approx(original.position.z));
    CHECK(restored.scale.x == doctest::Approx(original.scale.x));
    CHECK(restored.rotation.q.x == doctest::Approx(original.rotation.q.x));
    CHECK(restored.rotation.q.w == doctest::Approx(original.rotation.q.w));
}

TEST_CASE("a component with a GUID round-trips, and the GUID stays readable") {
    hp::adoptMetaContext();
    hp::registerCoreComponents();

    hp::MeshRenderer original;
    original.mesh = hp::Guid::generate();
    original.materials = {hp::Guid::generate(), hp::Guid{}, hp::Guid::generate()};

    hp::YamlDocument doc;
    REQUIRE(hp::writeProperties(doc.root(), entt::forward_as_meta(original)));

    // Written as its canonical string, not a raw integer: a GUID in a diff has
    // to be matchable against another file by a person.
    const std::string text = doc.emit();
    CHECK(text.find(original.mesh.toString()) != std::string::npos);

    const auto parsed = hp::YamlDocument::parse(text);
    REQUIRE(parsed.has_value());

    hp::MeshRenderer restored;
    REQUIRE(hp::readProperties(const_cast<hp::YamlDocument&>(*parsed).root(),
                               entt::forward_as_meta(restored)));
    CHECK(restored.mesh == original.mesh);
    REQUIRE(restored.materials.size() == original.materials.size());
    for (std::size_t i = 0; i < original.materials.size(); ++i) {
        CHECK(restored.materials[i] == original.materials[i]);
    }
}

TEST_CASE("a sequence of leaves is a flat list, not a list of single-key maps") {
    // It used to write `- 0: <guid>` per element, because the leaf writer could
    // only set a key. That is unreadable in a diff and miserable to hand-author,
    // which is the whole point of the format — so the leaf writer gained a
    // destination rather than the file gaining a shape.
    hp::adoptMetaContext();
    hp::registerCoreComponents();

    hp::MeshRenderer original;
    original.mesh = hp::Guid{0x11};
    original.materials = {hp::Guid{0xAA}, hp::Guid{0xBB}};

    hp::YamlDocument doc;
    REQUIRE(hp::writeProperties(doc.root(), entt::forward_as_meta(original)));
    const std::string text = doc.emit();

    CHECK(text.find("- " + hp::Guid{0xAA}.toString()) != std::string::npos);
    CHECK(text.find("0: ") == std::string::npos);
}

TEST_CASE("an empty sequence round-trips as empty, not as one default element") {
    // The distinction carries meaning: empty means "every surface uses what the
    // model imported", and a single default entry would mean "surface 0 does,
    // and there is only one surface".
    hp::adoptMetaContext();
    hp::registerCoreComponents();

    hp::MeshRenderer original;
    original.mesh = hp::Guid{0x22};

    hp::YamlDocument doc;
    REQUIRE(hp::writeProperties(doc.root(), entt::forward_as_meta(original)));
    const auto parsed = hp::YamlDocument::parse(doc.emit());
    REQUIRE(parsed.has_value());

    hp::MeshRenderer restored;
    restored.materials = {hp::Guid{0xFF}};  // must be cleared, not appended to
    REQUIRE(hp::readProperties(const_cast<hp::YamlDocument&>(*parsed).root(),
                               entt::forward_as_meta(restored)));
    CHECK(restored.materials.empty());
}

TEST_CASE("a sequence of leaves survives the cook") {
    hp::adoptMetaContext();
    hp::registerCoreComponents();

    hp::MeshRenderer original;
    original.mesh = hp::Guid{0x33};
    original.materials = {hp::Guid{0xAA}, hp::Guid{}, hp::Guid{0xCC}};

    std::vector<std::byte> payload;
    REQUIRE(hp::cookProperties(entt::forward_as_meta(original), payload));

    hp::MeshRenderer restored;
    std::size_t cursor = 0;
    REQUIRE(hp::readCookedProperties(payload, cursor, entt::forward_as_meta(restored)));
    REQUIRE(restored.materials.size() == 3);
    CHECK(restored.materials[0] == hp::Guid{0xAA});
    CHECK_FALSE(restored.materials[1].isValid());
    CHECK(restored.materials[2] == hp::Guid{0xCC});
}

TEST_CASE("a camera round-trips every field it gained from T0130") {
    // Covers bools, floats and integers in one type, and pins that the fields
    // T0130 and T0081 added are actually reachable through reflection rather
    // than merely registered.
    hp::adoptMetaContext();
    hp::registerCoreComponents();

    hp::Camera original;
    original.verticalFov = 1.2F;
    original.nearPlane = 0.25F;
    original.farPlane = 500.0F;
    original.orthographic = true;
    original.orthographicSize = 12.0F;
    original.exposureEv100 = 9.5F;
    original.depthOfField = true;
    original.aperture = 1.4F;
    original.focusDistance = 3.5F;
    original.priority = 42;
    original.enabled = false;
    original.cullingMask = hp::LayerMask{0x00FF00FFU};
    original.viewSlot = 3;

    hp::YamlDocument doc;
    REQUIRE(hp::writeProperties(doc.root(), entt::forward_as_meta(original)));

    const auto parsed = hp::YamlDocument::parse(doc.emit());
    REQUIRE(parsed.has_value());

    hp::Camera restored;
    REQUIRE(hp::readProperties(const_cast<hp::YamlDocument&>(*parsed).root(),
                               entt::forward_as_meta(restored)));

    CHECK(restored.verticalFov == doctest::Approx(original.verticalFov));
    CHECK(restored.farPlane == doctest::Approx(original.farPlane));
    CHECK(restored.orthographic == original.orthographic);
    CHECK(restored.depthOfField == original.depthOfField);
    CHECK(restored.aperture == doctest::Approx(original.aperture));
    CHECK(restored.priority == original.priority);
    CHECK(restored.enabled == original.enabled);
    CHECK(restored.cullingMask == original.cullingMask);
    CHECK(restored.viewSlot == original.viewSlot);
}

TEST_CASE("a field absent from the document keeps its current value") {
    // Forward compatibility, and the reason reading is lenient while writing is
    // exact. A component gains a property; every file written before it exists
    // must still load, with the new field at its default rather than at zero.
    hp::adoptMetaContext();
    hp::registerCoreComponents();

    const auto parsed = hp::YamlDocument::parse("position: [9, 9, 9]\n");
    REQUIRE(parsed.has_value());

    hp::Transform target;
    target.scale = hp::float3(7.0F, 7.0F, 7.0F);
    REQUIRE(hp::readProperties(const_cast<hp::YamlDocument&>(*parsed).root(),
                               entt::forward_as_meta(target)));

    CHECK(target.position.x == doctest::Approx(9.0F));
    // Untouched, not zeroed.
    CHECK(target.scale.x == doctest::Approx(7.0F));
}

TEST_CASE("a field the type does not have is ignored") {
    // Backward compatibility. A property is removed; files still containing it
    // must load rather than being refused, or removing a field becomes a
    // migration.
    hp::adoptMetaContext();
    hp::registerCoreComponents();

    const auto parsed = hp::YamlDocument::parse(
        "position: [1, 2, 3]\nlongGoneField: 12\nanotherOne: hello\n");
    REQUIRE(parsed.has_value());

    hp::Transform target;
    REQUIRE(hp::readProperties(const_cast<hp::YamlDocument&>(*parsed).root(),
                               entt::forward_as_meta(target)));
    CHECK(target.position.y == doctest::Approx(2.0F));
}

TEST_CASE("a malformed field leaves its target alone rather than corrupting it") {
    // A wrong-length vector is refused whole. A half-applied float3 is a value
    // that looks plausible and is wrong, which is the failure this layer exists
    // to avoid.
    hp::adoptMetaContext();
    hp::registerCoreComponents();

    const auto parsed = hp::YamlDocument::parse("position: [1, 2]\nscale: not-a-vector\n");
    REQUIRE(parsed.has_value());

    hp::Transform target;
    target.position = hp::float3(5.0F, 5.0F, 5.0F);
    target.scale = hp::float3(3.0F, 3.0F, 3.0F);
    REQUIRE(hp::readProperties(const_cast<hp::YamlDocument&>(*parsed).root(),
                               entt::forward_as_meta(target)));

    CHECK(target.position.x == doctest::Approx(5.0F));
    CHECK(target.scale.x == doctest::Approx(3.0F));
}

TEST_CASE("a float survives the round trip exactly, not approximately") {
    // The reason doubles are emitted with 17 significant digits. A position
    // that drifts every save is invisible in a diff and wrong in the world.
    hp::adoptMetaContext();
    hp::registerCoreComponents();

    hp::Transform original;
    original.position = hp::float3(0.1F, 1.0F / 3.0F, 123456.789F);

    hp::YamlDocument doc;
    REQUIRE(hp::writeProperties(doc.root(), entt::forward_as_meta(original)));
    const auto parsed = hp::YamlDocument::parse(doc.emit());
    REQUIRE(parsed.has_value());

    hp::Transform restored;
    REQUIRE(hp::readProperties(const_cast<hp::YamlDocument&>(*parsed).root(),
                               entt::forward_as_meta(restored)));

    CHECK(restored.position.x == original.position.x);
    CHECK(restored.position.y == original.position.y);
    CHECK(restored.position.z == original.position.z);
}

TEST_CASE("an unregistered type is refused rather than silently skipped") {
    struct NotReflected {
        int value = 0;
    };
    hp::adoptMetaContext();

    hp::YamlDocument doc;
    const NotReflected thing;
    CHECK_FALSE(hp::writeReflected(doc.root(), "thing", entt::forward_as_meta(thing)));
    // Nothing was written, rather than a key with a misleading empty value.
    CHECK_FALSE(doc.root().has("thing"));
}

namespace {

/// A reflected type containing another reflected type, which is the nesting
/// path `writeReflected` recurses through.
struct Inner {
    float weight = 0.0F;
    std::string label;
};

struct Outer {
    Inner inner;
    std::int32_t count = 0;
};

} // namespace

TEST_CASE("a reflected type nested inside another round-trips") {
    // The recursion in writeReflected/readReflected. Every component tested
    // above bottoms out in leaves at the first level, so without this the
    // nested path would be code nothing runs.
    hp::adoptMetaContext();
    hp::reflect<Inner>("SerializeInner")
        .property<&Inner::weight>("weight")
        .property<&Inner::label>("label");
    hp::reflect<Outer>("SerializeOuter")
        .property<&Outer::inner>("inner")
        .property<&Outer::count>("count");

    Outer original;
    original.inner.weight = 2.5F;
    original.inner.label = "nested";
    original.count = 17;

    hp::YamlDocument doc;
    REQUIRE(hp::writeProperties(doc.root(), entt::forward_as_meta(original)));

    const std::string text = doc.emit();
    CHECK(text.find("inner:") != std::string::npos);
    CHECK(text.find("label: nested") != std::string::npos);

    const auto parsed = hp::YamlDocument::parse(text);
    REQUIRE(parsed.has_value());

    Outer restored;
    REQUIRE(hp::readProperties(const_cast<hp::YamlDocument&>(*parsed).root(),
                               entt::forward_as_meta(restored)));

    CHECK(restored.inner.weight == doctest::Approx(2.5F));
    CHECK(restored.inner.label == "nested");
    CHECK(restored.count == 17);
}

TEST_CASE("a reflected object round-trips through the binary cook") {
    // Closes the last "Done when": object -> binary -> object, value-identical,
    // through the same reflection the YAML path uses.
    hp::adoptMetaContext();
    hp::registerCoreComponents();

    hp::Transform original;
    original.position = hp::float3(1.25F, -8.5F, 0.125F);
    original.rotation = hp::Quaternion(0.0F, 0.7071F, 0.0F, 0.7071F);
    original.scale = hp::float3(3.0F, 4.0F, 5.0F);

    std::vector<std::byte> payload;
    REQUIRE(hp::cookProperties(entt::forward_as_meta(original), payload));
    CHECK_FALSE(payload.empty());

    hp::Transform restored;
    std::size_t cursor = 0;
    REQUIRE(hp::readCookedProperties(payload, cursor, entt::forward_as_meta(restored)));
    CHECK(cursor == payload.size());

    // **Exact**, not approximate: floats are cooked as their bit pattern, so a
    // binary round trip has no reason to lose anything at all.
    CHECK(restored.position.x == original.position.x);
    CHECK(restored.position.y == original.position.y);
    CHECK(restored.position.z == original.position.z);
    CHECK(restored.scale.z == original.scale.z);
    CHECK(restored.rotation.q.y == original.rotation.q.y);
}

TEST_CASE("YAML and binary agree on the same object") {
    // The two leaf lists are written separately and must stay in step. A type
    // one path can write and the other cannot would show up as a cook that
    // silently drops a field, so both are driven from the same object here.
    hp::adoptMetaContext();
    hp::registerCoreComponents();

    hp::Camera original;
    original.verticalFov = 0.9F;
    original.orthographic = true;
    original.priority = -5;
    original.cullingMask = hp::LayerMask{0xABCD1234U};
    original.viewSlot = 2;
    original.depthOfField = true;

    hp::YamlDocument doc;
    REQUIRE(hp::writeProperties(doc.root(), entt::forward_as_meta(original)));

    // **Every registered property must appear in the emitted text.** This
    // exists because a key once went missing from a document intermittently, on
    // Windows only, and the read side cannot tell that from a field an older
    // build never wrote -- so it kept the default and nothing reported
    // anything. Asserting on the emitted text turns that silent drop into a
    // caught failure. See T0020's note and the comment in `Yaml.cpp`.
    const std::string emitted = doc.emit();
    const entt::meta_type cameraType = entt::resolve(entt::type_id<hp::Camera>());
    REQUIRE(static_cast<bool>(cameraType));
    for (auto&& [id, data] : cameraType.data()) {
        REQUIRE(data.name() != nullptr);
        INFO("property ", data.name(), " missing from:\n", emitted);
        CHECK(emitted.find(std::string(data.name()) + ":") != std::string::npos);
    }

    const auto parsed = hp::YamlDocument::parse(emitted);
    REQUIRE(parsed.has_value());
    hp::Camera viaYaml;
    REQUIRE(hp::readProperties(const_cast<hp::YamlDocument&>(*parsed).root(),
                               entt::forward_as_meta(viaYaml)));

    std::vector<std::byte> payload;
    REQUIRE(hp::cookProperties(entt::forward_as_meta(original), payload));
    hp::Camera viaBinary;
    std::size_t cursor = 0;
    REQUIRE(hp::readCookedProperties(payload, cursor, entt::forward_as_meta(viaBinary)));

    CHECK(viaYaml.orthographic == viaBinary.orthographic);
    CHECK(viaYaml.priority == viaBinary.priority);
    CHECK(viaYaml.cullingMask == viaBinary.cullingMask);
    CHECK(viaYaml.viewSlot == viaBinary.viewSlot);
    CHECK(viaYaml.depthOfField == viaBinary.depthOfField);
    CHECK(viaYaml.verticalFov == doctest::Approx(viaBinary.verticalFov));
}

TEST_CASE("a nested object round-trips through the binary cook") {
    hp::adoptMetaContext();
    hp::reflect<Inner>("SerializeInner")
        .property<&Inner::weight>("weight")
        .property<&Inner::label>("label");
    hp::reflect<Outer>("SerializeOuter")
        .property<&Outer::inner>("inner")
        .property<&Outer::count>("count");

    Outer original;
    original.inner.weight = -0.5F;
    original.inner.label = "deep";
    original.count = -3;

    std::vector<std::byte> payload;
    REQUIRE(hp::cookProperties(entt::forward_as_meta(original), payload));

    Outer restored;
    std::size_t cursor = 0;
    REQUIRE(hp::readCookedProperties(payload, cursor, entt::forward_as_meta(restored)));
    CHECK(restored.inner.weight == original.inner.weight);
    CHECK(restored.inner.label == "deep");
    CHECK(restored.count == -3);
}

TEST_CASE("an unknown cooked property is skipped, not fatal") {
    // The whole reason every property is length-prefixed. A payload written by
    // a build whose type had an extra field must still load -- skipping exactly
    // that field's bytes rather than losing sync and misreading the rest.
    hp::adoptMetaContext();
    hp::reflect<Inner>("SerializeInner")
        .property<&Inner::weight>("weight")
        .property<&Inner::label>("label");

    // Hand-build a payload: one known property, one that no type has.
    std::vector<std::byte> payload;
    hp::writeU32(payload, 2);

    hp::writeU32(payload, static_cast<std::uint32_t>(hp::hashSource("weight")));
    std::vector<std::byte> weightBytes;
    hp::writeU32(weightBytes, 0x40200000U); // 2.5f
    hp::writeU64(payload, weightBytes.size());
    payload.insert(payload.end(), weightBytes.begin(), weightBytes.end());

    hp::writeU32(payload, static_cast<std::uint32_t>(hp::hashSource("fieldFromTheFuture")));
    std::vector<std::byte> futureBytes;
    hp::writeString(futureBytes, "something this build knows nothing about");
    hp::writeU64(payload, futureBytes.size());
    payload.insert(payload.end(), futureBytes.begin(), futureBytes.end());

    Inner restored;
    restored.label = "kept";
    std::size_t cursor = 0;
    REQUIRE(hp::readCookedProperties(payload, cursor, entt::forward_as_meta(restored)));

    CHECK(restored.weight == doctest::Approx(2.5F));
    // The unknown record was skipped whole, so the reader stayed in sync and
    // the property it *does* have but the payload omitted kept its value.
    CHECK(restored.label == "kept");
    CHECK(cursor == payload.size());
}

TEST_CASE("a truncated cooked payload is refused rather than half-applied") {
    hp::adoptMetaContext();
    hp::registerCoreComponents();

    hp::Transform original;
    original.position = hp::float3(1.0F, 2.0F, 3.0F);
    std::vector<std::byte> payload;
    REQUIRE(hp::cookProperties(entt::forward_as_meta(original), payload));

    for (std::size_t keep = 1; keep < payload.size(); keep += 3) {
        std::vector<std::byte> cut(payload.begin(), payload.begin() + static_cast<long>(keep));
        hp::Transform restored;
        std::size_t cursor = 0;
        // Either it reports failure, or it stops early -- what it must never do
        // is read past the buffer, which is what the length checks prevent.
        const bool ok = hp::readCookedProperties(cut, cursor, entt::forward_as_meta(restored));
        CHECK(cursor <= cut.size());
        (void)ok;
    }
}

// --- enums (T0060, and a defect T0139 left behind) ---------------------------
//
// Enums are written by **name**, generically, for every reflected enum rather
// than one hand-written case per type. What this replaced was not merely
// unreadable: `readLeaf` parsed only an integer, so a hand-authored
// `Light: {type: Spot}` — which is the whole point of T0139's authoring mode —
// failed to read, hit the lenient rule that leaves an unreadable field alone,
// and produced a **directional** light with no warning at all.
//
// The fixture in `scene_serialize_test.cpp` had `type: Directional` in it and
// passed throughout, because Directional is the default. That is why these
// cases use Spot and Point: a test whose expected value is the default cannot
// tell "read correctly" from "never read".

TEST_CASE("an enum is written by its name, not its number") {
    hp::adoptMetaContext();
    hp::registerCoreComponents();

    hp::Light light;
    light.type = hp::LightType::Spot;

    hp::YamlDocument doc;
    REQUIRE(hp::writeProperties(doc.root(), entt::forward_as_meta(light)));
    const std::string text = doc.emit();
    CHECK(text.find("type: Spot") != std::string::npos);
    // `type: 2` survives nobody inserting a value into the middle of the enum,
    // and that is exactly the edit an engine makes between releases.
    CHECK(text.find("type: 2") == std::string::npos);
}

TEST_CASE("an enum reads back from its name") {
    hp::adoptMetaContext();
    hp::registerCoreComponents();

    const auto doc = hp::YamlDocument::parse("type: Spot\n");
    REQUIRE(doc);
    hp::Light restored;
    REQUIRE(restored.type == hp::LightType::Directional); // the default it must move off
    CHECK(hp::readProperties(const_cast<hp::YamlDocument&>(*doc).root(),
                             entt::forward_as_meta(restored)));
    CHECK(restored.type == hp::LightType::Spot);
}

TEST_CASE("an enum still reads from its number, because files already say that") {
    // Every scene written before enums had names carries integers. Reading them
    // is not legacy support to delete later: it is what makes writing names a
    // representation change rather than a schema break.
    hp::adoptMetaContext();
    hp::registerCoreComponents();

    const auto doc = hp::YamlDocument::parse("type: 1\n");
    REQUIRE(doc);
    hp::Light restored;
    CHECK(hp::readProperties(const_cast<hp::YamlDocument&>(*doc).root(),
                             entt::forward_as_meta(restored)));
    CHECK(restored.type == hp::LightType::Point);
}

TEST_CASE("a name this build does not have leaves the field alone") {
    hp::adoptMetaContext();
    hp::registerCoreComponents();

    const auto doc = hp::YamlDocument::parse("type: Volumetric\n");
    REQUIRE(doc);
    hp::Light restored;
    restored.type = hp::LightType::Point;
    (void)hp::readProperties(const_cast<hp::YamlDocument&>(*doc).root(),
                             entt::forward_as_meta(restored));
    // Not Directional-by-reset and not garbage: whatever it already held.
    CHECK(restored.type == hp::LightType::Point);
}

TEST_CASE("an enum round-trips through the cook, which stores its number") {
    // The cook is a cache keyed on a hash of its source, so it is never read by
    // a person and an enumerator rename invalidates it rather than being
    // misread from it. What matters is that the two paths agree on the value.
    hp::adoptMetaContext();
    hp::registerCoreComponents();

    hp::Light original;
    original.type = hp::LightType::Spot;
    original.intensity = 2.5F;

    std::vector<std::byte> payload;
    REQUIRE(hp::cookProperties(entt::forward_as_meta(original), payload));

    hp::Light restored;
    std::size_t cursor = 0;
    REQUIRE(hp::readCookedProperties(payload, cursor, entt::forward_as_meta(restored)));
    CHECK(restored.type == hp::LightType::Spot);
    CHECK(restored.intensity == doctest::Approx(2.5F));
}

TEST_CASE("an out-of-range number is refused by the cook path too") {
    // The YAML path has covered this since T0079 (`light_test.cpp`). The binary
    // path had the same guard hand-written and it moved into the generic code
    // with everything else, so it is worth pinning on both sides -- an enum with
    // no matching enumerator must never reach a switch that has no default case.
    hp::adoptMetaContext();
    hp::registerCoreComponents();

    hp::Light original;
    original.type = hp::LightType::Point;
    std::vector<std::byte> payload;
    REQUIRE(hp::cookProperties(entt::forward_as_meta(original), payload));

    // Rewrite the cooked `type` record's value to 99. It is the first property
    // registered on `Light`, so its 8-byte value sits after the u32 count, the
    // u32 name hash and the u64 length.
    constexpr std::size_t kValueAt = 4 + 4 + 8;
    REQUIRE(payload.size() > kValueAt);
    payload[kValueAt] = static_cast<std::byte>(99);

    hp::Light restored;
    restored.type = hp::LightType::Spot;
    std::size_t cursor = 0;
    (void)hp::readCookedProperties(payload, cursor, entt::forward_as_meta(restored));
    CHECK(restored.type == hp::LightType::Spot);
}
