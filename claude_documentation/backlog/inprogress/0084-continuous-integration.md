# T0084 — Continuous integration

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
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

- [x] 84.1 Choose CI (GitHub Actions is the obvious fit — the repo is on GitHub)
- [x] 84.2 Linux job: bootstrap, `zig build all`, run tests
- [ ] 84.3 Cache `.harness/` and ccache between runs
- [x] 84.4 Windows runner for the native path (T0004)
- [ ] 84.5 Verify configure works offline (T0010) — a job with networking blocked
- [x] 84.6 Upload `dist/` artifacts on demand
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
