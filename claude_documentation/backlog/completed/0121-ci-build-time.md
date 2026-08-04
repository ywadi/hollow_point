# T0121 — CI build time

| | |
|---|---|
| **Status** | ✅ DONE |
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

> **Those sizes were wrong** — measured during this ticket at 327 MB and 152 MB,
> compressing to 78 MB and 34 MB. The quota objection never applied. Left here
> as written because it is what the decision was actually made on; see the
> 2026-08-04 notes below.

## Done when

- [x] The Linux job is materially faster, with before/after numbers recorded —
      **18–19m → 5m**, measured cold and warm across runs 30891771048 and
      30893046876; the table under Notes carries both, and the Windows job went
      23m → 5m alongside it
- [x] Whatever mechanism is used reports its own effectiveness, so a
      silently-useless cache cannot persist — both test jobs end with a
      **Build tree size** step (`if: always()`), and a restore that hit shows a
      tree already near full size before the build step runs
- [x] The cache stays inside the repository's quota — measured at **78 MB
      (Linux) + 34 MB (Windows) compressed**, about 1% of the 10 GB. The
      original ~1.7 GB / ~1.3 GB estimate that kept this off the table was
      wrong by 5–9x

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
- [x] 121.6 **Record the before/after numbers from a real run** — cold and warm,
      both jobs. Done: runs 30891771048 (cold) and 30893046876 (warm), tabulated
      under Notes with the cache-hit lines quoted

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

### 2026-08-04 — measured: run 30891771048

