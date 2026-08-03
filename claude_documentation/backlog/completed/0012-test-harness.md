# T0012 — Build a test harness for TDD

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Complex |
| **Phase** | 1 — Harden the build |
| **Created** | 2026-08-02 |
| **Refs** | [../../documentation/03-build-harness.md](../../documentation/03-build-harness.md) |

## Why

The intention is to work test-first from here on, and there is currently no way
to run a test at all: `build.zig` has no `test` step and nothing in the tree
compiles or executes assertions.

Doing this **before** the application exists (T0006) is the right order — a test
harness retrofitted onto existing code tends to test what the code happens to do
rather than what it should.

There is also a concrete gap it would close. Retiring the probe app (T0007) left
the build with **no executable target**, so there is no way to catch a runtime
regression in the harness itself. Four of the nine recorded gotchas — G6, G7, the
sysroot glibc mismatch (D4) and the RPATH-to-stubs bug — compiled perfectly
cleanly and were only caught by *running* something. A test binary restores that.

## Done when

- [x] `zig build test` compiles and runs the suite, and fails the build on a
      failing test
- [x] Writing a new test requires adding one file and nothing else — no
      per-test CMake edits, or TDD friction kills adoption
- [x] Tests build for **both** targets, and the Windows suite is runnable
      (see notes — wine is proven to work)
- [x] Failure output identifies the failing assertion and file:line
- [x] At least one genuinely meaningful test exists, not just a placeholder
- [x] `BUILDING.md` documents how to run and how to add a test

## Subtasks

- [x] 12.1 **Choose the framework.** GoogleTest 1.16.0 is *already vendored*
      inside DiligentEngine at `DiligentCore/ThirdParty/googletest`, but it is
      not currently built (`DILIGENT_BUILD_TESTS` is OFF and the target does not
      appear in `build.ninja`). Weigh reusing it against a single-header
      alternative (doctest/Catch2) — see notes, this is the main decision.
- [x] 12.2 Decide the layout: `tests/` at the root, or `<module>/tests/`
      alongside the code. Root-level is simpler while there is no application.
- [x] 12.3 Add the test target to the build, following the existing pattern —
      a `cmake/` module plus a root `CMakeLists.txt` hook, **never** a patch to
      `third_party/DiligentEngine`.
- [x] 12.4 Add a `test` step to `build.zig`. Note the existing steps set
      `has_side_effects = true` and take a global lock; a test step should
      behave the same so its output is not interleaved.
- [x] 12.5 Decide how cross-target tests run (see notes).
- [x] 12.6 Write the first real tests. Good early candidates that need no
      application: `cmake/dist.cmake` staging rules, and the `cacheHas()`
      CMakeCache parsing in `build.zig` — the latter already had a real bug
      (`:UNINITIALIZED` vs `:FILEPATH`) that silently defeated incremental
      builds and would have been caught instantly by a unit test.
- [x] 12.7 Document it in `BUILDING.md`.

## Notes / findings

**Framework choice is the crux.** Reusing Diligent's vendored GoogleTest costs
nothing in new dependencies, but it means enabling a Diligent option that also
drags in its own test targets, and it couples the test setup to the engine
submodule — which every other decision here has deliberately avoided (see D1).
A single-header framework vendored under `third_party/` is independent and
cheaper to build, at the cost of one more dependency. Decide deliberately and
record it in the decision log.

**Cross-target execution.** Linux tests run natively. Windows tests can run
under wine — T0001 proved a cross-built Windows executable runs correctly that
way, including DLL loading and the generated import libraries. That makes a
`zig build test -Dtarget=windows` genuinely feasible from Linux, which is
unusual and worth having. Treat it as a stretch goal, not a blocker.

**Two distinct kinds of test are wanted here** and they should not be conflated:
- *unit tests* of project code (the eventual application, engine glue)
- *harness tests* of the build system itself — the dist staging rules, the
  toolchain fix-ups, the cache-key parsing

The second kind is unusual but is where the demonstrated bugs actually were. Do
not skip it just because it is not the conventional target of TDD.

**Do not require a GPU.** Anything that needs a device makes the suite
unrunnable in CI and on headless machines. Keep GPU-dependent checks in a
separate, clearly-marked target.

### Second review pass (2026-08-03) — the GPU-marked target can still run headless

Two facts make the "separate GPU target" more useful than it sounds, worth
knowing when it is designed:

- **Software rasterisation is already proven in this repo.** T0001/T0003 ran
  the probe under Xvfb on llvmpipe (GL) — including the *Windows* binary under
  wine — so a GPU-dependent test target can run in CI on Mesa with no real
  hardware. Lavapipe gives the same for Vulkan.
- **Diligent can render without a window.** `OffScreenSwapChain.hpp`
  (GraphicsTools) creates an off-screen swap chain from just a device — no
  surface, no X11 — which suits image-comparison render tests better than
  driving a hidden window.

