# T0122 — `.zig-cache` and `build/` collide across hosts, the way `.harness/` used to

| | |
|---|---|
| **Status** | 🚧 IN PROGRESS |
| **Priority** | High |
| **Complexity** | Simple |
| **Phase** | 1 — Harden the build |
| **Order** | 1 |
| **Created** | 2026-08-04 |
| **Found by** | T0100 |
| **Refs** | T0102, T0004, [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D5 |

## Why

**T0102 fixed this for `.harness/` and only `.harness/`.** Installs are now keyed
by host (`.harness/zig/linux-x86_64/0.16.0/`), so the two bootstraps no longer
destroy each other. The two *other* shared, host-agnostic directories were not
touched:

- **`.zig-cache/`** — one path, no host discriminator
- **`build/<target>-<buildtype>/`** — one path per target, no host discriminator,
  and it contains absolute host paths

This is not hypothetical; it blocked T0100's local verification. On a Windows box
with WSL — which is how this project is actually developed — running the Linux
`zig build` against a tree last built from Windows fails:

```
error: failed to rename compilation results ('.zig-cache/tmp/17b026b44f6c6824')
       into local cache ('.zig-cache/o/213c45259d0009b8481e686cd149efc4'):
       AccessDenied
```

The directory is `drwxrwxrwx`, so this is not a permission bit. It is drvfs/9p
rename semantics against cache entries the Windows toolchain owns.

And even past that, the build tree itself is wrong for the host:

```
CMAKE_C_COMPILER:FILEPATH=C:/Development/hollow_point/build/linux-x86_64-release/toolchain/zig-cc.cmd
CMAKE_MAKE_PROGRAM:UNINITIALIZED=C:\Development\hollow_point/.harness/ninja/1.13.2/ninja.exe
```

Note `.harness/ninja/1.13.2/` — the *pre-T0102* layout. That tree predates the
host-keying fix, which is its own reason to expect trouble from it.

So the practical state is: **a dual-host developer cannot build from both hosts
without a reconfigure that destroys the other host's tree.** T0102's "Done when"
said "both hosts can build from the same working tree without re-bootstrapping
each time they switch" — that is true of the *toolchain* and false of everything
downstream of it.

## Done when

- [ ] Running `zig build` from one host does not invalidate or clobber the
      other host's cache or build tree
- [ ] A dual-host machine can alternate hosts without a full rebuild each time
- [ ] `BUILDING.md` says what happens, matching how T0102 documented its half
- [ ] Verified on a real dual-host tree, not reasoned about — this ticket exists
      because the reasoning in T0102 stopped one directory short

## Subtasks

- [ ] 122.1 Decide the layout. The obvious move is to mirror T0102: key both on
      the host, `build/<host>/<target>-<buildtype>/` and a host-keyed zig cache
      via `--cache-dir`/`ZIG_LOCAL_CACHE_DIR`. Weigh it against the disk cost —
      two full trees, and T0121 measured a tree at 327 MB
- [ ] 122.2 Check whether `dist/` has the same problem
- [ ] 122.3 Confirm CI is unaffected: a runner is single-host, so this should be
      invisible there. Confirm rather than assume — if the cache key ends up
      host-keyed, T0121's `buildtree-${{ runner.os }}-` key may need to agree
- [ ] 122.4 Decide what happens to an existing wrong-host tree: detect and
      re-configure, or detect and refuse with a message saying what to run

## Notes / findings

**Found while verifying T0100.** The frame-anatomy change could not be built or
tested locally on WSL because of this; verification was done through CI instead.
That is a workable fallback now that CI is fast (T0121 took it to ~5 minutes),
but it means a WSL-side developer has no local test loop at all, which is a worse
day-to-day cost than the ticket's priority suggests.

**The legacy `.harness/zig/0.16.0/` directory still exists** in at least one real
tree, holding the pre-T0102 Windows install. `bootstrap.sh` already prints a hint
about removing it (lines 100–102), so this is known — but a stale toolchain
sitting next to a host-keyed one is worth confirming nothing resolves to it.
