//! Integration tests for `cmake/dist.cmake`.
//!
//! Bucket: integration. These build a synthetic build tree, run the real
//! staging script against it with `cmake -P`, and assert on the layout it
//! produces. Seconds rather than microseconds -- hence a separate bucket from
//! the pure unit tests.
//!
//! The script is driven as a subprocess rather than reimplemented, because what
//! is worth testing is the globbing and copy behaviour of the exact file that
//! `zig build dist` runs.
//!
//! `build.zig` supplies the pinned cmake path and the repository root as build
//! options (see the `test_config` module) rather than environment variables, so
//! the test needs nothing set up around it.

const std = @import("std");
const testing = std.testing;
const io = testing.io;
const config = @import("test_config");

/// A staged synthetic tree. Absolute paths throughout: the test must not depend
/// on the working directory it happens to be run from.
const Staged = struct {
    work: []const u8,
    dist: []const u8,
    gpa: std.mem.Allocator,

    fn deinit(self: *Staged) void {
        std.Io.Dir.cwd().deleteTree(io, self.work) catch {};
        self.gpa.free(self.work);
        self.gpa.free(self.dist);
    }

    /// True when `rel` exists under the staged dist directory.
    fn has(self: *Staged, rel: []const u8) bool {
        const path = std.fs.path.join(self.gpa, &.{ self.dist, rel }) catch return false;
        defer self.gpa.free(path);
        std.Io.Dir.cwd().access(io, path, .{}) catch return false;
        return true;
    }
};

fn writeAt(cwd: std.Io.Dir, base: []const u8, rel: []const u8, bytes: []const u8) !void {
    const gpa = testing.allocator;
    const full = try std.fs.path.join(gpa, &.{ base, rel });
    defer gpa.free(full);
    if (std.fs.path.dirname(full)) |sub| try cwd.createDirPath(io, sub);
    try cwd.writeFile(io, .{ .sub_path = full, .data = bytes });
}

/// Build a synthetic build tree and stage it with the real dist.cmake.
fn stage(gpa: std.mem.Allocator, name: []const u8, target_os: []const u8) !Staged {
    const cwd = std.Io.Dir.cwd();

    const work = try std.fs.path.join(gpa, &.{ config.repo_root, ".zig-cache", name });
    errdefer gpa.free(work);
    cwd.deleteTree(io, work) catch {};
    try cwd.createDirPath(io, work);

    // An app payload, a shared library, a static library, and two decoys that
    // must not be staged.
    try writeAt(cwd, work, "build/apps/demo/demo", "executable");
    try writeAt(cwd, work, "build/apps/demo/assets/shader.hlsl", "shader");
    try writeAt(cwd, work, "build/apps/demo/CMakeFiles/demo.dir/main.cpp.o", "object");
    try writeAt(cwd, work, "build/libengine.a", "static");
    try writeAt(cwd, work, "build/CMakeFiles/decoy.a", "must not be staged");
    if (std.mem.eql(u8, target_os, "windows")) {
        try writeAt(cwd, work, "build/Renderer.dll", "shared");
        try writeAt(cwd, work, "build/Renderer.dll.a", "import");
    } else {
        try writeAt(cwd, work, "build/libRenderer.so", "shared");
    }

    // A gameplay module's working copy (T0048). Transient by construction, and
    // left behind whenever a process is killed rather than exiting cleanly --
    // an editor crash, a test runner timing one out. One was staged into a
    // shipping layout before it was excluded.
    if (std.mem.eql(u8, target_os, "windows")) {
        try writeAt(cwd, work, "build/apps/demo/hp_mod.hot1.dll", "must not be staged");
    } else {
        try writeAt(cwd, work, "build/libhp_mod.hot1.so", "must not be staged");
    }

    // Test fixtures, which the suite builds under build/tests/ and the
    // recursive globs cannot tell apart from real output by name.
    try writeAt(cwd, work, "build/tests/libfixture.a", "must not be staged");
    if (std.mem.eql(u8, target_os, "windows")) {
        try writeAt(cwd, work, "build/tests/Fixture.dll", "must not be staged");
        try writeAt(cwd, work, "build/tests/Fixture.dll.a", "must not be staged");
    } else {
        try writeAt(cwd, work, "build/tests/libFixture.so", "must not be staged");
    }

    const build_dir = try std.fs.path.join(gpa, &.{ work, "build" });
    defer gpa.free(build_dir);
    const dist_dir = try std.fs.path.join(gpa, &.{ work, "dist" });
    errdefer gpa.free(dist_dir);
    const script = try std.fs.path.join(gpa, &.{ config.repo_root, "cmake", "dist.cmake" });
    defer gpa.free(script);

    const arg_build = try std.fmt.allocPrint(gpa, "-DBUILD_DIR={s}", .{build_dir});
    defer gpa.free(arg_build);
    const arg_dist = try std.fmt.allocPrint(gpa, "-DDIST_DIR={s}", .{dist_dir});
    defer gpa.free(arg_dist);
    const arg_os = try std.fmt.allocPrint(gpa, "-DTARGET_OS={s}", .{target_os});
    defer gpa.free(arg_os);

    const res = try std.process.run(gpa, io, .{
        .argv = &.{ config.cmake, arg_build, arg_dist, arg_os, "-P", script },
    });
    defer gpa.free(res.stdout);
    defer gpa.free(res.stderr);

    if (res.term != .exited or res.term.exited != 0) {
        std.debug.print("dist.cmake failed:\n{s}\n{s}\n", .{ res.stdout, res.stderr });
        return error.DistScriptFailed;
    }

    return .{ .work = work, .dist = dist_dir, .gpa = gpa };
}