Neither changes the rule (default suite stays GPU-free); they mean the GPU
suite is CI-able rather than desktop-only.

---

## Closing evidence (2026-08-03)

### What was built

| Piece | Where |
|---|---|
| doctest 2.5.3 | `third_party/doctest` (submodule) |
| Bucket executables | `tests/CMakeLists.txt` → `hp_tests_<bucket>` |
| C++ tests | `tests/fast/*.cpp` |
| Harness tests (Zig) | `tests/harness/{cache,dist}_test.zig` |
| Extracted for testability | `tools/harness/cache.zig` |
| `test` step, bucket/filter options, runner resolution | `build.zig` |
| Docs | `BUILDING.md` § Tests |
| Decision | `02-decision-log.md` D11 |

### Verified by red-green, not by watching it pass

**A failing test fails the build — C++ side.** A temporary failing case:

```
EXIT_WITH_FAILING_TEST=1
C:/Development/hollow_point/tests/fast/tmp_redgreen_test.cpp:4: ERROR: CHECK( answer == 42 ) is NOT correct!
  values: CHECK( 41 == 42 )
[doctest] test cases: 4 | 3 passed | 1 failed | 0 skipped
```

That single file also proves the "one file and nothing else" requirement: it was
picked up with no CMake edit and no manual reconfigure (`CONFIGURE_DEPENDS`).

**A failing test fails the build — Zig side.** Inverting one assertion:

```
EXIT_WITH_FAILING_ZIG_TEST=1
error: 'cache_test.test.a missing name is false, not an error' failed:
  C:\Development\hollow_point\tests\harness\cache_test.zig:45:5
```

**The dist tests catch a real regression.** Pointing `dist.cmake`'s Windows
shared-library destination at `lib/` instead of `bin/`:

```
EXIT_WITH_BROKEN_DIST=1
error: 'dist_test.test.windows staging puts DLLs beside the exe, not in lib/' failed
Build Summary: 2/4 steps succeeded (1 failed); 2/3 tests passed (1 failed)
```

Exactly the test that claims to cover it, and only that test. `dist.cmake` was
restored and verified identical to HEAD afterwards. This is the check that
separates a meaningful test from one that merely passes.

**Everything green, both targets:**

```
$ zig build test -Dtest=all --summary all
Build Summary: 12/12 steps succeeded; 10/10 tests passed
+- test (linux-x86_64, fast)    success 129ms
+- test (windows-x86_64, fast)  success  13ms
+- run test harness-cache  7 pass (7 total)
+- run test harness-dist   3 pass (3 total)

$ zig build test -Dtest=all -Dtest-filter='enkiTS*'
[doctest] test cases: 1 | 1 passed | 0 failed | 2 skipped     (x2, both targets)
```

### Findings

**The framework question was really two questions.** Both of the ticket's own
suggested first tests — `cacheHas` and `dist.cmake` — are harness tests, and
neither is C++. Zig's built-in runner covers them with no new dependency;
doctest covers project code. Recorded as D11.

**Tags are the wrong lever for keeping a big suite fast.** Raised during design:
this becomes a huge system and nobody wants to wait hours. A tag only saves the
*running* of a test — the compile and the link are already paid. Buckets are
therefore separate executables, and `zig build test` builds one by name so Ninja
resolves only that binary's dependencies. This also satisfies the ticket's
"keep GPU tests in a separate target" requirement structurally rather than by
convention.

**Zig 0.16's std has moved a long way**, which cost most of the implementation
time and is worth recording before the next person hits it:

| Was | Now |
|---|---|
| `std.fs.cwd()` | `std.Io.Dir.cwd()` |
| `std.fs.Dir` | `std.Io.Dir`, and every method takes an `Io` |
| `std.process.getEnvVarOwned` | gone; use build options instead |
| `std.process.Child.run(.{…})` | `std.process.run(gpa, io, .{…})` |
| `std.testing.tmpDir` | still there, but `TmpDir.dir` is an `Io.Dir` |
| `Term.Exited` | `Term.exited` (lower case) |

The env-var route was abandoned in favour of `b.addOptions()`, which is better
anyway: the cmake path and repo root arrive as comptime constants and the test
needs nothing set up around it.

**A relative `@import` may not escape the module root.** `tests/harness/` cannot
`@import("../../tools/harness/cache.zig")` — "import of file outside module
path". `build.zig` passes the module explicitly as a named import instead.

### Not done, deliberately

- **The `gpu` and `perf` buckets exist but hold no tests.** Empty directories
  produce no target, so they cost nothing. There is no GPU code to test yet;
  the ticket's second review pass established that a GPU suite is CI-able on
  llvmpipe/lavapipe when there is.
- **A zero-match `-Dtest-filter` passes silently** on the Zig side rather than
  erroring on a typo. Minor, noted rather than fixed.
- **ozz-animation is still unexercised** — enkiTS and meshoptimizer now have
  tests, ozz does not. It needs a skeleton/animation fixture, which belongs with
  T0041 rather than here.
