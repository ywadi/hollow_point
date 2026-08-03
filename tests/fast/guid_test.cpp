// Stable identity (T0016).
//
// Bucket: fast. Pure value type -- no filesystem, no subprocesses.

#include <doctest/doctest.h>

#include <hp/Guid.hpp>

#include <set>
#include <unordered_map>
#include <unordered_set>

TEST_CASE("a default GUID is null and invalid") {
    // Zero-is-invalid is what makes a memset'd struct safe: it must not look
    // like it references a real asset.
    constexpr hp::Guid nullGuid;
    CHECK(nullGuid.value() == 0);
    CHECK_FALSE(nullGuid.isValid());
    CHECK_FALSE(static_cast<bool>(nullGuid));
    CHECK(nullGuid == hp::Guid());
}

TEST_CASE("generated GUIDs are valid and distinct") {
    const hp::Guid a = hp::Guid::generate();
    const hp::Guid b = hp::Guid::generate();
    CHECK(a.isValid());
    CHECK(b.isValid());
    CHECK(a != b);
}

TEST_CASE("a large batch of GUIDs has no collisions") {
    // 100k of a 64-bit space: the birthday probability of any collision is
    // about 2.7e-10, so a failure here means the generator is broken, not
    // unlucky. This is the property the whole type rests on.
    constexpr int kCount = 100000;
    std::unordered_set<hp::Guid> seen;
    seen.reserve(kCount);
    for (int i = 0; i < kCount; ++i) {
        const hp::Guid guid = hp::Guid::generate();
        REQUIRE(guid.isValid());
        REQUIRE(seen.insert(guid).second);
    }
    CHECK(seen.size() == kCount);
}

TEST_CASE("text round-trips exactly") {
    for (int i = 0; i < 1000; ++i) {
        const hp::Guid original = hp::Guid::generate();
        const std::string text = original.toString();
        REQUIRE(text.size() == 16);
        const auto parsed = hp::Guid::parse(text);
        REQUIRE(parsed.has_value());
        CHECK(*parsed == original);
    }
}

TEST_CASE("formatting is fixed width and lowercase") {
    // Fixed width means lexicographic order matches numeric order, and a
    // truncated value is visible rather than parsing as a different valid GUID.
    CHECK(hp::Guid(1).toString() == "0000000000000001");
    CHECK(hp::Guid(0xDEADBEEF).toString() == "00000000deadbeef");
    CHECK(hp::Guid(0xFFFFFFFFFFFFFFFFull).toString() == "ffffffffffffffff");
    CHECK(hp::Guid().toString() == "0000000000000000");
}

TEST_CASE("parsing rejects anything that is not exactly sixteen hex digits") {
    // Strict because this parses data files: a lenient parser turns a corrupt
    // scene into a silently wrong asset reference.
    CHECK_FALSE(hp::Guid::parse("").has_value());
    CHECK_FALSE(hp::Guid::parse("1").has_value());
    CHECK_FALSE(hp::Guid::parse("000000000000000").has_value());   // 15
    CHECK_FALSE(hp::Guid::parse("00000000000000000").has_value()); // 17
    CHECK_FALSE(hp::Guid::parse("0x00000000000001").has_value());
    CHECK_FALSE(hp::Guid::parse(" 000000000000001").has_value());
    CHECK_FALSE(hp::Guid::parse("000000000000001 ").has_value());
    CHECK_FALSE(hp::Guid::parse("00000000000000zz").has_value());

    // Uppercase is accepted on read though never produced, so a hand-edited
    // file works.
    const auto upper = hp::Guid::parse("00000000DEADBEEF");
    REQUIRE(upper.has_value());
    CHECK(*upper == hp::Guid(0xDEADBEEF));
}

TEST_CASE("usable as a key in ordered and unordered containers") {
    std::unordered_map<hp::Guid, int> byGuid;
    std::set<hp::Guid> ordered;

    const hp::Guid a = hp::Guid::generate();
    const hp::Guid b = hp::Guid::generate();
    byGuid[a] = 1;
    byGuid[b] = 2;
    ordered.insert(a);
    ordered.insert(b);

    CHECK(byGuid.at(a) == 1);
    CHECK(byGuid.at(b) == 2);
    CHECK(ordered.size() == 2);
    CHECK((*ordered.begin() < *std::next(ordered.begin())));
}
