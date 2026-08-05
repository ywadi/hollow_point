# T0084 — Continuous integration

| | |
|---|---|
| **Status** | ✅ DONE |
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

- [~] Both targets build on every push — **not met as written, deliberately.**
      Both targets do build; `full-build.yml` runs them nightly and on dispatch
      rather than per-commit, weighed against 84.7. See "Closing" below
- [x] The test suite runs and failures block the result
- [x] The bootstrap is exercised from scratch, catching toolchain-pinning breakage
- [~] Build time is reasonable — caching where it helps — **`.harness/` is
      cached (635 MB of pure download, the real win) and the fast/slow job split
      is the design answer; no durations were recorded here**, so the claim rested
      on reasoning. Measuring it — and fixing what the measurement showed — became
      [T0121](0121-ci-build-time.md)
- [x] A failure is legible without reproducing locally
- [x] Ideally, a Windows runner covers T0004 (building *on* Windows)

## Subtasks

- [x] 84.1 Choose CI (GitHub Actions is the obvious fit — the repo is on GitHub)
- [x] 84.2 Linux job: bootstrap, `zig build all`, run tests
- [~] 84.3 Cache `.harness/` and ccache between runs — **`.harness/` yes, the
      ccache directory never.** ccache therefore did nothing useful on the Linux
      jobs even when present; [T0121](0121-ci-build-time.md) later measured it at
      2 hits in 615 compiles and removed it rather than persisting it
- [x] 84.4 Windows runner for the native path (T0004)
- [~] 84.5 Verify configure works offline (T0010) — a job with networking blocked
      — **the job asserts on the *result*** (no `_deps/*-src`), which is how a lost
      `FETCHCONTENT_SOURCE_DIR_*` redirect manifests and it fires correctly.
      **Networking is not blocked**, so it is weaker than the local
      `unshare -r -n` proof
- [x] 84.6 Upload `dist/` artifacts on demand
- [~] 84.7 Keep run time low enough that it is not routinely ignored — **the
      split addresses it and no timings were taken here.** The one datum seen was
      a 282s Windows configure against ~28s locally. Measured for real in
      [T0121](0121-ci-build-time.md), which found the fast jobs at 18–21m and
      brought them to 5m

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

---

## Progress (2026-08-03) — written, **not yet observed running**

`.github/workflows/ci.yml` exists and every part of it that can be checked
without GitHub has been. **No run has happened**, so none of the "Done when"
conditions are ticked: each of them is a claim about CI behaviour, and this
ticket stays open until a green run exists. `gh` is not installed on this
machine and there is no API token, so the run cannot be triggered or observed
from here — someone with the repository open has to confirm the first one.

### The workflow

**`ci.yml`** — on push, PR and dispatch. Everything here should finish in
minutes:

| Job | Runner | Does |
|---|---|---|
| `tests-linux-host` | ubuntu-latest | bootstrap, install wine, `zig build test -Dtest=all` — both target suites |
| `tests-windows-host` | windows-latest | `bootstrap.ps1`, `zig build test -Dtest=all -Dtarget=windows` |
| `offline-configure` | ubuntu-latest | configure, then assert nothing was downloaded |

**`full-build.yml`** — nightly (03:17 UTC) and on dispatch:

| Job | Runner | Does |
|---|---|---|
| `build` | ubuntu-latest ×2 | `zig build linux` / `zig build windows`, then `dist` |

The split is the whole design. The full build compiles ~1100 targets per target
and is far too slow to sit between a push and knowing whether it broke
something. What it catches that the fast jobs cannot: breakage in engine code
nothing links against yet — the test binaries pull in only enkiTS,
meshoptimizer and doctest, so a change that breaks DiligentFX compiles nowhere
in `ci.yml`. That is worth knowing nightly, not per-commit.

Note GitHub disables scheduled workflows after 60 days without repository
activity, and runs them only from the default branch.

### Verified locally

The shell embedded in a workflow is the part that usually fails on the first
run, so it was exercised here rather than guessed:

