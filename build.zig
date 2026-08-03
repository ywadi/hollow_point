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

/// Test buckets, separated at the *binary* level rather than by runtime tags.
/// A tag still costs you the compile and the link; a separate executable costs
/// nothing at all when you are not running it. `fast` is the default because
/// the inner loop is what decides whether tests actually get written.
const test_buckets = [_][]const u8{ "fast", "integration", "gpu", "perf" };

/// Zig-side suites covering the harness itself. They sit in the same buckets as
/// the C++ ones so `-Dtest=fast` means the same thing on both sides.
const harness_tests = [_]struct {
    name: []const u8,
    bucket: []const u8,
    path: []const u8,
}{
    .{ .name = "harness-cache", .bucket = "fast", .path = "tests/harness/cache_test.zig" },
    .{ .name = "harness-dist", .bucket = "integration", .path = "tests/harness/dist_test.zig" },
};

fn bucketSelected(sel: []const u8, bucket: []const u8) bool {
    return std.mem.eql(u8, sel, "all") or std.mem.eql(u8, sel, bucket);
}

/// True when `tests/<bucket>/` exists. The CMake side creates a bucket target
/// only when that directory holds sources, so asking Ninja to build a target
/// for an absent bucket would be an error rather than a no-op.
fn hasBucketDir(b: *Build, bucket: []const u8) bool {
    b.build_root.handle.access(b.graph.io, b.fmt("tests/{s}", .{bucket}), .{}) catch return false;
    return true;
}

/// argv prefix needed to execute a binary built for `target_os` on this host.
/// An empty slice means "run it directly"; null means it cannot be run here.
///
/// The 2x2 of hosts and targets, deliberately mirroring D3's build matrix --
/// a test suite that only runs for the host target proves half of what it
/// should.
fn runnerFor(b: *Build, target_os: std.Target.Os.Tag) ?[]const []const u8 {
    const host = b.graph.host.result.os.tag;
    if (host == target_os) return &.{};

    if (host == .linux and target_os == .windows) {
        // Under WSL, binfmt_misc hands a PE to the real Windows loader, so the
        // .exe runs as a genuine Windows process -- higher fidelity than wine
        // and needing nothing installed. Proven in T0004.
        if (wslInteropEnabled(b)) return &.{};
        // A real Linux box falls back to wine, which T0001 proved works for
        // this project's binaries including DLL loading.
        if (b.findProgram(&.{"wine"}, &.{})) |wine| {
            const arr = b.allocator.alloc([]const u8, 1) catch @panic("OOM");
            arr[0] = wine;
            return arr;
        } else |_| {}
        return null;
    }

    if (host == .windows and target_os == .linux) {
        // The mirror image: reach the Linux binary through WSL. `wslpath`
        // translates the drive path, and `exec` keeps the child's exit code,
        // which is what makes a failing test fail the build.
        if (b.findProgram(&.{"wsl.exe"}, &.{})) |wsl| {
            const argv = [_][]const u8{
                wsl, "-e", "sh", "-c",
                "p=$(wslpath -a \"$1\"); shift; exec \"$p\" \"$@\"",
                "sh",
            };
            return b.allocator.dupe([]const u8, &argv) catch @panic("OOM");
        } else |_| {}
        return null;
    }

    return null;
}

/// Whether this Linux host can execute Windows binaries directly.
fn wslInteropEnabled(b: *Build) bool {
    const data = std.Io.Dir.cwd().readFileAlloc(
        b.graph.io,
        "/proc/sys/fs/binfmt_misc/WSLInterop",
        b.allocator,
        .limited(256),
    ) catch return false;
    return std.mem.indexOf(u8, data, "enabled") != null;
}

