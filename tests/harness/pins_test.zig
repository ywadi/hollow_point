//! The pinned versions and the `.harness/` layout live in three places at once.
//!
//! Bucket: fast. Three small file reads, no subprocesses.
//!
//! `bootstrap.sh`, `bootstrap.ps1` and `build.zig` each carry their own copy of
//! the pinned Zig, CMake and Ninja versions, and each spells out where a tool
//! gets installed. Nothing makes them agree -- a shell script, a PowerShell
//! script and a Zig file have no way to share a constant, and the sensible
//! single-pins-file fix has to be readable by all three, which is its own small
//! problem. Until that exists, this test is what notices.
//!
//! The layout half exists specifically to stop T0102 coming back. Losing the
//! host key does not fail a build: it silently returns to the arrangement where
//! a Windows bootstrap and a WSL bootstrap delete each other's toolchain, which
//! is exactly the kind of regression that goes unnoticed until it costs someone
//! a re-download mid-task.

const std = @import("std");
const testing = std.testing;
const io = testing.io;
const config = @import("test_config");

fn readScript(gpa: std.mem.Allocator, name: []const u8) ![]u8 {
    const path = try std.fs.path.join(gpa, &.{ config.repo_root, name });
    defer gpa.free(path);
    return std.Io.Dir.cwd().readFileAlloc(io, path, gpa, .limited(256 * 1024));
}

/// Value of a `NAME=value` assignment at the start of a line in a shell script.
fn shValue(src: []const u8, name: []const u8) ?[]const u8 {
    var lines = std.mem.splitScalar(u8, src, '\n');
    while (lines.next()) |line| {
        const trimmed = std.mem.trimEnd(u8, line, "\r");
        if (!std.mem.startsWith(u8, trimmed, name)) continue;
        if (trimmed.len <= name.len or trimmed[name.len] != '=') continue;
        return trimmed[name.len + 1 ..];
    }
    return null;
}

/// Value of a `$Name = 'value'` assignment in a PowerShell script.
fn psValue(src: []const u8, name: []const u8) ?[]const u8 {
    var lines = std.mem.splitScalar(u8, src, '\n');
    while (lines.next()) |line| {
        const trimmed = std.mem.trimEnd(u8, line, "\r");
        if (!std.mem.startsWith(u8, trimmed, name)) continue;
        const eq = std.mem.indexOfScalar(u8, trimmed, '=') orelse continue;
        return std.mem.trim(u8, trimmed[eq + 1 ..], " '\"");
    }
    return null;
}

test "bootstrap.sh pins the versions build.zig expects" {
    const gpa = testing.allocator;
    const src = try readScript(gpa, "bootstrap.sh");
    defer gpa.free(src);

    try testing.expectEqualStrings(config.pinned_zig, shValue(src, "ZIG_VERSION").?);
    try testing.expectEqualStrings(config.pinned_cmake, shValue(src, "CMAKE_VERSION").?);
    try testing.expectEqualStrings(config.pinned_ninja, shValue(src, "NINJA_VERSION").?);
}

test "bootstrap.ps1 pins the versions build.zig expects" {
    const gpa = testing.allocator;
    const src = try readScript(gpa, "bootstrap.ps1");
    defer gpa.free(src);

    try testing.expectEqualStrings(config.pinned_zig, psValue(src, "$ZigVersion").?);
    try testing.expectEqualStrings(config.pinned_cmake, psValue(src, "$CMakeVersion").?);
    try testing.expectEqualStrings(config.pinned_ninja, psValue(src, "$NinjaVersion").?);
}

test "bootstrap.sh installs under a host key" {
    const gpa = testing.allocator;
    const src = try readScript(gpa, "bootstrap.sh");
    defer gpa.free(src);

    for ([_][]const u8{ "zig", "cmake", "ninja" }) |tool| {
        const keyed = try std.fmt.allocPrint(gpa, ".harness/{s}/$HOST_KEY", .{tool});
        defer gpa.free(keyed);
        try testing.expect(std.mem.indexOf(u8, src, keyed) != null);
    }
}

test "bootstrap.ps1 installs under a host key" {
    const gpa = testing.allocator;
    const src = try readScript(gpa, "bootstrap.ps1");
    defer gpa.free(src);

    for ([_][]const u8{ "zig", "cmake", "ninja" }) |tool| {
        const keyed = try std.fmt.allocPrint(gpa, ".harness\\{s}\\$HostKey", .{tool});
        defer gpa.free(keyed);
        try testing.expect(std.mem.indexOf(u8, src, keyed) != null);
    }
}

test "neither script installs at the old unkeyed path" {
    // The T0102 regression, stated directly. `.harness/zig/$ZIG_VERSION` is the
    // exact expression that made a Linux bootstrap delete a Windows toolchain,
    // and the shape most likely to be reintroduced by someone simplifying the
    // path back to what it used to be.
    const gpa = testing.allocator;

    const sh = try readScript(gpa, "bootstrap.sh");
    defer gpa.free(sh);
    try testing.expect(std.mem.indexOf(u8, sh, ".harness/zig/$ZIG_VERSION") == null);
    try testing.expect(std.mem.indexOf(u8, sh, ".harness/cmake/$CMAKE_VERSION") == null);
    try testing.expect(std.mem.indexOf(u8, sh, ".harness/ninja/$NINJA_VERSION") == null);

    const ps = try readScript(gpa, "bootstrap.ps1");
    defer gpa.free(ps);
    try testing.expect(std.mem.indexOf(u8, ps, ".harness\\zig\\$ZigVersion") == null);
    try testing.expect(std.mem.indexOf(u8, ps, ".harness\\cmake\\$CMakeVersion") == null);
    try testing.expect(std.mem.indexOf(u8, ps, ".harness\\ninja\\$NinjaVersion") == null);
}