```
$ zig_version=$(grep -m1 '^ZIG_VERSION=' bootstrap.sh | cut -d= -f2)   -> 0.16.0
$ powershell -File vtest.ps1                                            -> 0.16.0, path exists
$ ls -d build/linux-x86_64-release/_deps/*-src | grep -q .              -> no match (passes)
$ (same check against a planted _deps/entt-src)                         -> fires correctly
```

The YAML parses, and all four jobs carry the intended `runs-on`, step counts
and `zig build` invocations.

### Decisions worth recording

**Build trees are not cached.** They are ~1.7 GB (Linux) and ~1.3 GB (Windows).
Caching both would consume most of the repository's 10 GB quota, and restoring
that much is not obviously cheaper than rebuilding it. `.harness/` *is* cached:
635 MB of pure download, keyed on `hashFiles('bootstrap.sh')` so a version bump
invalidates it automatically.

**ccache is deliberately absent, so 84.3 is only half done.** `build.zig`
enables ccache whenever it is on PATH, but that path has *never executed* in
this project — ccache is not installed on the development machine, and the
CMakeCache confirms no `COMPILER_LAUNCHER` was ever set. ccache identifies a
compiler by hashing it, and here the "compiler" is a generated `zig cc` wrapper
script; whether that produces correct hits is unknown. Introducing an untested
variable into the job meant to catch problems is the wrong order. Test ccache
against the shims locally first, then enable it here.

**84.5 is only half done too.** The offline job asserts on the *result* — that
`_deps/` contains no `*-src`, which is exactly how a lost `FETCHCONTENT_SOURCE_DIR_*`
redirect manifests. It does **not** block networking. An earlier draft set
`FETCHCONTENT_FULLY_DISCONNECTED` as an environment variable, which would have
been worse than nothing: CMake reads that as a cache variable and never from
the environment, so the job would have looked like a check and tested nothing.
A runner with genuinely no network is the stronger form and is not set up here.

**The Windows job passes `-Dtarget=windows`.** Without it the harness would also
configure and build the Linux target on that runner — a second full CMake
configure — only to decline to run it, since a Windows runner has neither WSL
nor wine. That degradation is by design (T0012), but paying for it on every
push is not.

**`dist` upload is gated on `workflow_dispatch`.** The staged trees are ~900 MB
(Linux) and ~400 MB (Windows), nearly all static libraries. Uploading 1.3 GB per
push would cost more than it is worth.

### The one requirement deliberately not met

**Resolved: the full build is nightly, not per-push.** The ticket asks for
"both targets build on every push", and that was written that way first. It was
changed deliberately: a cold run downloads ~775 MB of submodules, bootstraps a
635 MB toolchain and compiles ~1100 targets on a 4-core runner, twice — and on
a private repository Windows minutes bill at 2×, so per-push would consume a
monthly allowance quickly. Weighed against "keep run time low enough that it is
not routinely ignored" (84.7), which is the condition that decides whether CI
is worth having at all, nightly wins. Both targets still build, just not on
every commit; anyone who wants one now can dispatch it.

**So the first "Done when" is intentionally not met as literally written.**
Recorded here rather than ticked.

### Next

1. Push, then look at the first run
2. Fix whatever the runner environment disagrees with — likeliest candidates:
   the wine package name on `ubuntu-latest`, and disk headroom on the `build`
   job (submodules + harness + build tree is roughly 3 GB)
3. Record real timings against 84.7, and revisit the cost question above
4. Only then tick the "Done when" conditions and move this to `completed/`

---

## First run (2026-08-03) — 3 of 4 jobs green, one real bug found

| Job | Result |
|---|---|
| Tests (Linux host, both targets) | ✅ passed — both suites, Windows one under wine |
| Configure with FetchContent disconnected | ✅ passed |
| Tests (Windows host, native) | ❌ failed — but 10/10 Zig harness tests passed first |
| Full build | did not run (nightly/dispatch, by design) |

### The failure: ccache was already on the runner

```
C:\Strawberry\c\bin\ccache.exe D:\a\...\toolchain\zig-cxx.cmd -DENKITS_... 
ccache: error: execute_noreturn of ...\toolchain\zig-cxx.cmd
        failed: No such file or directory
```

