# T0122 — Zig cannot build from a working tree on `/mnt/c` under WSL

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Simple |
| **Phase** | 1 — Harden the build |
| **Order** | 1 |
| **Created** | 2026-08-04 |
| **Found by** | T0100 |
| **Blocks** | T0123 |
| **Found** | T0125 |
| **Refs** | T0102, T0004, T0125, [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D3, D5, D18 |

## Why

`zig build` fails on WSL when the working tree is on `/mnt/c`, before compiling
anything:

```
error: failed to rename compilation results ('.zig-cache/tmp/0f37fe50ce00be6e')
       into local cache ('.zig-cache/o/e238ee67df134356a93b6a24a3f149a0'):
       AccessDenied
```

This blocked T0100's local verification entirely; that ticket was verified
through CI instead.

### The actual cause

A build cache must never be observable half-written, so Zig writes compilation
output into `.zig-cache/tmp/<hash>/` and then **renames that directory** into
`.zig-cache/o/<hash>`. `rename()` is atomic, which is what makes the cache safe
against interruption and concurrent builds.

On Linux you may rename a directory while files inside it are still open — an
open handle refers to the inode, not the path. **On Windows you may not**, and
`/mnt/c` is not a Linux filesystem: it is a 9p bridge onto NTFS with Windows
performing the operation. Windows' rule leaks through, and the rename is
refused.

Isolated to a single variable:

| Test | Result |
|---|---|
| Rename a directory on `/mnt/c`, nothing open inside | OK |
| Rename a directory on `/mnt/c`, one file held open inside | **`EACCES`** |
| Rename a directory on ext4, one file held open inside | OK |

So it is not permissions (the tree is `drwxrwxrwx`), not slowness, and not
corruption. It is a POSIX guarantee the compiler depends on that drvfs does not
implement — which is also why it cannot be fixed by relocating paths *within*
`/mnt/c`.

### It is a known upstream bug, and it is not being fixed

- [ziglang/zig#24955 — "Zig cannot compile on Windows filesystem in WSL"][i]
  is **open**, unassigned, with no maintainer response. Its recommended
  workaround is to use a Linux filesystem rather than a `/mnt/` mount.
- [PR #30588][p], which would have fixed it by extending Zig's existing Windows
  `AccessDenied` retry to WSL, is **closed and unmerged**.
- It is a regression: **0.14 and earlier worked; 0.15.1 broke it.** This project
  pins **0.16.0**, so it is affected, and pinning backwards is not an option.

Waiting is therefore not a strategy, and a local workaround would mean carrying
a patch upstream has already declined.

[i]: https://github.com/ziglang/zig/issues/24955
[p]: https://codeberg.org/ziglang/zig/pulls/30588

### What this ticket originally claimed, and why that was wrong

The first version of this ticket asserted a **host collision** — that the Linux
and Windows toolchains were fighting over shared `.zig-cache/` and `build/`
paths, the same shape as T0102. That was a guess, and the evidence refutes it:
the rename destination **did not exist** (`No such file or directory`), and the
hash differed on every run. Nothing was colliding.

Recorded because the wrong diagnosis pointed at the wrong fix: host-keying the
cache path, as T0102 did for `.harness/`, would **not** have worked — a
host-keyed cache is still on `/mnt/c` and still cannot be renamed.

## The decision

**The WSL working tree moves to the Linux filesystem.** WSL then *is* a native
Linux host — the row CI already proves green every run — and produces both
targets by cross-compilation exactly as D3 requires.

Recorded as **D18** in the decision log, with the alternatives that were
rejected.

What this costs: one working tree cannot serve both hosts, because no filesystem
gives Windows and Linux correct semantics simultaneously (`/mnt/c` breaks Linux,
`\\wsl$` breaks Windows). Native Windows-host building on that tree is given up.
It remains covered by CI's `tests-windows-host` job, which exists for exactly
that purpose (T0004), so the 2×2 matrix stays honest even though no developer
exercises it by hand.

What this costs in build-system work: **nothing.** No wrapper, no environment
variable, no `build.zig` change. That is the main argument for it over the
alternatives in D18.

## Done when

- [x] A clone on the Linux filesystem builds both targets under WSL —
      `zig build test -Dtest=fast` green, output pasted here
- [x] `BUILDING.md` says the tree must not live on `/mnt/*` under WSL, and why,
      with a link to the upstream issue so the constraint is not mistaken for a
      local quirk
- [x] D18 recorded in the decision log
- [x] T0102's host-keying is reviewed: still correct and worth keeping for a
      genuine dual-host tree, but no longer the thing standing between a WSL
      developer and a working build. Say so there rather than leaving it
      implying more coverage than it has

## Subtasks

- [x] 122.1 Clone to the Linux filesystem, `--recursive` (the `/mnt/c` tree has
      `third_party/SDL` uninitialized), bootstrap, and build
- [x] 122.2 `BUILDING.md` — the constraint, the reason, the upstream link
- [x] 122.3 D18 in the decision log
- [x] 122.4 Note on T0102 that it solved a different problem from this one
- [x] 122.5 Decide the fate of the `/mnt/c` tree: keep as the Windows-host
      checkout, or retire it. Do not delete before 122.1 is green

## Evidence

The tree now lives at `~/Development/hollow_point`, on ext4:

```
$ df -T .
Filesystem     Type  1K-blocks      Used Available Use% Mounted on
/dev/sdd       ext4 1055762868 139669536 862389860  14% /
```

Submodules are all initialized, including the `third_party/SDL` that was missing
from the `/mnt/c` tree, and `git status` is clean — which is itself a result.
The permanently-dirty `third_party/sysroot/linux-x86_64/include/X11/bitmaps/stipple`
predicted below **is gone**, exactly as expected: ext4 is case-sensitive and
holds both `Stipple` and `stipple`.

### The rename that `/mnt/c` refuses now succeeds

The first `zig build test -Dtest=fast` of the session reported `ninja: no work
to do` for both targets — the tree had been built before the session started, so
that run proved the suites pass but exercised no compilation. Two further runs
close that gap.

**Discarding the whole Zig cache** forces the `tmp/<hash>` → `o/<hash>` directory
rename that is the entire cause of this ticket:

```
$ rm -rf .zig-cache            # 78M discarded
$ zig build test -Dtest=fast
[0/2] Re-checking globbed directories...
ninja: no work to do.
[0/2] Re-checking globbed directories...
ninja: no work to do.
[doctest] test cases:     24 |     24 passed | 0 failed | 0 skipped
[doctest] assertions: 213091 | 213091 passed | 0 failed |
[doctest] Status: SUCCESS!
[doctest] test cases:     24 |     24 passed | 0 failed | 0 skipped
[doctest] assertions: 213091 | 213091 passed | 0 failed |
[doctest] Status: SUCCESS!
EXITCODE=0

real    0m6.008s
```

And the rename destination was repopulated — five committed cache objects where
`/mnt/c` produced `AccessDenied` before compiling anything:

```
$ ls .zig-cache/o/ | wc -l
5
$ du -sh .zig-cache
73M     .zig-cache
```

**Touching a source** proves the C++ path for both targets, not just the cache:

```
$ touch tests/fast/guid_test.cpp && zig build test -Dtest=fast
[1/3] Building CXX object tests/CMakeFiles/hp_tests_fast.dir/fast/guid_test.cpp.o
[2/3] Linking CXX executable tests/hp_tests_fast
[1/3] Building CXX object tests/CMakeFiles/hp_tests_fast.dir/fast/guid_test.cpp.obj
[2/3] Linking CXX executable tests/hp_tests_fast.exe
[doctest] test cases:     24 |     24 passed | 0 failed | 0 skipped
[doctest] assertions: 213091 | 213091 passed | 0 failed |
[doctest] Status: SUCCESS!
[doctest] test cases:     24 |     24 passed | 0 failed | 0 skipped
[doctest] assertions: 213091 | 213091 passed | 0 failed |
[doctest] Status: SUCCESS!
EXITCODE=0
```

Both targets compiled (`.o` and `.obj`/`.exe`), both suites ran, 48 cases and
426182 assertions across the two, zero failures.

### What was *not* observed

**No full cold ~1100-target build was watched in this session.** The build trees
under `build/` were produced on this ext4 tree earlier the same day, before the
session began; what is proven above is the Zig cache commit from empty and
incremental C++ compilation and linking for both targets. The full-tree build on
this filesystem is inferred from those, not observed end to end. It is a weak
inference to have to make and a cheap one to retire — `zig build all` on a fresh
tree would settle it.

### The `/mnt/c` tree (122.5)

**Decision: retire it.** `/mnt/c/Development/hollow_point`, 6.7 GB, is no longer
a working checkout of anything — D18 makes it unbuildable by design. The owner
removes it from the Windows side; it was deliberately not deleted from here.

Checked before advising that, because "delete 6.7 GB" deserves evidence rather
than assurance — it holds nothing that exists only there:

```
$ git -C /mnt/c/Development/hollow_point status --short
 M third_party/sysroot/linux-x86_64/include/X11/bitmaps/stipple

$ git -C /mnt/c/Development/hollow_point log --oneline origin/main..HEAD
(no output — nothing unpushed)
```

The single modified file is the case-collision artifact described below, which
that filesystem cannot represent and which is not a real edit. No unpushed
commits, no other uncommitted work.

Windows-host coverage therefore rests entirely on CI's `tests-windows-host` job
(T0004), which is precisely what D18 says it does. Nobody exercises that row by
hand any more, and the trade was made knowingly: the alternative was keeping
6.7 GB and a second toolchain for a row CI already proves every push.

## Notes / findings

**Verification is the clone, not reasoning.** This ticket has already been wrong
once by reasoning confidently from a plausible cause. It closes when a build on
a Linux-filesystem tree is green and the output is in this file.

**A side effect worth expecting:** the permanently-dirty
`third_party/sysroot/linux-x86_64/include/X11/bitmaps/stipple` disappears. It is
a case-collision — the repo tracks both `Stipple` and `stipple`, and a
case-insensitive Windows filesystem can only hold one, so whichever git writes
last reports the other as modified forever. ext4 is case-sensitive and holds
both. If that file is *still* dirty in the new tree, the tree is not where it
was meant to be.

**`df -h ~` is the check that matters** before trusting the move: `ext4` or
`overlay` is right, `9p` or `drvfs` means nothing was actually gained.

---

**Closed 2026-08-04.** Both predictions in this section held: the tree is on
ext4 and the `stipple` case-collision resolved itself. The evidence is above.

**A pre-warmed build tree nearly produced a hollow verification.** The first
`zig build test -Dtest=fast` returned exit 0 with two green suites — and
`ninja: no work to do` for both targets, because the tree had been built before
the session. Exit 0 there proves the *tests* pass; it proves nothing about the
thing this ticket is about, which is whether Zig can commit its cache on this
filesystem. Discarding `.zig-cache` and touching a source is what turned it into
evidence. This is the same trap the top of this file warns about, met in a
different disguise: the ticket had already been wrong once by reasoning from a
plausible cause, and a green exit code is just as plausible a cause to reason
from.

**The Windows suite runs under wine here, not WSL interop — filed as T0125.**
`/proc/sys/fs/binfmt_misc/WSLInterop` reads `enabled` and wine has no binfmt
registration, yet wine's own startup warning appears in the output above, so
`build.zig`'s `wslInteropEnabled()` is returning false and `runnerFor` is
falling through to its wine branch. The likely reason is that `/proc` files
report a stat size of zero while holding real content (measured: `stat` says 0,
`wc -c` says 56), so a stat-sized read comes back empty. Not confirmed — the
probe needed more Zig 0.16 `std.Io` scaffolding than was worth writing mid-
ticket, and T0125 says so rather than asserting the mechanism.

It changes nothing about this ticket: both suites pass under wine, and wine is a
supported runner that T0001 proved. What it costs is the *higher-fidelity* path
silently, on what is now — post-D18 — the primary way this project is developed.
Kept out of this ticket deliberately; it is an unrelated defect that this work
merely walked past.
