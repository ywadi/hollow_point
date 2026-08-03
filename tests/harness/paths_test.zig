//! Unit tests for the layout of `.harness/`.
//!
//! Bucket: fast. Pure string work -- no filesystem, no subprocesses.
//!
//! These exist because of T0102. `bootstrap.sh` and `bootstrap.ps1` both
//! installed into `.harness/<tool>/<version>/` with no host discriminator, and
//! both deleted the destination before extracting, so on a machine that runs
//! both -- a Windows box with WSL, which is how this project is developed --
//! running one bootstrap silently destroyed the other's toolchain. The layout
//! is now host-keyed, and "two hosts never share a directory" is the property
//! worth pinning down in a test rather than in a comment.

const std = @import("std");
const testing = std.testing;

// Named import, not a relative path: a relative `@import` may not escape the
// module root, so `build.zig` hands this file the module explicitly.
const paths = @import("harness_paths");

test "the host key uses the project's <os>-<arch> vocabulary" {
    // Deliberately the same spelling as the target keys in build.zig's `specs`
    // and the directory names under build/ and dist/. One vocabulary for
    // "which machine is this", not two.
    try testing.expectEqualStrings("linux-x86_64", paths.hostKey(.linux, .x86_64).?);
    try testing.expectEqualStrings("windows-x86_64", paths.hostKey(.windows, .x86_64).?);
    try testing.expectEqualStrings("linux-aarch64", paths.hostKey(.linux, .aarch64).?);
}

test "an unsupported host has no key rather than a wrong one" {
    // Neither bootstrap script supports these, so the harness must not invent a
    // path that no script would ever populate. Falling back to PATH is the
    // documented behaviour; silently pointing at an empty directory is not.
    try testing.expect(paths.hostKey(.macos, .aarch64) == null);
    try testing.expect(paths.hostKey(.windows, .aarch64) == null);
}

test "two hosts never share a tool directory" {
    // The bug itself. If this ever passes by accident again, one host's
    // bootstrap is about to delete the other's toolchain.
    const a = testing.allocator;
    for ([_][]const u8{ "zig", "cmake", "ninja" }) |tool| {
        const linux = try paths.toolDir(a, tool, "linux-x86_64", "1.2.3");
        defer a.free(linux);
        const windows = try paths.toolDir(a, tool, "windows-x86_64", "1.2.3");
        defer a.free(windows);

        try testing.expect(!std.mem.eql(u8, linux, windows));
    }
}

test "a tool directory is keyed by host first, then version" {
    const a = testing.allocator;

    const zig = try paths.toolDir(a, "zig", "linux-x86_64", "0.16.0");
    defer a.free(zig);
    try testing.expectEqualStrings(".harness/zig/linux-x86_64/0.16.0", zig);

    const cmake = try paths.toolDir(a, "cmake", "windows-x86_64", "3.31.12");
    defer a.free(cmake);
    try testing.expectEqualStrings(".harness/cmake/windows-x86_64/3.31.12", cmake);
}

test "the same host and tool at two versions do not collide either" {
    // Host-keying must not have cost us the property the old layout did have.
    const a = testing.allocator;
    const old = try paths.toolDir(a, "cmake", "linux-x86_64", "3.31.12");
    defer a.free(old);
    const new = try paths.toolDir(a, "cmake", "linux-x86_64", "4.0.0");
    defer a.free(new);

    try testing.expect(!std.mem.eql(u8, old, new));
}

test "the legacy directory is the pre-T0102 shared path" {
    // Only ever read, never installed into -- it is what the harness and the
    // bootstrap scripts look for so they can tell you a stale toolchain is
    // sitting there. Getting this string wrong makes the warning silent.
    const a = testing.allocator;
    const legacy = try paths.legacyToolDir(a, "zig", "0.16.0");
    defer a.free(legacy);
    try testing.expectEqualStrings(".harness/zig/0.16.0", legacy);

    const current = try paths.toolDir(a, "zig", "linux-x86_64", "0.16.0");
    defer a.free(current);
    try testing.expect(!std.mem.eql(u8, legacy, current));
}