pub fn build(b: *Build) void {
    const config = b.option(Config, "config", "CMake build type (default: Release)") orelse .Release;
    const build_type = @tagName(config);

    const jobs = b.option(u32, "jobs", "Parallel compile jobs (default: one per core)");
    const verbose = b.option(bool, "verbose", "Echo every compiler command line") orelse false;
    const want_ccache = b.option(bool, "ccache", "Use ccache when present (default: yes)") orelse true;
    const only = b.option([]const u8, "target", "Restrict 'all'/'dist' to one target key");
    const test_sel = b.option([]const u8, "test", "Test bucket: fast|integration|gpu|perf|all (default: fast)") orelse "fast";
    const test_filter = b.option([]const u8, "test-filter", "Run only tests whose name matches this pattern");

    checkPinnedZig(b);

    const cmake = harnessTool(b, "cmake", b.fmt(".harness/cmake/{s}/bin", .{pinned_cmake_version}));
    const ninja = harnessTool(b, "ninja", b.fmt(".harness/ninja/{s}", .{pinned_ninja_version}));

    const all_step = b.step("all", "Build every target");
    const dist_step = b.step("dist", "Stage runnable output into dist/");
    const configure_step = b.step("configure", "Run CMake configure without building");
    const test_step = b.step("test", "Build and run the test suites");

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

        const needs_cfg = needsConfigure(b, build_dir, build_type, cmake, ninja);
        if (needs_cfg) compile.step.dependOn(&configure.step);

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

        // --- tests -------------------------------------------------------
        //
        // Each bucket is built by name rather than building everything: Ninja
        // resolves just that executable's dependencies, so running the fast
        // suite does not drag in ~1100 engine targets.
        for (test_buckets) |bucket| {
            if (!bucketSelected(test_sel, bucket)) continue;
            if (!hasBucketDir(b, bucket)) continue;

            const tgt = b.fmt("hp_tests_{s}", .{bucket});
            const build_tests = b.addSystemCommand(&.{ cmake, "--build", build_dir, "--target", tgt });
            build_tests.setName(b.fmt("build tests ({s}, {s})", .{ spec.key, bucket }));
            build_tests.stdio = .inherit;
            build_tests.has_side_effects = true;
            if (needs_cfg) build_tests.step.dependOn(&configure.step);

            const exe = b.fmt("{s}/{s}/tests/{s}{s}", .{
                b.build_root.path orelse ".",
                build_dir,
                tgt,
                if (spec.os == .windows) ".exe" else "",
            });

            const prefix = runnerFor(b, spec.os) orelse {
                // Never silently pass: a suite that cannot be executed here is
                // reported, and the build still fails if it was asked for
                // explicitly rather than picked up by `all`.
                std.debug.print(
                    "warning: cannot run the {s} test suite for {s} on this host " ++
                        "(no WSL interop, no wine) -- built but not run\n",
                    .{ bucket, spec.key },
                );
                test_step.dependOn(&build_tests.step);
                continue;
            };

            const run = if (prefix.len == 0)
                b.addSystemCommand(&.{exe})
            else blk: {
                const r = b.addSystemCommand(prefix);
                r.addArg(exe);
                break :blk r;
            };
            if (test_filter) |f| run.addArg(b.fmt("--test-case={s}", .{f}));
            run.setName(b.fmt("test ({s}, {s})", .{ spec.key, bucket }));
            run.stdio = .inherit;
            // Tests must re-run every time; a cached "pass" is not a pass.
            run.has_side_effects = true;
            run.step.dependOn(&build_tests.step);
            test_step.dependOn(&run.step);
        }

        // `zig build` with no step builds whatever the host runs.
        if (spec.os == host_os) b.getInstallStep().dependOn(&compile.step);
    }

    // --- harness tests (Zig) ---------------------------------------------
    //
    // These test the build harness itself -- CMakeCache parsing, the dist
    // staging script. Host-only by design: they exercise logic that only ever
    // runs on the host, so cross-compiling them would prove nothing. The C++
    // suites above are what run for both targets.
    const cache_mod = b.createModule(.{ .root_source_file = b.path("tools/harness/cache.zig") });

    // Paths the harness knows and a test cannot discover for itself, handed
    // over as build options rather than environment variables so the suite
    // needs nothing set up around it.
    const test_config = b.addOptions();
    test_config.addOption([]const u8, "cmake", cmake);
    test_config.addOption([]const u8, "repo_root", b.build_root.path orelse ".");

    for (harness_tests) |h| {
        if (!bucketSelected(test_sel, h.bucket)) continue;

        const mod = b.createModule(.{
            .root_source_file = b.path(h.path),
            .target = b.graph.host,
            .optimize = .Debug,
            .imports = &.{
                .{ .name = "harness_cache", .module = cache_mod },
                .{ .name = "test_config", .module = test_config.createModule() },
            },
        });

        const filters: []const []const u8 = if (test_filter) |f| one: {
            const arr = b.allocator.alloc([]const u8, 1) catch @panic("OOM");
            arr[0] = f;
            break :one arr;
        } else &.{};

        const t = b.addTest(.{ .name = h.name, .root_module = mod, .filters = filters });
        const run = b.addRunArtifact(t);
        run.has_side_effects = true;
        test_step.dependOn(&run.step);
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
/// Lives in `tools/harness/cache.zig` so it can be unit tested (T0012); the
/// tests import that file as a module. See it for why the type is ignored.
const cacheHas = @import("tools/harness/cache.zig").cacheHas;

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
