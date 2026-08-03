//! Unit tests for the harness's CMakeCache parsing.
//!
//! Bucket: fast. No filesystem, no subprocesses -- these are microseconds.

const std = @import("std");
const testing = std.testing;

// Named import, not a relative path: a relative `@import` may not escape the
// module root, so `build.zig` hands this file the module explicitly.
const cacheHas = @import("harness_cache").cacheHas;

const sample =
    \\# This is the CMakeCache file.
    \\CMAKE_BUILD_TYPE:STRING=Release
    \\HP_ZIG:UNINITIALIZED=/opt/zig/zig
    \\HP_ZIG_LIB_DIR:PATH=/opt/zig/lib
    \\CMAKE_MAKE_PROGRAM:FILEPATH=/usr/bin/ninja
    \\EMPTY_VALUE:STRING=
    \\
;

test "matches a value regardless of the declared type" {
    // The bug this whole module exists to prevent: a value passed as
    // -DHP_ZIG=... lands as UNINITIALIZED, not FILEPATH. Matching the type
    // as well as the name made this return false forever, which silently
    // reconfigured CMake on every build.
    try testing.expect(cacheHas(sample, "HP_ZIG", "/opt/zig/zig"));
    try testing.expect(cacheHas(sample, "CMAKE_MAKE_PROGRAM", "/usr/bin/ninja"));
    try testing.expect(cacheHas(sample, "CMAKE_BUILD_TYPE", "Release"));
}

test "reports a mismatch when the value differs" {
    try testing.expect(!cacheHas(sample, "HP_ZIG", "/usr/bin/zig"));
    try testing.expect(!cacheHas(sample, "CMAKE_BUILD_TYPE", "Debug"));
}

test "a longer name is not matched by a shorter prefix" {
    // HP_ZIG_LIB_DIR starts with HP_ZIG. Without the ':' guard, looking up
    // HP_ZIG would find the LIB_DIR line first and compare the wrong value.
    try testing.expect(cacheHas(sample, "HP_ZIG_LIB_DIR", "/opt/zig/lib"));
    try testing.expect(!cacheHas(sample, "HP_ZIG", "/opt/zig/lib"));
}

test "a missing name is false, not an error" {
    try testing.expect(!cacheHas(sample, "NOT_PRESENT", "anything"));
    try testing.expect(!cacheHas("", "HP_ZIG", "/opt/zig/zig"));
}

test "CRLF line endings are tolerated" {
    // A cache written by a Windows-host configure. The harness reads the same
    // tree from either host, so both endings have to parse (T0004).
    const crlf = "CMAKE_BUILD_TYPE:STRING=Release\r\nHP_ZIG:UNINITIALIZED=C:/zig/zig.exe\r\n";
    try testing.expect(cacheHas(crlf, "HP_ZIG", "C:/zig/zig.exe"));
    try testing.expect(cacheHas(crlf, "CMAKE_BUILD_TYPE", "Release"));
    // Without the trimEnd the value would carry a trailing \r and never match.
    try testing.expect(!cacheHas(crlf, "CMAKE_BUILD_TYPE", "Release\r"));
}

test "an empty value matches an empty want" {
    try testing.expect(cacheHas(sample, "EMPTY_VALUE", ""));
    try testing.expect(!cacheHas(sample, "EMPTY_VALUE", "x"));
}

test "a value containing '=' keeps everything after the first one" {
    // Compiler launchers and flags routinely carry '='; splitting on the last
    // one, or on every one, would truncate the value.
    const eq = "CMAKE_CXX_FLAGS:STRING=-DFOO=1 -DBAR=2\n";
    try testing.expect(cacheHas(eq, "CMAKE_CXX_FLAGS", "-DFOO=1 -DBAR=2"));
}