test "linux staging puts shared objects in lib/ and the app payload in bin/" {
    var s = try stage(testing.allocator, "hp-dist-test-linux", "linux");
    defer s.deinit();

    try testing.expect(s.has("bin/demo"));
    // Assets keep their layout relative to the executable -- that is where the
    // app looks for them.
    try testing.expect(s.has("bin/assets/shader.hlsl"));
    try testing.expect(s.has("lib/libRenderer.so"));
    try testing.expect(s.has("lib/libengine.a"));
}

test "windows staging puts DLLs beside the exe, not in lib/" {
    var s = try stage(testing.allocator, "hp-dist-test-windows", "windows");
    defer s.deinit();

    // The whole point of the TARGET_OS split: on Windows the loader only finds
    // a DLL next to the executable, so bin/ is correct and lib/ would be a bug.
    try testing.expect(s.has("bin/Renderer.dll"));
    try testing.expect(!s.has("lib/Renderer.dll"));
    // Import libraries are what an application links against, so those do go to
    // lib/ even though the DLL does not.
    try testing.expect(s.has("lib/Renderer.dll.a"));
    try testing.expect(s.has("lib/libengine.a"));
}

test "test fixtures are never staged (linux)" {
    var s = try stage(testing.allocator, "hp-dist-test-fixture-linux", "linux");
    defer s.deinit();

    try testing.expect(!s.has("lib/libFixture.so"));
    try testing.expect(!s.has("bin/libFixture.so"));
    try testing.expect(!s.has("lib/libfixture.a"));

    // The real shared object sitting beside them still stages. Without this the
    // two assertions above would pass just as well if the glob had stopped
    // finding anything at all.
    try testing.expect(s.has("lib/libRenderer.so"));
}

test "test fixtures are never staged (windows)" {
    var s = try stage(testing.allocator, "hp-dist-test-fixture-windows", "windows");
    defer s.deinit();

    // This is the case with teeth. Windows stages shared libraries into bin/,
    // so an unfiltered fixture lands *beside the executable* -- which is where
    // hp_unload_module_broken.dll was going, an artifact built specifically to
    // segfault the process at exit (T0105.1's control case).
    try testing.expect(!s.has("bin/Fixture.dll"));
    try testing.expect(!s.has("lib/Fixture.dll"));
    try testing.expect(!s.has("lib/Fixture.dll.a"));
    try testing.expect(!s.has("lib/libfixture.a"));

    try testing.expect(s.has("bin/Renderer.dll"));
}

test "a module's working copy is never staged" {
    var s = try stage(testing.allocator, "hp-dist-test-hotcopy", "linux");
    defer s.deinit();

    try testing.expect(!s.has("lib/libhp_mod.hot1.so"));
    try testing.expect(!s.has("bin/libhp_mod.hot1.so"));
    // The real shared object still stages, so this is an exclusion rather than
    // a glob that stopped matching anything.
    try testing.expect(s.has("lib/libRenderer.so"));
}

test "build bookkeeping is never staged" {
    var s = try stage(testing.allocator, "hp-dist-test-decoy", "linux");
    defer s.deinit();

    // CMakeFiles/ holds intermediate objects and compiler-detection artefacts.
    // The .a in there is a decoy: the recursive glob finds it, and only the
    // explicit filter keeps it out of dist.
    try testing.expect(!s.has("lib/decoy.a"));
    try testing.expect(!s.has("bin/CMakeFiles/demo.dir/main.cpp.o"));
    try testing.expect(!s.has("bin/main.cpp.o"));
}
