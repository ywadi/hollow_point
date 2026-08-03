# T0121 — CI build time

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 1 — Harden the build |
| **Order** | 5 |
| **Created** | 2026-08-04 |
| **Refs** | T0084, [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D5 |

## Why

The Linux CI job takes **18–21 minutes** and only grows. It was ~4 minutes before
SDL3 (T0015) added 568 source files compiled for both targets, and Tracy (T0029),
Jolt (T0051) and the reflection work are still to come.

`ci.yml`'s own reasoning is that "a CI run slow enough to be ignored provides no
signal" — which is why the fast jobs were split from `full-build.yml` at all.
That threshold is close.

Build trees are deliberately **not** cached: ~1.7 GB Linux and ~1.3 GB Windows
against a 10 GB repository quota. So every run recompiles SDL and Diligent from
scratch, twice.

## Done when

- [ ] The Linux job is materially faster, with before/after numbers recorded
- [ ] Whatever mechanism is used reports its own effectiveness, so a
      silently-useless cache cannot persist
- [ ] The cache stays inside the repository's quota

## Subtasks

- [ ] 121.1 **Find out why ccache sees almost nothing** (see below) — this
      blocks any judgement about whether ccache is the right tool
- [ ] 121.2 If the launcher can be made to cover the bulk of compilation, measure
      a warm run and decide on the evidence
- [ ] 121.3 Otherwise, cache the `third_party/SDL` and `third_party/DiligentEngine`
      build outputs keyed on their **submodule SHAs** — coarse, but they change
      only when a pin moves, so the hit rate would be near 100%
- [ ] 121.4 Consider whether the Linux job needs to build *both* targets on every
      push. Some duplication is deliberate (it proves the cross-compile matrix);
      not all of it needs to be per-push
- [ ] 121.5 Windows cannot use ccache — `build.zig` declines a PATH-discovered
      one there because the runner's copy cannot exec the generated `.cmd`
      compiler shim (T0084). Anything done for Windows must be tree-based

## Notes / findings

**ccache was wired into the Linux job and did not earn its place.** First (cold)
run, as expected, was *slower*: 18m16s → 21m06s. But the statistics show why a
warm run would not have helped either:

```
Hits: 2 / 82 (2.44%)   Misses: 80 / 82 (97.56%)
```

**Only 82 compilations went through ccache**, against a build of ~4,248 targets
of which SDL alone is 568 source files. Even a perfect cache over 82 compiles
saves almost nothing.

Why is **not yet known**, and the candidates are worth listing rather than
guessing between: `build.zig` passes `CMAKE_C/CXX_COMPILER_LAUNCHER` at configure
time and something about the two-target test build may not carry it into both
trees; or most compilation may reach the compiler by a path the launcher never
wraps. This is a hypothesis, not a diagnosis — measure it before acting on it.

**Adding `--show-stats` to the job was the part that paid off**, and is worth
keeping whatever mechanism wins: without it the cache would have looked like it
was working, because the job was green and the cache step said "Cache hit".

**A transient Windows failure was observed once and did not reproduce**: two Zig
harness runners (`harness-paths`, `harness-cache`) timed out at 60s while the
runner was compiling SDL, then passed on the next run untouched. Consistent with
resource starvation on a loaded runner, which is another reason to care about
build time — but one observation is not a diagnosis.
