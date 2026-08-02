# T0084 — Continuous integration

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 1 — Harden the build |
| **Created** | 2026-08-03 |

## Why

The build harness supports two targets from either host, and the test harness
(T0012) will provide a suite. Nothing runs either automatically, so a change that
breaks the Windows cross-build or a test is discovered whenever someone happens to
try it.

This is unusually cheap here because the harness was built for it: `zig build all`
plus a bootstrap that installs its own toolchain means CI needs almost no setup.

## Done when

- [ ] Both targets build on every push
- [ ] The test suite runs and failures block the result
- [ ] The bootstrap is exercised from scratch, catching toolchain-pinning breakage
- [ ] Build time is reasonable — caching where it helps
- [ ] A failure is legible without reproducing locally
- [ ] Ideally, a Windows runner covers T0004 (building *on* Windows)

## Subtasks

- [ ] 84.1 Choose CI (GitHub Actions is the obvious fit — the repo is on GitHub)
- [ ] 84.2 Linux job: bootstrap, `zig build all`, run tests
- [ ] 84.3 Cache `.harness/` and ccache between runs
- [ ] 84.4 Windows runner for the native path (T0004)
- [ ] 84.5 Verify configure works offline (T0010) — a job with networking blocked
- [ ] 84.6 Upload `dist/` artifacts on demand
- [ ] 84.7 Keep run time low enough that it is not routinely ignored

## Notes / findings

**The submodules are the first thing to get right.** `third_party/` is nine
submodules and DiligentEngine has four of its own — CI must clone recursively, and
that is slow. Cache aggressively or use a shallow recursive clone.

**Caching `.harness/` matters more than it looks.** Zig, CMake and Ninja are
roughly 700 MB installed; downloading them per run is most of the wall clock for
an otherwise incremental build.

**CI is what would actually close T0004.** A Windows runner exercises
`bootstrap.ps1`, the `.cmd` compiler shims and the case-sensitivity probe's false
branch — all currently written but never executed. That is a more reliable answer
than waiting for access to a Windows machine.

Keep it fast. A CI run slow enough to be ignored provides no signal, and a red
build nobody looks at is worse than no CI at all.
