#include <hp/Cook.hpp>

#include <hp/Profiling.hpp>

#include <cstring>
#include <string>

namespace hp {
namespace {

constexpr std::size_t kMagicSize = 8;
constexpr char kMagic[kMagicSize] = {'H', 'P', 'C', 'O', 'O', 'K', '\0', '\0'};

/// Header size: magic + format + schema + hash + payload size.
constexpr std::size_t kHeaderSize = kMagicSize + 4 + 4 + 8 + 8;

} // namespace

std::uint64_t hashSource(std::string_view text) {
    // FNV-1a, 64-bit. The constants are the standard ones and are written out
    // rather than named, because a "clever" variant here would silently
    // invalidate every cooked file in existence.
    std::uint64_t hash = 1469598103934665603ULL;
    for (const char ch : text) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(ch));
        hash *= 1099511628211ULL;
    }
    return hash;
}

void writeU32(std::vector<std::byte>& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
}

void writeU64(std::vector<std::byte>& out, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
}

void writeString(std::vector<std::byte>& out, std::string_view text) {
    writeU64(out, static_cast<std::uint64_t>(text.size()));
    const auto* first = reinterpret_cast<const std::byte*>(text.data());
    out.insert(out.end(), first, first + text.size());
}

bool readU32(const std::vector<std::byte>& bytes, std::size_t& cursor, std::uint32_t& out) {
    if (cursor + 4 > bytes.size()) {
        return false;
    }
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(bytes[cursor + static_cast<std::size_t>(i)]) << (i * 8);
    }
    cursor += 4;
    out = value;
    return true;
}

bool readU64(const std::vector<std::byte>& bytes, std::size_t& cursor, std::uint64_t& out) {
    if (cursor + 8 > bytes.size()) {
        return false;
    }
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(bytes[cursor + static_cast<std::size_t>(i)]) << (i * 8);
    }
    cursor += 8;
    out = value;
    return true;
}

bool readString(const std::vector<std::byte>& bytes, std::size_t& cursor, std::string& out) {
    std::size_t probe = cursor;
    std::uint64_t length = 0;
    if (!readU64(bytes, probe, length)) {
        return false;
    }
    // Checked against what is actually left, not merely for being "reasonable".
    // The input is a file on disk and a corrupt length is the classic way a
    // deserializer either allocates gigabytes or walks off the end of its
    // buffer -- and both look like a crash somewhere unrelated.
    if (length > bytes.size() - probe) {
        return false;
    }
    out.assign(reinterpret_cast<const char*>(bytes.data() + probe),
               static_cast<std::size_t>(length));
    cursor = probe + static_cast<std::size_t>(length);
    return true;
}

std::vector<std::byte> writeCook(const std::vector<std::byte>& payload, std::uint64_t sourceHash,
                                 std::uint32_t schemaVersion) {
    HP_PROFILE_ZONE();

    std::vector<std::byte> out;
    out.reserve(kHeaderSize + payload.size());
    for (const char ch : kMagic) {
        out.push_back(static_cast<std::byte>(ch));
    }
    writeU32(out, kCookFormatVersion);
    writeU32(out, schemaVersion);
    writeU64(out, sourceHash);
    writeU64(out, static_cast<std::uint64_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

CookStatus readCookHeader(const std::vector<std::byte>& bytes, CookHeader& outHeader) {
    if (bytes.size() < kHeaderSize) {
        return CookStatus::NotACookFile;
    }
    for (std::size_t i = 0; i < kMagicSize; ++i) {
        if (static_cast<char>(bytes[i]) != kMagic[i]) {
            return CookStatus::NotACookFile;
        }
    }

    std::size_t cursor = kMagicSize;
    CookHeader header;
    if (!readU32(bytes, cursor, header.formatVersion) || !readU32(bytes, cursor, header.schemaVersion)
        || !readU64(bytes, cursor, header.sourceHash)
        || !readU64(bytes, cursor, header.payloadSize)) {
        return CookStatus::NotACookFile;
    }
    outHeader = header;

    if (header.formatVersion != kCookFormatVersion) {
        return CookStatus::FormatMismatch;
    }
    return CookStatus::Ok;
}

CookStatus readCook(const std::vector<std::byte>& bytes, std::uint64_t expectedSourceHash,
                    std::uint32_t expectedSchemaVersion, std::vector<std::byte>& outPayload) {
    HP_PROFILE_ZONE();

    CookHeader header;
    const CookStatus headerStatus = readCookHeader(bytes, header);
    if (headerStatus != CookStatus::Ok) {
        return headerStatus;
    }

    // Schema before source hash, deliberately. Both mean "re-cook", but a schema
    // mismatch is the more informative answer when both are true -- the source
    // hash almost certainly changed *because* the schema did.
    if (header.schemaVersion != expectedSchemaVersion) {
        return CookStatus::SchemaMismatch;
    }
    if (header.sourceHash != expectedSourceHash) {
        return CookStatus::SourceChanged;
    }
    if (bytes.size() - kHeaderSize < header.payloadSize) {
        return CookStatus::Truncated;
    }

    const auto* first = bytes.data() + kHeaderSize;
    outPayload.assign(first, first + static_cast<std::size_t>(header.payloadSize));
    return CookStatus::Ok;
}

const char* describe(CookStatus status) {
    switch (status) {
    case CookStatus::Ok:
        return "ok";
    case CookStatus::NotACookFile:
        return "not a cooked file (bad magic, or too short)";
    case CookStatus::FormatMismatch:
        return "cooked by a different container version";
    case CookStatus::SchemaMismatch:
        return "cooked against a different schema version";
    case CookStatus::SourceChanged:
        return "the source YAML has changed";
    case CookStatus::Truncated:
        return "payload is shorter than the header claims";
    }
    return "unknown";
}

} // namespace hp
