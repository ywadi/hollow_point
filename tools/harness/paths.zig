//! Where the bootstrapped toolchain lives under `.harness/`.
//!
//! One definition of the layout, shared by `build.zig` and its tests, because
//! the bootstrap scripts cannot import it and will drift from it if nothing is
//! watching. See `tests/harness/paths_test.zig`, and `tests/harness/pins_test.zig`
//! for the check that `bootstrap.sh` and `bootstrap.ps1` still agree with this.
//!
//! The layout is `.harness/<tool>/<host-key>/<version>/`. It is host-keyed
//! because of T0102: it used to be `.harness/<tool>/<version>/`, and both
//! bootstrap scripts deleted the destination before extracting into it. On a
//! Windows machine with WSL -- how this project is actually developed -- that
//! meant running either bootstrap silently destroyed the other host's
//! toolchain, and neither script noticed, because each checks only for its own
//! binary name (`zig` vs `zig.exe`), finds it missing, and reinstalls over the
//! top.
//!
//! `.harness/dl/` stays shared and is deliberately not host-keyed: the archives
//! are already named for their host (`zig-x86_64-linux-0.16.0.tar.xz` versus
//! `zig-x86_64-windows-0.16.0.zip`), so they never collide, and a dual-host
//! machine gets to download each one once.

const std = @import("std");

/// The `<os>-<arch>` name for a host, or null if no bootstrap script supports
/// it. Deliberately the same vocabulary as the target keys in `build.zig`'s
/// `specs` and the directory names under `build/` and `dist/` -- one spelling
/// for "which machine is this", not two.
///
/// Returning null rather than a guess matters: an invented key names a
/// directory no bootstrap script will ever populate, and `harnessTool` would
/// then fall back to PATH while looking like it had a pinned toolchain.
pub fn hostKey(os: std.Target.Os.Tag, arch: std.Target.Cpu.Arch) ?[]const u8 {
    return switch (os) {
        // Matches the case arms in bootstrap.sh.
        .linux => switch (arch) {
            .x86_64 => "linux-x86_64",
            .aarch64 => "linux-aarch64",
            else => null,
        },
        // bootstrap.ps1 requires PROCESSOR_ARCHITECTURE=AMD64.
        .windows => switch (arch) {
            .x86_64 => "windows-x86_64",
            else => null,
        },
        else => null,
    };
}

/// Directory a bootstrapped tool is installed into, relative to the repo root.
/// Caller owns the returned memory.
pub fn toolDir(
    gpa: std.mem.Allocator,
    tool: []const u8,
    host_key: []const u8,
    version: []const u8,
) ![]u8 {
    return std.fmt.allocPrint(gpa, ".harness/{s}/{s}/{s}", .{ tool, host_key, version });
}

/// The pre-T0102 shared path. Only ever read, never installed into -- it is
/// what the harness and the bootstrap scripts look for so they can tell you a
/// stale toolchain is still sitting there taking up a few hundred megabytes.
///
/// Nothing deletes it automatically, and that is the point: on a dual-host
/// machine the directory left at the old path may well be the *other* host's
/// toolchain, and removing it would reintroduce exactly the bug this layout
/// exists to fix. Caller owns the returned memory.
pub fn legacyToolDir(gpa: std.mem.Allocator, tool: []const u8, version: []const u8) ![]u8 {
    return std.fmt.allocPrint(gpa, ".harness/{s}/{s}", .{ tool, version });
}
