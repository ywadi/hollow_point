# T0121 — CI build time

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
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
- [x] Whatever mechanism is used reports its own effectiveness, so a
      silently-useless cache cannot persist — both test jobs end with a
      **Build tree size** step (`if: always()`), and a restore that hit shows a
      tree already near full size before the build step runs
- [ ] The cache stays inside the repository's quota

## Subtasks

- [x] 121.3 Cache the third-party build outputs keyed on their **submodule
      SHAs** — coarse, but they change only when a pin moves, so the hit rate
      should be near 100%. Done for both test jobs; the tree is cached whole
      rather than per-library, which is simpler and covers enkiTS, ozz and
      meshoptimizer as well as SDL and Diligent
- [x] 121.4 Consider whether the Linux job needs to build *both* targets on every
      push. **Decided: keep both.** The duplication is what proves the
      cross-compile matrix, and with the tree cached the second target's warm
      cost is relinking plus whatever engine files changed — the cache removes
      the reason to cut it. Revisit only if the warm number below says otherwise
- [x] 121.5 Windows cannot use ccache — `build.zig` declines a PATH-discovered
      one there because the runner's copy cannot exec the generated `.cmd`
      compiler shim (T0084). Anything done for Windows must be tree-based.
      Satisfied: Windows gets the same tree cache, which is its only lever
- [ ] 121.6 **Record the before/after numbers from a real run** — cold and warm,
      both jobs. Nothing above is proven until this is filled in

### Not pursued

121.1 (*find out why ccache saw almost nothing*) and 121.2 (*measure a warm
ccache run*) were dropped rather than completed. ccache is gone, so the question
no longer gates anything — but it was **never answered**, and that is worth
keeping rather than quietly deleting: if a future change reaches for a compiler
launcher again, it starts from the same unexplained 2.44% and should expect to
have to diagnose it. See the note below.

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

**The Windows timeout was not transient, and is now diagnosed and fixed** — see
G9 in `04-cross-compile-gotchas.md`. Zig's build runner has a hardcoded
60-second floor on the test-binary handshake, measured in real time rather than
CPU time, and the Windows scheduler misses it when ninja is saturating the
machine. `build.zig` now runs the Zig suites after the C++ build rather than
alongside it, which is what Zig's own CI does.

It is recorded here because it is the same underlying problem as this ticket:
**the build is heavy enough to starve things.** Fixing build time reduces the
pressure that produced it.

---

### 2026-08-04 — the build-tree cache landed on Windows only, and was missing the job this ticket is about

`17f50b1` ("CI: cache the build tree, so pinned dependencies stop recompiling")
removed ccache and added the tree cache — but added it **only to
`tests-windows-host`**. `tests-linux-host` lost its ccache and got nothing back,
so the job this ticket exists to fix went from marginal caching to none.

Worse, the commit left the **Build tree size** step in the Linux job with the
comment *"a cache that silently misses looks exactly like one that works"* —
reporting on a cache that was not there. That is exactly the false signal the
step was added to prevent, pointed the wrong way.

Now fixed. `tests-linux-host` has the same two steps as Windows:

```yaml
- name: Key the build cache on the pinned dependencies
  run: |
    git submodule status --recursive | awk '{print $1, $2}' > .ci-dep-key
    sha256sum CMakeLists.txt cmake/toolchains/*.cmake >> .ci-dep-key
- name: Cache the build tree
  uses: actions/cache@v6
  with:
    path: build
    key: buildtree-${{ runner.os }}-${{ hashFiles('.ci-dep-key') }}
    restore-keys: |
      buildtree-${{ runner.os }}-
```

Three things worth knowing about the shape of it:

- **`path: build` covers both targets.** `build.zig:188` lays trees out as
  `build/<target>-<buildtype>`, so the Linux job's entry holds
  `linux-x86_64-*` *and* `windows-x86_64-*`; the Windows job passes
  `-Dtarget=windows` and so holds only the latter. `runner.os` in the key keeps
  the two entries apart, which they must be — they are not interchangeable.
- **`api-docs-current` and `offline-configure` are deliberately excluded.** The
  first runs `zig build docs` (a libclang parse, not a compile); the second runs
  `zig build configure` only, and a pre-populated `build/` would actively defeat
  its purpose, which is to prove every dependency resolves to a vendored
  checkout with nothing downloaded.
- **`.ci-dep-key` is now gitignored.** CI writes it into the repo root; local
  builds never produce it, but an untracked stray is noise `offline-configure`
  would otherwise have to account for.

**Not yet measured, and therefore not closed.** Everything above is structural —
the workflow parses and the steps are in the right jobs, but no run has produced
a number. The ticket stays in `inprogress/` until 121.6 is filled in with a cold
and a warm run for both jobs. Per this backlog's own rule, a ticket that claims
a speedup it has not observed is worse than one left open — and this ticket has
already been burned once by a cache that *looked* like it was working.

**The ccache question is abandoned, not answered.** 82 of ~4,248 compilations
reached the launcher and nobody found out why. Caching output sidesteps it
rather than solving it. Anyone reaching for a compiler launcher here again
inherits that unexplained number as their starting point.
