// Stable identity (T0016).
//
// A GUID names a thing -- an asset, an entity, a prefab -- in a way that
// survives being written to disk, reloaded, moved between machines and renamed.
// A pointer does not, an index does not, and a file path stops being true the
// moment someone reorganises a folder.
//
// **64-bit, not a 128-bit RFC-4122 UUID.** That is deliberate: this is an
// identifier for one project's content, not a globally unique identifier for
// the internet. Half the storage in every asset reference, every scene file and
// every map key, and the collision risk is negligible at the scale involved --
// with 100,000 assets the probability of any collision is about 2.7e-10. It also
// means UUID libraries do not apply; this is not the same type, so vendoring one
// would give us the wrong thing at twice the size.
//
// The corollary is that this is *not* suitable as a cryptographic or
// cross-organisation identifier, and nothing should treat it as unguessable.
#pragma once

#include <hp/Api.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace hp {

class Guid {
public:
    /// The null GUID. Deliberately zero, so a default-constructed or
    /// zero-initialised GUID is invalid rather than accidentally meaningful --
    /// a memset'd struct must not look like it references a real asset.
    constexpr Guid() = default;

    constexpr explicit Guid(std::uint64_t value) : value_(value) {}

    /// A new random GUID. Thread-safe; never returns the null GUID.
    static HP_API Guid generate();

    constexpr std::uint64_t value() const { return value_; }

    constexpr bool isValid() const { return value_ != 0; }

    constexpr explicit operator bool() const { return isValid(); }

    friend constexpr bool operator==(Guid a, Guid b) { return a.value_ == b.value_; }

    friend constexpr bool operator!=(Guid a, Guid b) { return a.value_ != b.value_; }

    /// Ordering exists so a GUID can key a sorted container. It is arbitrary and
    /// carries no meaning -- do not present it to a user as an order.
    friend constexpr bool operator<(Guid a, Guid b) { return a.value_ < b.value_; }

    /// 16 lowercase hex digits, zero-padded, no separators or prefix.
    ///
    /// Fixed width on purpose: it sorts lexicographically the same way it sorts
    /// numerically, and a truncated or corrupted value is visible on sight
    /// rather than parsing as a different valid GUID.
    HP_API std::string toString() const;

    /// Parses `toString`'s output. Strict -- exactly 16 hex digits, nothing else,
    /// no whitespace, no `0x`.
    ///
    /// Strict because this parses *data files*. A lenient parser that accepts
    /// "0x1234" and " 1234 " turns a corrupt scene file into a silently wrong
    /// asset reference, which surfaces much later as a missing mesh.
    static HP_API std::optional<Guid> parse(std::string_view text);

private:
    std::uint64_t value_ = 0;
};

} // namespace hp

template <>
struct std::hash<hp::Guid> {
    std::size_t operator()(hp::Guid guid) const noexcept {
        // The value is already random, so it is its own hash. Passing it through
        // std::hash<uint64_t> would be an identity on every implementation that
        // matters and costs a function call in debug builds.
        return static_cast<std::size_t>(guid.value());
    }
};
