# T0122 — Zig cannot build from a working tree on `/mnt/c` under WSL

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Simple |
| **Phase** | 1 — Harden the build |
| **Order** | 1 |
| **Created** | 2026-08-04 |
| **Found by** | T0100 |
| **Blocks** | T0123 |
| **Refs** | T0102, T0004, [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D3, D5, D18 |

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

- [ ] A clone on the Linux filesystem builds both targets under WSL —
      `zig build test -Dtest=fast` green, output pasted here
- [ ] `BUILDING.md` says the tree must not live on `/mnt/*` under WSL, and why,
      with a link to the upstream issue so the constraint is not mistaken for a
      local quirk
- [ ] D18 recorded in the decision log
- [ ] T0102's host-keying is reviewed: still correct and worth keeping for a
      genuine dual-host tree, but no longer the thing standing between a WSL
      developer and a working build. Say so there rather than leaving it
      implying more coverage than it has

## Subtasks

- [ ] 122.1 Clone to the Linux filesystem, `--recursive` (the `/mnt/c` tree has
      `third_party/SDL` uninitialized), bootstrap, and build
- [ ] 122.2 `BUILDING.md` — the constraint, the reason, the upstream link
- [ ] 122.3 D18 in the decision log
- [ ] 122.4 Note on T0102 that it solved a different problem from this one
- [ ] 122.5 Decide the fate of the `/mnt/c` tree: keep as the Windows-host
      checkout, or retire it. Do not delete before 122.1 is green

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
