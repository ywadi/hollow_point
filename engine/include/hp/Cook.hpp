// The binary cache derived from YAML (T0020.4, T0020.5).
//
// **The binary form is a cache and must always be safely discardable.** If it is
// missing, stale, truncated, from a future version or outright corrupt, the
// correct behaviour is to re-cook from the YAML — never to fail. That property
// is not a nicety: it is what makes the format safe to ship (Phase 8 export) and
// safe to delete, and it is the reason every reader below returns "no" rather
// than throwing.
//
// **Staleness is decided by content hash, never by mtime.** Timestamps lie after
// a git checkout, a file copy, a clock change, or a CI cache restore — this
// repository has a whole ticket about the last one (T0131). A stale binary that
// loads without error is a genuinely nasty class of bug, because everything
// works and the data is silently old.
//
// **The format is versioned from its first commit**, because a binary with no
// version field cannot be migrated, only discarded. T0082.5 requires the schema
// version to participate in staleness alongside the content hash, so a schema
// change cannot silently load a cook that still happens to parse.
//
// ---
//
// Layout, little-endian throughout:
//
// ```text
//   magic        8 bytes   "HPCOOK\0\0"
//   format       u32       kCookFormatVersion, this file's own layout
//   schema       u32       caller's schema version (T0082)
//   sourceHash   u64       FNV-1a of the YAML this was cooked from
//   payloadSize  u64       bytes that follow
//   payload      ...       the cooked bytes
// ```
//
// **Little-endian is chosen, not assumed.** Both targets are x86-64 so it costs
// nothing today, and writing it down means a future big-endian target is a
// conversion in one place rather than a silent corruption everywhere.
#pragma once

#include <hp/Api.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace hp {

/// The version of the container layout described above.
///
/// Bumped when the *header* changes, which is separate from a caller's own
/// schema version. A reader refuses anything it does not recognise rather than
/// guessing.
inline constexpr std::uint32_t kCookFormatVersion = 1;

/// What a cooked file says about itself.
struct CookHeader {
    /// The container layout version this file was written with.
    std::uint32_t formatVersion = 0;

    /// The caller's schema version (T0082).
    std::uint32_t schemaVersion = 0;

    /// Hash of the YAML source it was cooked from.
    std::uint64_t sourceHash = 0;

    /// Size of the payload in bytes.
    std::uint64_t payloadSize = 0;
};

/// Why a cooked file could not be used.
///
/// Every one of these means "re-cook", and none of them is an error a user
/// should ever see. They are distinguished only so a diagnostic can say *why* a
/// cache missed, which is the difference between debugging a stale-cache problem
/// in minutes and in an afternoon.
enum class CookStatus : std::uint8_t {
    /// Usable.
    Ok,

    /// Too short to contain a header, or the magic does not match. Also what an
    /// empty or truncated file looks like.
    NotACookFile,

    /// Written by a different container version.
    FormatMismatch,

    /// Written against a different schema version (T0082.5).
    SchemaMismatch,

    /// The source YAML has changed since this was cooked.
    SourceChanged,

    /// The header is well-formed but the payload is shorter than it claims.
    Truncated,
};

/// Hashes YAML source for staleness comparison.
///
/// FNV-1a, 64-bit. **Not a cryptographic hash and not trying to be** — this
/// detects accidental change, not tampering, and a caller that needs the latter
/// has a different problem. Chosen over a stronger hash because it is a few
/// lines with no dependency and is fast enough to run on every load.
/// @param text the YAML source.
/// @returns the hash.
[[nodiscard]] HP_API std::uint64_t hashSource(std::string_view text);

/// Wraps a payload in a cook container.
///
/// @param payload the cooked bytes.
/// @param sourceHash `hashSource` of the YAML this came from.
/// @param schemaVersion the caller's schema version (T0082).
/// @returns the complete file contents, ready to write through the VFS.
[[nodiscard]] HP_API std::vector<std::byte> writeCook(const std::vector<std::byte>& payload,
                                                      std::uint64_t sourceHash,
                                                      std::uint32_t schemaVersion);

/// Reads a cook container's header without copying the payload.
/// @param bytes the file contents.
/// @param outHeader receives the header when the result is not `NotACookFile`.
/// @returns why the file is unusable, or `Ok`. Does **not** check the source
///          hash or schema — `readCook` does that, because those need the
///          caller's expectations.
[[nodiscard]] HP_API CookStatus readCookHeader(const std::vector<std::byte>& bytes,
                                               CookHeader& outHeader);

/// Reads and validates a cooked file.
///
/// @param bytes the file contents.
/// @param expectedSourceHash `hashSource` of the YAML available now. A mismatch
///        yields `SourceChanged` — **this is the staleness check**, and it is
///        why nothing here looks at a timestamp.
/// @param expectedSchemaVersion the schema the caller can read (T0082.5).
/// @param outPayload receives the payload when the result is `Ok`, and is left
///        untouched otherwise.
/// @returns `Ok`, or the reason to re-cook. **Never an error to propagate**: a
///          caller's response to every non-`Ok` value is the same, which is to
///          cook again from YAML.
[[nodiscard]] HP_API CookStatus readCook(const std::vector<std::byte>& bytes,
                                         std::uint64_t expectedSourceHash,
                                         std::uint32_t expectedSchemaVersion,
                                         std::vector<std::byte>& outPayload);

/// @param status a cook status.
/// @returns a short human-readable reason, for logs.
[[nodiscard]] HP_API const char* describe(CookStatus status);

/// Appends a value to a byte buffer in the cook format's byte order.
///
/// Exposed because payload writers need the same primitives the container uses,
/// and two implementations of "write a u32" is exactly how an endianness
/// decision gets made twice and differently.
/// @param out the buffer to append to.
/// @param value the value to write.
/// @returns nothing.
HP_API void writeU32(std::vector<std::byte>& out, std::uint32_t value);

/// Appends a 64-bit value in the cook format's byte order.
/// @param out the buffer to append to.
/// @param value the value to write.
/// @returns nothing.
HP_API void writeU64(std::vector<std::byte>& out, std::uint64_t value);

/// Appends a length-prefixed string.
/// @param out the buffer to append to.
/// @param text the text to write.
/// @returns nothing.
HP_API void writeString(std::vector<std::byte>& out, std::string_view text);

/// Reads a 32-bit value, advancing the cursor.
/// @param bytes the buffer.
/// @param cursor the read position, advanced on success.
/// @param out receives the value.
/// @returns false when there are not enough bytes left, leaving `out` and
///          `cursor` untouched.
[[nodiscard]] HP_API bool readU32(const std::vector<std::byte>& bytes, std::size_t& cursor,
                                  std::uint32_t& out);

/// Reads a 64-bit value, advancing the cursor.
/// @param bytes the buffer.
/// @param cursor the read position, advanced on success.
/// @param out receives the value.
/// @returns false when there are not enough bytes left.
[[nodiscard]] HP_API bool readU64(const std::vector<std::byte>& bytes, std::size_t& cursor,
                                  std::uint64_t& out);

/// Reads a length-prefixed string, advancing the cursor.
///
/// **Refuses a length that exceeds the remaining bytes** rather than trusting
/// it, because the input is a file on disk and a corrupt length is the classic
/// way a deserializer allocates four gigabytes or reads past its buffer.
/// @param bytes the buffer.
/// @param cursor the read position, advanced on success.
/// @param out receives the text.
/// @returns whether a complete string was read.
[[nodiscard]] HP_API bool readString(const std::vector<std::byte>& bytes, std::size_t& cursor,
                                     std::string& out);

} // namespace hp
