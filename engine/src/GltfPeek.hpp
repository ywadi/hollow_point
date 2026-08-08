// Internal: raw glTF container peeks, shared by `AssetImport.cpp` (T0168.6's
// required-extension warning) and `Import.cpp` (T0169's sub-asset extraction).
//
// Both callers need the document *before or without* the loader: the warning
// because tinygltf refuses a compressed file before any load callback runs
// (measured on pirate.glb), the extraction because it is deviceless on
// purpose — identity and file production must be testable in the fast suite,
// and Diligent's model only exists on a device.
//
// Deliberately not a parser: these slice the container; `nlohmann::json` does
// the reading, in the callers.

#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace hp {

/// The document's JSON, whether the container is binary or text.
///
/// A GLB is a 12-byte header (`glTF`, version, length) and chunks, the first
/// of which must be `JSON`; anything that does not start with the magic is
/// taken to be a `.gltf`, which *is* the JSON. Returns empty on a malformed
/// container rather than guessing — callers treat empty as "nothing to say"
/// and leave the real diagnosis to the loader.
[[nodiscard]] std::string_view gltfJsonOf(const std::vector<std::byte>& bytes);

/// The GLB's binary chunk (`BIN`), where embedded images and buffers live.
///
/// Empty for a text `.gltf` (whose buffers are URIs), for a GLB without a
/// second chunk, and for anything malformed — same contract as `gltfJsonOf`.
[[nodiscard]] std::string_view gltfBinChunkOf(const std::vector<std::byte>& bytes);

} // namespace hp