`build.zig` adopts any ccache it finds on `PATH` as `CMAKE_<LANG>_COMPILER_LAUNCHER`.
The GitHub `windows-latest` image ships one via Strawberry Perl, so ccache
turned itself on unasked and killed every compile before it started.

**This ticket predicted the hazard and still got it wrong.** The note above said
"ccache is deliberately absent... enabling it in CI would be introducing an
untested variable". Not installing something is not the same as it being
absent. The variable was already there.

### The stated cause was also wrong, and measuring said so

The obvious explanation -- ccache execs the compiler directly and a `.cmd` is
not executable without `cmd.exe` -- is **false**. Tested by putting ccache
4.13.6 on this Windows host's PATH and building:

```
pre-fix code + ccache 4.13.6 on PATH:
  CMakeCache: CMAKE_CXX_COMPILER_LAUNCHER=...\ccache.exe
  build: BUILD_EXIT=0            <- works fine
```

So a modern ccache runs the shim without complaint. The real difference is the
**ccache version**: Strawberry Perl's bundled copy cannot, 4.13.6 can. Had the
fix shipped on the first explanation it would have carried a comment stating
something untrue.

### Fix

`build.zig` no longer adopts a PATH-discovered ccache on a **Windows host**.
Not because ccache is broken there, but because whether it works depends on
which ccache the machine happens to have — and this file's own rule is that the
build must not vary with whatever the host ships. The failure is also badly
disguised: it presents as a compile error in third-party code that builds
everywhere else. Linux hosts keep ccache; their shims are `#!/bin/sh` and exec
cleanly.

Verified in both directions, with ccache still on PATH:

```
post-fix: CMakeCache has no COMPILER_LAUNCHER   -> "(none — ccache not adopted)"
post-fix build with ccache on PATH:  BUILD_EXIT=0, doctest Status: SUCCESS
```

If Windows caching is ever wanted, probe the shim through ccache once at
configure time rather than assuming either answer.

### Also fixed

Node 20 deprecation warnings on all three jobs: `actions/checkout@v4` →
`@v7`, `actions/cache@v4` → `@v6`, `actions/upload-artifact@v4` → `@v7`
(the current majors, checked against the release API rather than guessed).

### Still open

- The Windows job has not yet gone green; that is the next thing to confirm
- No timings recorded yet for 84.7. The Windows configure alone took **282s**
  on the runner versus ~28s locally, which is worth knowing before judging
  whether the fast jobs are actually fast
- The nightly full build has never run
- Persisting the ccache directory across runs would make ccache genuinely
  useful on the Linux jobs rather than pure overhead. Not done, not measured

---

## Second run (2026-08-03) — `ci.yml` fully green ✅

All three jobs passed after the ccache fix: Tests (Linux host, both targets),
Tests (Windows host, native), and Configure with FetchContent disconnected.

**The failed first run proved two conditions that are otherwise hard to
demonstrate on purpose:**

- *Failures block the result* — the ccache breakage failed the job, exit 1,
  and nothing downstream pretended otherwise
- *A failure is legible without reproducing locally* — the root cause
  (`C:\Strawberry\c\bin\ccache.exe`, an old ccache adopted off PATH) was
  identified entirely from the CI log. The local reproduction that followed
  was to *test the fix*, not to find the bug

**The bootstrap-from-scratch condition is met too**: the first run had a cold
`.harness` cache on both hosts, so `bootstrap.sh` and `bootstrap.ps1` each
installed and checksum-verified the full toolchain from nothing.

### What CI now covers that nothing did before

| | |
|---|---|
| Linux target, built and tested on Linux | ✅ |
| Windows target, built on Linux, **run under wine** | ✅ — the wine fallback is now proven on a real Linux host, not just asserted from T0001 |
| Windows target, built and run natively on Windows | ✅ — `bootstrap.ps1`, the `.cmd` shims and the case probe's false branch all exercised per push |
| Vendored dependencies resolve without downloading | ✅ |

That last Windows row is the one worth noting: it is a standing, automated
version of what T0004 had to establish by hand.

### Conditions deliberately not met, with reasons

