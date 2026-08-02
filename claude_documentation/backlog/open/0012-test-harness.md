# T0012 — Build a test harness for TDD

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
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

- [ ] `zig build test` compiles and runs the suite, and fails the build on a
      failing test
- [ ] Writing a new test requires adding one file and nothing else — no
      per-test CMake edits, or TDD friction kills adoption
- [ ] Tests build for **both** targets, and the Windows suite is runnable
      (see notes — wine is proven to work)
- [ ] Failure output identifies the failing assertion and file:line
- [ ] At least one genuinely meaningful test exists, not just a placeholder
- [ ] `BUILDING.md` documents how to run and how to add a test

## Subtasks

- [ ] 12.1 **Choose the framework.** GoogleTest 1.16.0 is *already vendored*
      inside DiligentEngine at `DiligentCore/ThirdParty/googletest`, but it is
      not currently built (`DILIGENT_BUILD_TESTS` is OFF and the target does not
      appear in `build.ninja`). Weigh reusing it against a single-header
      alternative (doctest/Catch2) — see notes, this is the main decision.
- [ ] 12.2 Decide the layout: `tests/` at the root, or `<module>/tests/`
      alongside the code. Root-level is simpler while there is no application.
- [ ] 12.3 Add the test target to the build, following the existing pattern —
      a `cmake/` module plus a root `CMakeLists.txt` hook, **never** a patch to
      `third_party/DiligentEngine`.
- [ ] 12.4 Add a `test` step to `build.zig`. Note the existing steps set
      `has_side_effects = true` and take a global lock; a test step should
      behave the same so its output is not interleaved.
- [ ] 12.5 Decide how cross-target tests run (see notes).
- [ ] 12.6 Write the first real tests. Good early candidates that need no
      application: `cmake/dist.cmake` staging rules, and the `cacheHas()`
      CMakeCache parsing in `build.zig` — the latter already had a real bug
      (`:UNINITIALIZED` vs `:FILEPATH`) that silently defeated incremental
      builds and would have been caught instantly by a unit test.
- [ ] 12.7 Document it in `BUILDING.md`.

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
