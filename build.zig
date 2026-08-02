//! HollowPoint build harness.
//!
//! One entry point for both hosts and both targets. Zig supplies the compiler,
//! libc and the Windows SDK; CMake configures DiligentEngine; Ninja does the
//! incremental work. Nothing under third_party/ is modified.
//!
//!   zig build                 build for the host OS
//!   zig build linux           build the Linux x86_64 target
//!   zig build windows         build the Windows x86_64 target
//!   zig build all             build both
//!   zig build dist            stage runnable output into dist/
//!   zig build configure       (re)run CMake configure only
//!   zig build clean           delete build/ and dist/
//!
//!   -Dconfig=Release|Debug|RelWithDebInfo|MinSizeRel   (default: Release)
//!   -Djobs=N -Dverbose -Dccache=false -Dtarget=linux|windows
//!
//! Prerequisites: none beyond what ./bootstrap.sh (or bootstrap.ps1) installs
//! into .harness/ -- zig, cmake and ninja are all pinned. Anything already on
//! PATH is used only as a fallback.

const std = @import("std");
const Build = std.Build;
const Step = Build.Step;

/// Kept in sync with the bootstrap scripts. A mismatch is a warning, not an
/// error: building with a different Zig usually works, but it is the first
/// thing to suspect when output differs between machines.
const pinned_zig_version = "0.16.0";

/// Installed by the bootstrap scripts; used in preference to anything on PATH
/// so the build does not vary with whatever the distribution ships.
///
/// CMake is held on the 3.x line deliberately. CMake 4 rejects
/// `cmake_minimum_required(VERSION <3.5)` and DiligentEngine's vendored
/// third-party libraries still declare 2.8; 3.31 is also new enough for
/// ozz-animation, which requires 3.30.
const pinned_cmake_version = "3.31.12";
const pinned_ninja_version = "1.13.2";

const TargetSpec = struct {
    /// Directory name component, and the name used under dist/.
    key: []const u8,
    /// The `zig build <name>` step.
    step_name: []const u8,
    toolchain: []const u8,
    desc: []const u8,
    os: std.Target.Os.Tag,
};

const specs = [_]TargetSpec{
    .{
        .key = "linux-x86_64",
        .step_name = "linux",
        .toolchain = "cmake/toolchains/x86_64-linux-gnu.cmake",
        .desc = "Build the Linux x86_64 target (glibc 2.28, Vulkan + OpenGL)",
        .os = .linux,
    },
    .{
        .key = "windows-x86_64",
        .step_name = "windows",
        .toolchain = "cmake/toolchains/x86_64-windows-gnu.cmake",
        .desc = "Build the Windows x86_64 target (MinGW ABI, Vulkan + OpenGL)",
        .os = .windows,
    },
};

/// Mirrors CMAKE_BUILD_TYPE. Zig's own -Doptimize names would only have to be
/// translated back into these, so expose the CMake vocabulary directly.
const Config = enum { Release, Debug, RelWithDebInfo, MinSizeRel };

pub fn build(b: *Build) void {
    const config = b.option(Config, "config", "CMake build type (default: Release)") orelse .Release;
    const build_type = @tagName(config);

    const jobs = b.option(u32, "jobs", "Parallel compile jobs (default: one per core)");
    const verbose = b.option(bool, "verbose", "Echo every compiler command line") orelse false;
    const want_ccache = b.option(bool, "ccache", "Use ccache when present (default: yes)") orelse true;
    const only = b.option([]const u8, "target", "Restrict 'all'/'dist' to one target key");

    checkPinnedZig(b);

    const cmake = harnessTool(b, "cmake", b.fmt(".harness/cmake/{s}/bin", .{pinned_cmake_version}));
    const ninja = harnessTool(b, "ninja", b.fmt(".harness/ninja/{s}", .{pinned_ninja_version}));

    const all_step = b.step("all", "Build every target");
    const dist_step = b.step("dist", "Stage runnable output into dist/");
    const configure_step = b.step("configure", "Run CMake configure without building");

    const host_os = b.graph.host.result.os.tag;

    for (specs) |spec| {
        if (only) |k| if (!std.mem.eql(u8, k, spec.key) and !std.mem.eql(u8, k, spec.step_name)) continue;

        const build_dir = b.fmt("build/{s}-{s}", .{ spec.key, asciiLower(b, build_type) });

        // --- configure ---------------------------------------------------
        //
        // CMake is only re-run when the build tree is absent or was configured
        // differently. Once build.ninja exists, Ninja re-runs CMake by itself
        // whenever a CMakeLists.txt or the toolchain file changes -- so an
        // unconditional configure here would only add latency, never accuracy.
        const configure = b.addSystemCommand(&.{ cmake, "-S", ".", "-B", build_dir, "-G", "Ninja" });
        configure.addArgs(&.{
            b.fmt("-DCMAKE_TOOLCHAIN_FILE={s}", .{spec.toolchain}),
            b.fmt("-DCMAKE_BUILD_TYPE={s}", .{build_type}),
            b.fmt("-DHP_ZIG={s}", .{b.graph.zig_exe}),
            // Absolute: CMake rejects a relative CMAKE_CXX_COMPILER.
            b.fmt("-DHP_SHIM_DIR={s}/{s}/toolchain", .{ b.build_root.path orelse ".", build_dir }),
            b.fmt("-DCMAKE_MAKE_PROGRAM={s}", .{ninja}),
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        });
        if (want_ccache) {
            if (b.findProgram(&.{"ccache"}, &.{})) |cc| {
                configure.addArgs(&.{
                    b.fmt("-DCMAKE_C_COMPILER_LAUNCHER={s}", .{cc}),
                    b.fmt("-DCMAKE_CXX_COMPILER_LAUNCHER={s}", .{cc}),
                });
            } else |_| {}
        }
        configure.setName(b.fmt("cmake configure ({s})", .{spec.key}));
        configure.stdio = .inherit;
        configure.has_side_effects = true;

        configure_step.dependOn(&configure.step);

        // --- build -------------------------------------------------------
        const compile = b.addSystemCommand(&.{ cmake, "--build", build_dir });
        if (jobs) |n| compile.addArgs(&.{ "--parallel", b.fmt("{d}", .{n}) });
        if (verbose) compile.addArgs(&.{ "--", "-v" });
        compile.setName(b.fmt("build ({s}, {s})", .{ spec.key, build_type }));
        compile.stdio = .inherit;
        compile.has_side_effects = true;

        if (needsConfigure(b, build_dir, build_type, cmake, ninja)) {
            compile.step.dependOn(&configure.step);
        }

        const target_step = b.step(spec.step_name, spec.desc);
        target_step.dependOn(&compile.step);
        all_step.dependOn(&compile.step);

        // --- dist --------------------------------------------------------
        //
        // Staging is a CMake -P script rather than Zig code so the globbing and
        // the copy behave identically on both hosts, and so `dist` stays usable
        // without the harness.
        const stage = b.addSystemCommand(&.{cmake});
        stage.addArgs(&.{
            b.fmt("-DBUILD_DIR={s}", .{build_dir}),
            b.fmt("-DDIST_DIR=dist/{s}", .{spec.key}),
            b.fmt("-DTARGET_OS={s}", .{@tagName(spec.os)}),
            "-P",
            "cmake/dist.cmake",
        });
        stage.setName(b.fmt("stage dist ({s})", .{spec.key}));
        stage.stdio = .inherit;
        stage.has_side_effects = true;
        stage.step.dependOn(&compile.step);
        dist_step.dependOn(&stage.step);

        // `zig build` with no step builds whatever the host runs.
        if (spec.os == host_os) b.getInstallStep().dependOn(&compile.step);
    }

    // --- clean -----------------------------------------------------------
    const clean = b.addSystemCommand(&.{ cmake, "-E", "rm", "-rf", "build", "dist" });
    clean.setName("clean");
    clean.stdio = .inherit;
    clean.has_side_effects = true;
    b.step("clean", "Delete build/ and dist/").dependOn(&clean.step);
}

