#include <hp/Guid.hpp>

#include <array>
#include <random>

namespace hp {
namespace {

/// Per-thread generator.
///
/// `thread_local` rather than a shared generator behind a mutex: GUID
/// generation happens during asset import and scene loading, which are exactly
/// the things that will be parallelised (T0026), and a global lock there would
/// be a contention point for no benefit.
///
/// Each thread seeds from `std::random_device` with a full seed sequence rather
/// than a single 32-bit value. Seeding a 64-bit Mersenne twister from one
/// `random_device()` call gives it 32 bits of entropy, which means two threads
/// starting in the same second on a platform with a weak `random_device` can
/// produce identical streams -- and identical asset GUIDs.
std::mt19937_64& generator() {
    thread_local std::mt19937_64 engine = [] {
        std::random_device device;
        std::array<std::uint32_t, 8> seedData{};
        for (auto& word : seedData) {
            word = device();
        }
        std::seed_seq sequence(seedData.begin(), seedData.end());
        return std::mt19937_64(sequence);
    }();
    return engine;
}

constexpr char kHexDigits[] = "0123456789abcdef";

} // namespace

Guid Guid::generate() {
    std::uniform_int_distribution<std::uint64_t> distribution;
    std::uint64_t value = distribution(generator());
    // Zero is the null GUID. Astronomically unlikely, but "astronomically
    // unlikely" is how you get a bug that appears once and cannot be reproduced.
    while (value == 0) {
        value = distribution(generator());
    }
    return Guid(value);
}

std::string Guid::toString() const {
    std::string text(16, '0');
    for (int i = 15; i >= 0; --i) {
        text[static_cast<std::size_t>(i)] = kHexDigits[(value_ >> ((15 - i) * 4)) & 0xF];
    }
    return text;
}

std::optional<Guid> Guid::parse(std::string_view text) {
    if (text.size() != 16) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (const char c : text) {
        std::uint64_t digit = 0;
        if (c >= '0' && c <= '9') {
            digit = static_cast<std::uint64_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = static_cast<std::uint64_t>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            // Accepted on read, never produced on write. Being liberal about
            // case costs nothing and makes a hand-edited file work; being
            // liberal about *length* or whitespace would not, which is why
            // those are still rejected.
            digit = static_cast<std::uint64_t>(c - 'A' + 10);
        } else {
            return std::nullopt;
        }
        value = (value << 4) | digit;
    }
    return Guid(value);
}

} // namespace hp