The first run after the fix. Linux was cold by construction (new key prefix;
ccache's entries used a different one), so it built from scratch and *saved* the
cache. Windows hit an exact key — nothing in the commit touched a submodule or
a CMake file, so `.ci-dep-key` hashed identically to the entry run 30888418425
had already saved.

| Job | Before | This run | Cache |
|---|---|---|---|
| Tests (Windows host, native) | 23m (`30888418425`, cold) | **6m** | exact-key hit |
| Tests (Linux host, both targets) | 18m (`30888418425`) / 19m (`30884687594`, ccache) | 16m | cold — populated it |

Windows, verbatim:

```
Cache hit for: buildtree-Windows-bdeeef15b3ac65b4007cf7bf758857e3e15deca7...
Cache Size: ~34 MB (35906558 B)
Cache restored successfully
152 MB                                        <- Build tree size
Cache hit occurred on the primary key ..., not saving cache.
```

Linux, verbatim:

```
Cache not found for input keys: buildtree-Linux-83c6def5164c7d4d6cb352207...
327M	build                                  <- Build tree size
Sent 78564880 of 78564880 (100.0%), 34.0 MBs/sec
Cache saved with key: buildtree-Linux-83c6def5164c7d4d6cb3522071df6455d34...
```

**The premise for not caching build trees was wrong, and by a lot.** The
original judgement — recorded in the Why above and in `ci.yml`'s own comment —
was that trees are ~1.7 GB Linux and ~1.3 GB Windows against a 10 GB quota.
Measured, they are **327 MB and 152 MB**, compressing to **78 MB and 34 MB**.
Both entries together are about 1% of the quota. The size objection that kept
this ticket's fix off the table never actually applied; nobody had measured it.
`ci.yml`'s comment is corrected to say so.

The 16m Linux figure above is a cold build, within noise of the 18–19m
baselines — it is not the speedup. The cache only exists from the end of that
run onward, so the number that matters came from the next one.

### 2026-08-04 — measured warm: run 30893046876

Triggered with `gh workflow run ci.yml --ref main`. `workflow_dispatch` rather
than a commit on purpose: `paths-ignore` excludes `claude_documentation/**` and
`*.md`, so a docs-only commit does not trigger CI, and inventing a dummy source
change to force one would have polluted the history for a measurement.

| Job | Before | Cold (`30891771048`) | **Warm (`30893046876`)** |
|---|---|---|---|
| Tests (Linux host, both targets) | 18m / 19m | 16m | **5m** |
| Tests (Windows host, native) | 23m / 22m | 6m | **5m** |

Both jobs hit their primary key and correctly declined to re-save:

```
Cache hit for: buildtree-Linux-83c6def5164c      Cache Size: ~75 MB   333M build
Cache hit for: buildtree-Windows-bdeeef15b3ac    Cache Size: ~34 MB   152 MB
... not saving cache.
```

**The Linux job went from 18–19 minutes to 5.** That is the ticket's headline
condition, and it is now met rather than asserted. Total workflow wall clock
falls from ~23m to ~5m, since the two heavy jobs run in parallel and both are
now warm.

Two things this does *not* prove, recorded so nobody reads more into it:

- **A pin bump still costs a full cold build.** The key is the submodule SHAs,
  so moving any pin — SDL, Diligent, or one of the nested Diligent submodules —
  invalidates it and the next run pays 16–18m again. That is the intended
  trade and the reason the key is coarse; it is not a regression when it
  happens.
- **`restore-keys` makes a near-miss cheap but not free.** On a pin bump the
  fallback restores the most recent tree and ninja rebuilds what actually
  changed, which is better than cold but was not separately measured here.

**The ccache question is abandoned, not answered.** 82 of ~4,248 compilations
reached the launcher and nobody found out why. Caching output sidesteps it
rather than solving it. Anyone reaching for a compiler launcher here again
inherits that unexplained number as their starting point.

## Amendment (2026-08-04) — the cache never saved, and two figures here are wrong

This ticket closed on "the Linux job goes 18-19m to 5m, measured". That held for
about a day. It is now 22m22s, and the cause is in the mechanism this ticket
built rather than in anything since.

### The cache restored and never wrote

`actions/cache` writes an entry **only when the primary key misses**. The key
here — submodule SHAs plus root `CMakeLists.txt` and toolchains — hit on every
run, so the tree was never updated. That was invisible for exactly as long as
the cached tree happened to be complete.

It stopped being complete when `engine/CMakeLists.txt` linked Diligent's Vulkan
and OpenGL backends (T0025.1, `15ee38c`). Nothing in the key moved — the
submodules had not changed and Diligent was already vendored — but **which parts
of it got compiled** did. From the job log:

```
18:31:26  Cache restored from key: buildtree-Linux-b8f9632…   Cache Size: ~78 MB
18:52:05  2.1G   build
18:52:05  Cache hit occurred on the primary key buildtree-Linux-b8f9632…, not saving cache.
```

Restore a 78 MB pre-Diligent snapshot, compile ~955 targets of backend and
shader-compiler code (glslang, SPIRV-Tools, SPIRV-Cross) into a 2.1 GB tree,
discard it. Every run, indefinitely, until something *in the key* changed.

Exact boundary, same step, nothing else different:

| job, "Run every test bucket" | `929ce9b` before | `15ee38c` after |
|---|---|---|
| Tests (Linux host, both targets) | 3m38s | **20m38s** |
| Tests (Windows host, native) | 7m48s total | **18m40s** total |

### Two figures recorded here are now wrong

- **"18-19m to 5m."** True when measured; the warm path is 22m22s today.
- **"the trees are 327 MB and 152 MB, compressing to 78 MB and 34 MB … about 1%
  of quota."** Measured before Diligent was linked. The trees are **2.1 GB and
  879 MB**. The original 1.7 GB / 1.3 GB estimate this ticket dismissed as
  "wrong by roughly 5-9x" was in fact roughly right, just early.

### The fix, and what it is not

The key is now unique per run (`…-${{ github.run_id }}`) with two restore-key
tiers, so **every run saves** and the cached tree is always what the build
actually produced. Restore prefers a tree built against the same pinned
dependencies, then any tree.

The fix deliberately is **not** "add `engine/CMakeLists.txt` to the key". That is
the same design one input wider, and the lesson is that a key which must
enumerate everything affecting the build graph is a bet this repository already
lost once, silently. A unique key cannot go stale; it can only cost storage.

**Cost, stated because it is real rather than a rounding error:** saving every
run instead of once. Repo cache usage was 1.33 GB of 10 GB at the time of the
change, and eviction is least-recently-used, so the effect is a rolling window
of recent trees. The compressed sizes are an **estimate** (~5x ratio, so roughly
565 MB per run) until a run actually writes one — no post-Diligent tree had ever
been saved. **Read the real numbers off the first successful save and correct
this again**, and if they are much worse than estimated, narrowing the cached
path to `build/*/third_party` is the lever.

### Not fixed here

ccache is unrelated and remains correctly absent — this ticket removed it after
measuring 82 of ~4,248 compilations. The leftover `ccache-linux-*` cache entries
predate that removal and will age out on their own.

## Amendment (2026-08-05) — superseded by T0131, and two claims here corrected

The unique-key fix made the cache *save*; it still bought zero compile
avoidance, because the checkout's mtimes are always newer than the restored
outputs and ninja rebuilds the whole graph regardless. That, the every-run
CMake re-run, and this ticket's ccache numbers all turned out to be one
mechanism — diagnosed, fixed and measured in
[T0131](0131-ci-warm-cache-rebuilds-everything.md).

Corrections to what this file says, so nobody inherits it as written:

- **"82 of ~4,248 compilations" misread the stats.** The block in run
  30884687594 reads `Cacheable calls: 86 / 615` — ccache saw the *entire*
  615-compile graph; 529 calls were uncacheable under apt ccache 4.9.1, and
  the CMake re-run rewrote the compiler shims each run, which
  `compiler_check=mtime` treats as a new compiler. The launcher wiring this
  ticket suspected was fine.
- **The measured "estimate" is now measured:** 376 MB (Linux) + 156 MB
  (Windows) per run, read off run 30946039676's save, versus the ~565 MB
  guessed above.
- **The 3m38s era was never compile avoidance** — 661 compiles ran in ~127 s
  there; what the restored tree avoided was the ~14-minute fresh configure.
  This cache has been a configure cache all along.
