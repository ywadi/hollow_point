// Where the running program is (T0048).
//
// One function, and it exists because three places had already hand-rolled it:
// two test suites and, without it, both apps. The duplication is not the only
// argument — every copy has to independently remember that the *working
// directory* is not the answer. The harness runs a binary from wherever it
// likes, and under wine or WSL interop the cwd is not even reliably the same
// shape of path. An executable can always find itself.
//
// **This is a platform query, not content addressing.** Anything that resolves
// game content — packs, patches, mounts — belongs to the virtual filesystem
// (T0103, D13), which is a different mechanism with different rules. This is
// only "where am I", which is what a loader needs before a VFS exists.
#pragma once

#include <hp/Api.hpp>

#include <string>

namespace hp {

/// Directory containing the running executable, without a trailing separator.
///
/// @returns the absolute directory, or "." if the platform would not say —
///          which is a degraded answer rather than an error, because every
///          caller is composing a path and "." keeps that working when the
///          binary is run from its own directory.
[[nodiscard]] HP_API std::string executableDirectory();

} // namespace hp
