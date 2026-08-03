//! CMakeCache.txt parsing for the build harness.
//!
//! Extracted from `build.zig` so it can be unit tested (T0012). `build.zig`
//! imports this module; `tests/harness/cache_test.zig` imports the same file
//! and exercises it directly.
//!
//! This is deliberately a pure function over a string: no filesystem, no
//! allocation. The one bug this logic has actually had (see `cacheHas`) would
//! have been caught instantly by a unit test, which is most of why the harness
//! exists at all.

const std = @import("std");

/// Look up `name` in a CMakeCache.txt and report whether it equals `want`.
///
/// Entries are `NAME:TYPE=VALUE`, and the type is not predictable: a value
/// passed as `-DHP_ZIG=...` without a declared type lands as `UNINITIALIZED`,
/// not `FILEPATH`. Matching on the name alone avoids depending on that.
///
/// Getting this wrong is not loud. An over-strict match reports "the cache
/// disagrees" on every run, so CMake reconfigures every build and the
/// incremental build quietly stops being incremental -- which is exactly what
/// happened when this matched `NAME:FILEPATH=` literally.
pub fn cacheHas(cache: []const u8, name: []const u8, want: []const u8) bool {
    var lines = std.mem.splitScalar(u8, cache, '\n');
    while (lines.next()) |raw| {
        // CMakeCache.txt written on Windows has CRLF line endings; the harness
        // reads the same file from either host.
        const line = std.mem.trimEnd(u8, raw, "\r");
        if (!std.mem.startsWith(u8, line, name)) continue;
        // Guard against a prefix match: `HP_ZIG_LIB_DIR` must not answer for
        // `HP_ZIG`. The character after the name has to be the type separator.
        if (line.len <= name.len or line[name.len] != ':') continue;
        const eq = std.mem.indexOfScalarPos(u8, line, name.len, '=') orelse continue;
        return std.mem.eql(u8, line[eq + 1 ..], want);
    }
    return false;
}