/// Absolute path to a bootstrapped tool, or the bare name for PATH lookup if it
/// was never installed. `dir` is relative to the build root.
///
/// Falling back rather than failing keeps the harness usable with a
/// system-provided cmake/ninja; the pin is a default, not a requirement.
fn harnessTool(b: *Build, name: []const u8, dir: []const u8) []const u8 {
    const exe = if (b.graph.host.result.os.tag == .windows) b.fmt("{s}.exe", .{name}) else name;
    const rel = b.fmt("{s}/{s}", .{ dir, exe });
    b.build_root.handle.access(b.graph.io, rel, .{}) catch return name;
    return b.fmt("{s}/{s}", .{ b.build_root.path orelse ".", rel });
}

fn asciiLower(b: *Build, s: []const u8) []const u8 {
    const out = b.allocator.alloc(u8, s.len) catch @panic("OOM");
    for (s, 0..) |c, i| out[i] = std.ascii.toLower(c);
    return out;
}

/// True when the build tree must be configured from scratch: either it does not
/// exist, or its CMakeCache records a different toolchain or build type than
/// the one being asked for.
fn needsConfigure(b: *Build, build_dir: []const u8, build_type: []const u8, cmake: []const u8, ninja: []const u8) bool {
    const io = b.graph.io;
    const dir = b.build_root.handle;

    dir.access(io, b.fmt("{s}/build.ninja", .{build_dir}), .{}) catch return true;

    const cache = dir.readFileAlloc(
        io,
        b.fmt("{s}/CMakeCache.txt", .{build_dir}),
        b.allocator,
        .limited(8 * 1024 * 1024),
    ) catch return true;

    if (!cacheHas(cache, "HP_ZIG", b.graph.zig_exe)) return true;
    if (!cacheHas(cache, "CMAKE_BUILD_TYPE", build_type)) return true;
    // Catches a bumped pin: a different CMake or Ninja needs a fresh tree.
    if (!cacheHas(cache, "CMAKE_COMMAND", cmake)) return true;
    if (!cacheHas(cache, "CMAKE_MAKE_PROGRAM", ninja)) return true;
    return false;
}

/// Look up `name` in a CMakeCache.txt and report whether it equals `want`.
///
/// Entries are `NAME:TYPE=VALUE`, and the type is not predictable: a value
/// passed as `-DHP_ZIG=...` without a declared type lands as `UNINITIALIZED`,
/// not `FILEPATH`. Matching on the name alone avoids depending on that.
fn cacheHas(cache: []const u8, name: []const u8, want: []const u8) bool {
    var lines = std.mem.splitScalar(u8, cache, '\n');
    while (lines.next()) |raw| {
        const line = std.mem.trimEnd(u8, raw, "\r");
        if (!std.mem.startsWith(u8, line, name)) continue;
        if (line.len <= name.len or line[name.len] != ':') continue;
        const eq = std.mem.indexOfScalarPos(u8, line, name.len, '=') orelse continue;
        return std.mem.eql(u8, line[eq + 1 ..], want);
    }
    return false;
}

fn checkPinnedZig(b: *Build) void {
    const running = @import("builtin").zig_version_string;
    if (!std.mem.eql(u8, running, pinned_zig_version)) {
        std.debug.print(
            "warning: running zig {s}, but this project pins {s}. " ++
                "Run ./bootstrap.sh (or bootstrap.ps1) to install the pinned toolchain.\n",
            .{ running, pinned_zig_version },
        );
    }
    _ = b;
}