- **"Both targets build on every push"** — the full engine build is nightly
  plus dispatch instead. Decided against the literal wording because 84.7
  ("keep run time low enough that it is not routinely ignored") is the
  condition that decides whether CI gets used at all. Both targets still
  build; not per-commit.
- **84.3, ccache caching** — half done. `.harness/` is cached; the ccache
  directory is not, so ccache does nothing useful on the Linux jobs even when
  present. Persisting it would genuinely help the nightly full build. Not done,
  not measured.
- **84.5, offline** — the job asserts no `_deps/*-src` rather than running with
  networking blocked. Real, but weaker than the local `unshare -r -n` proof.
- **84.7, run time** — no timings recorded. The one number seen so far is a
  **282s Windows configure** on the runner against ~28s locally, a 10x gap that
  deserves attention before anyone calls the fast jobs fast.

### Not yet verified

**`full-build.yml` has never executed.** It is scheduled nightly and has not
fired, so the full ~1100-target build of both targets, the `dist` staging and
the artifact upload are all unproven in CI. That is the last thing standing
between this ticket and `completed/`.

---

## Closing (2026-08-03) — full build dispatched and passed ✅

`full-build.yml` was run manually and passed, which was the last unverified
piece. That covers what no other job touches: the full ~1100-target compile of
**both** targets, `zig build dist` staging, and the artifact upload.

So every workflow in the repository has now executed successfully at least once:

| Workflow | Verified by |
|---|---|
| `ci.yml` — Linux tests, Windows tests, offline configure | green on push |
| `full-build.yml` — both targets, dist, artifacts | green on manual dispatch |

### Closing with four conditions honestly unticked

Following `completed/0007-retire-imgui-probe.md`: a ticket that overstates what
it achieved is worse than one left open.

> **2026-08-05:** all four are now `[~]` rather than `[ ]`. Every one is
> *partly* achieved — the split, `.harness/`, the offline assertion — and none
> is another ticket's, so none was descoped. Left unticked they said the work
> never happened, which is the opposite error to overstating it.

- **"Both targets build on every push"** — not met as written, deliberately.
  The full build is nightly plus dispatch. Chosen against the literal wording
  because 84.7 ("keep run time low enough that it is not routinely ignored")
  decides whether CI gets used at all, and a cold matrix run compiles ~1100
  targets twice on a 4-core runner. Both targets do build; not per-commit.
- **"Build time is reasonable"** and **84.7** — the *design* addresses this
  (fast jobs on push, slow work nightly) but **no durations were recorded**, so
  the claim rests on reasoning rather than measurement. The one datum seen is a
  282s Windows configure on the runner against ~28s locally, a 10x gap nobody
  has explained. Read the timings off the run pages before trusting the split.
- **84.3, ccache** — `.harness/` is cached (635 MB of pure download, the real
  win). The ccache directory is not, so ccache does nothing useful on the Linux
  jobs even when present. Persisting it would genuinely speed the nightly.
- **84.5, offline** — the job asserts no `_deps/*-src` rather than running with
  networking blocked. Real and it fires correctly, but weaker than the local
  `unshare -r -n` proof.

### What this ticket actually bought

Beyond automation, CI immediately found a defect that local work could not:
`build.zig` silently adopting whatever `ccache` sits on `PATH`. It broke only on
a machine that had one — which this one does not. That is the class of problem
CI exists for, and it surfaced on the first run.

It also converts two things from hand-verified to standing checks: the
Windows-host path from T0004 (`bootstrap.ps1`, the `.cmd` shims, the case
probe's false branch) now runs on every push, and wine executing the
cross-built Windows suite is proven on a real Linux host rather than inferred
from T0001.

### Follow-ups worth their own tickets, if wanted

- Persist the ccache directory so the nightly gets faster, and settle whether
  ccache is worth having on Windows hosts by probing the shim at configure time
- Record real job durations against 84.7, and explain the 282s-vs-28s configure
- Publishing to GitHub **Releases** is *not* set up. `dist/` is uploaded as a
  workflow artifact (7-day retention, dispatch only), which is a different
  thing. Releases become meaningful with T0042/T0043, when there is an
  executable to ship rather than ~90 static libraries
