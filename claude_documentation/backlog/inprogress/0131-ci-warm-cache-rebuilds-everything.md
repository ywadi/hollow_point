# T0131 — CI: the restored build tree never prevents a compile

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 1 — Harden the build |
| **Order** | 5 |
| **Created** | 2026-08-05 |
| **Found by** | T0121 |
| **Refs** | T0121, T0084, [../../documentation/03-build-harness.md](../../documentation/03-build-harness.md) |

## Why

T0121 closed twice on "the pinned dependencies stop recompiling", and both
times the mechanism it built did not do that. After the second fix (`fa8dcec`,
unique keys so the cache actually saves), run 30946039676 restored a complete
376 MB tree — right key, tier-1 hit, every object byte-for-byte present — and
then recompiled **all 1476 compile edges anyway**: ~18 minutes of a 22-minute
job spent rebuilding SDL, Diligent, glslang and SPIRV-Tools that had not
changed by a byte.

The cause is not the cache. `actions/cache` round-trips mtimes faithfully
(`tar --posix` save, plain extract), so restored outputs carry the previous
run's timestamps — while `actions/checkout` stamps every source file with
clone time. Every output is therefore older than every input, and ninja's
mtime comparison marks the entire graph dirty. Reproduced locally with the
exact CI tar flags: 792 edges directly "output older than most recent input",
2130 more transitively dirty, 1435 of 1435 scheduled.

The same inversion explains two older mysteries, recorded in T0121 as open:

- **CMake re-ran every run** (`[1/2] Re-running CMake...` in every log):
  build.ninja restored older than every freshly-cloned CMakeLists.txt.
- **ccache hit 2 of 615** and was removed as useless. The re-run rewrites the
  compiler shims each run; ccache's default `compiler_check=mtime` treats a
  re-stamped shim as a new compiler, so nothing ever matched. Reproduced
  locally: a bit-identical rebuild after one CMake re-run hit 16 of 1710.

## The fix

Mtimes must carry the same information git content does. After checkout,
`tools/ci_restore_mtimes.py` stamps:

- every tracked file with the timestamp of the last commit touching it — the
  pushed commit's files come out newer than the cached outputs and rebuild;
  everything else predates them and is left alone;
- every submodule file with max(submodule HEAD commit time, time the pin last
  moved in the superproject, containing tree's stamp) — the pin-move term
  keeps a rollback honest, since checking out an *older* pin must still look
  newer than the previous run's outputs.

The script deepens the shallow clone's commit graph itself
(`git fetch --filter=blob:none --unshallow`, commits and trees only — blobs
are not needed to attribute paths). Anything it cannot attribute keeps its
checkout mtime and simply rebuilds: the failure mode is wasted work, never a
stale binary.

## Done when

- [ ] A warm CI run's build step does no compilation — the log shows
      `ninja: no work to do` (or only edges a real change dirtied), not a
      748/779-edge rebuild
- [ ] Both test jobs are materially faster warm, with before/after wall-clock
      numbers from real runs pasted below
- [ ] The wrong figures this ticket found are corrected where they live
      (`ci.yml` comments, T0121)

## Subtasks

- [x] 131.1 Verify the inversion diagnosis rather than inheriting it — done
      locally: CI tar flags round-trip preserves mtimes to the nanosecond,
      `ninja -d explain` shows 792 × "output older than most recent input"
- [x] 131.2 Explain T0121's ccache numbers before touching anything — done,
      see Notes: "82 of ~4,248" was a misread of the stats format, and the
      real killers were shim rewrites (compiler_check=mtime) plus 529 of 615
      calls uncacheable under the apt ccache 4.9.1
- [x] 131.3 Write `tools/ci_restore_mtimes.py` and prove it locally — restored
      tree + fresh-checkout mtimes goes from 1435 scheduled edges to
      `ninja: no work to do` in 0.06 s, and a touched source still rebuilds
      (1 compile + 9 relinks)
- [x] 131.4 Wire it into both test jobs in `ci.yml`
- [ ] 131.5 Measure in real CI: dispatch on a scratch branch, warm run,
      paste the numbers
- [x] 131.6 Correct the stale figures in `ci.yml` (trees ARE cached; 376 MB +
      156 MB per run measured off 30946039676) and annotate T0121

## Notes / findings

**T0121's "ccache saw 82 of roughly 4,248 compilations" was a misreading, and
the unexplained number it bequeathed is now explained.** The stats block in run
30884687594 actually reads:

```
Cacheable calls:    86 / 615 (13.98%)
  Hits:              2 /  86 ( 2.33%)
Uncacheable calls: 529 / 615 (86.02%)
```

615 was the *entire* compile graph of that era, not a fraction of it — the
launcher wrapped everything. Two separate defects made it useless:

1. **529 of 615 uncacheable under apt ccache 4.9.1** — suspiciously close to
   the 531 SDL C files. Locally, ccache 4.10.2 on the same tree reports 73%
   cacheable, with every uncacheable call "Could not use precompiled header"
   (Diligent's and SDL's PCH consumers). The 4.9.1 reason was never printed
   (`--show-stats` without `-v`) and is now moot.
2. **The mtime inversion re-ran CMake every run, rewriting the shims**, and
   `compiler_check=mtime` hashes the compiler's mtime — so even the 86
   cacheable calls could never hit across runs. Reproduced locally: 16 hits
   of 1710 on a bit-identical rebuild whose only difference was one CMake
   re-run.

**A warm ccache is not the lever here anyway, measured:** a full local rebuild
of the Linux target took 332 s cold and 291 s with every object in a warm
ccache — zig cc's per-invocation work dominates. Avoiding the ninja edge
entirely (this ticket's fix) took 0.06 s. If a compiler launcher is ever
reconsidered, it needs `compiler_check=content` (the shims embed the pinned
zig path, so content pins the version), PCH sloppiness or PCH off, and a
pinned ≥4.10 ccache — and it still only softens cold builds, which the tree
cache already makes rare.

**The pre-Diligent history reads differently with this understood.** The
"3m38s era" was never the cache avoiding compiles — phase timings show 661
compiles ran in ~127 s there, the same full-rebuild pacing as every other era.
What the restored tree avoided was the ~14-minute *fresh configure* (job start
06:38 → first "Configuring done" 06:52:46 in run 30884687594; a CMake re-run
in a restored tree is ~85 s). The tree cache has been a configure cache all
along; compile avoidance starts with this ticket.

**Figures corrected while here:** `ci.yml`'s header said build trees are not
cached (they are, since 17f50b1) at ~1.7 GB/~1.3 GB (measured 2.1 GB / 879 MB,
compressing to 376 MB / 156 MB per run — read off run 30946039676's actual
save, as T0121's amendment asked). Quota: ~532 MB per run against 10 GB, an
LRU window of roughly 18 runs alongside the two ~250 MB toolchain entries.

### Measured — to be filled from the scratch-branch runs

(pending)

### Not verified

- A **pin bump** under honest mtimes: expectation is the tier-2 restore plus
  the bumped submodule's (and only its) rebuild, because its files stamp to
  the new pin's commit time. Expected, not yet observed.
- Windows-host `os.utime`/git behaviour is exercised only by the CI runs
  below, not locally.
