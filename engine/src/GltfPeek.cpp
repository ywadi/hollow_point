// See GltfPeek.hpp. The GLB layout these functions slice is glTF 2.0's
// "Binary glTF Layout": a 12-byte header (magic `glTF`, version, total
// length), then chunks of `{uint32 length, uint32 type, bytes}`, 4-byte
// aligned, first chunk `JSON`, optional second chunk `BIN`.

#include "GltfPeek.hpp"

#include <cstdint>
#include <cstring>

namespace hp {
namespace {

constexpr std::uint32_t kGlbMagic = 0x46546C67U;  // 'glTF'
constexpr std::uint32_t kJsonChunk = 0x4E4F534AU; // 'JSON'
constexpr std::uint32_t kBinChunk = 0x004E4942U;  // 'BIN\0'

std::uint32_t readU32(const std::byte* at) {
    std::uint32_t value = 0;
    std::memcpy(&value, at, 4);
    return value;
}

} // namespace

std::string_view gltfJsonOf(const std::vector<std::byte>& bytes) {
    const std::byte* data = bytes.data();
    if (bytes.size() < 4 || readU32(data) != kGlbMagic) {
        return {reinterpret_cast<const char*>(data), bytes.size()};
    }
    if (bytes.size() < 20) {
        return {};
    }
    const std::uint32_t chunkLength = readU32(data + 12);
    if (readU32(data + 16) != kJsonChunk || chunkLength > bytes.size() - 20) {
        return {};
    }
    return {reinterpret_cast<const char*>(data) + 20, chunkLength};
}

std::string_view gltfBinChunkOf(const std::vector<std::byte>& bytes) {
    const std::byte* data = bytes.data();
    if (bytes.size() < 20 || readU32(data) != kGlbMagic) {
        return {};
    }
    const std::uint32_t jsonLength = readU32(data + 12);
    if (readU32(data + 16) != kJsonChunk || jsonLength > bytes.size() - 20) {
        return {};
    }
    // The JSON chunk is 4-byte aligned by the spec; the writer pads it, so
    // rounding up here is reading the format, not repairing it.
    std::size_t offset = 20 + jsonLength;
    offset = (offset + 3U) & ~static_cast<std::size_t>(3U);
    if (offset + 8 > bytes.size()) {
        return {};
    }
    const std::uint32_t binLength = readU32(data + offset);
    if (readU32(data + offset + 4) != kBinChunk || binLength > bytes.size() - offset - 8) {
        return {};
    }
    return {reinterpret_cast<const char*>(data) + offset + 8, binLength};
}

} // namespace hp
