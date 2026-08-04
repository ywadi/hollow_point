# T0125 — WSL-interop detection silently loses to wine

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Trivial |
| **Phase** | 1 — Harden the build |
| **Order** | 4 |
| **Created** | 2026-08-04 |
| **Found by** | T0122 |
| **Refs** | T0004, T0001, T0122, [../../../BUILDING.md](../../../BUILDING.md) |

## Why

`build.zig` picks how to run a cross-built Windows test binary on a Linux host,
and documents a deliberate preference order (`runnerFor`, and BUILDING.md's
"How a cross-built suite is executed"):

1. **WSL interop** — the `.exe` runs as a genuine Windows process, which is
   higher fidelity than emulation and needs nothing installed
2. **wine** — the fallback on a real Linux box
3. built but **not run**, with a warning

On this WSL machine, step 1 is being skipped even though interop is enabled, and
the suite runs under wine. So the higher-fidelity path that T0004 established is
silently not the one being exercised, and the build says nothing about it.

### Measured

```
$ cat /proc/sys/fs/binfmt_misc/WSLInterop
enabled
interpreter /init
flags: PF

$ ls /proc/sys/fs/binfmt_misc/
WSLInterop  register  status          # wine is NOT registered here

$ command -v wine
/usr/bin/wine
```

Interop is enabled and wine has no binfmt registration, yet `zig build test
-Dtest=fast` prints wine's own startup complaint before the Windows suite runs:

```
it looks like wine32 is missing, you should install it.
multiarch needs to be enabled first.  as root, please
execute "dpkg --add-architecture i386 && apt-get update &&
apt-get install wine32:i386"
```

That message can only come from wine, so `wslInteropEnabled()` returned false
and `runnerFor` fell through to the wine branch.

### The likely cause — measured, but the mechanism is not yet proven

`wslInteropEnabled()` reads the proc file with a size limit:

```zig
const data = std.Io.Dir.cwd().readFileAlloc(
    b.graph.io,
    "/proc/sys/fs/binfmt_misc/WSLInterop",
    b.allocator,
    .limited(256),
) catch return false;
return std.mem.indexOf(u8, data, "enabled") != null;
```

`/proc` files report a **stat size of zero** while holding real content:

```
$ stat -c '%n size=%s' /proc/sys/fs/binfmt_misc/WSLInterop
/proc/sys/fs/binfmt_misc/WSLInterop size=0
$ wc -c < /proc/sys/fs/binfmt_misc/WSLInterop
56
```

If `readFileAlloc` sizes its buffer from `stat` — which is the usual
implementation — it returns an empty slice, `indexOf` finds no `"enabled"`, and
the function reports interop as unavailable. Note the `catch return false` also
maps *any* read error to "not available", so a genuine failure is
indistinguishable from a genuine "disabled".

**Not verified:** that `readFileAlloc` specifically under-reads here. A probe
was attempted and abandoned — Zig 0.16 moved this API to `std.Io.Dir` and needs
an `Io` instance, which is more scaffolding than the guess was worth mid-ticket.
Confirm it before fixing, because the fix differs: a stat-sized read wants a
loop-until-EOF, whereas a different cause wants something else.

## Done when

- [ ] The mechanism is confirmed by measurement, not inference — a probe that
      shows what that read actually returns for this file
- [ ] `wslInteropEnabled()` reads the file correctly regardless of the
      zero-stat-size behaviour
- [ ] A read *error* is distinguishable from a genuine "interop disabled",
      rather than both silently becoming wine
- [ ] `zig build test -Dtest=fast` on a WSL host runs the Windows suite through
      interop, with no wine startup message, and the evidence pasted here
- [ ] The build says which runner it chose, so the next person does not have to
      infer it from a wine warning appearing by accident

## Subtasks

- [ ] 125.1 Probe what `readFileAlloc` returns for a zero-stat-size proc file
- [ ] 125.2 Fix the read
- [ ] 125.3 Separate "read failed" from "disabled"
- [ ] 125.4 Announce the selected runner in the build output
- [ ] 125.5 Re-verify on this WSL host, paste the output

## Notes / findings

**This is a fidelity regression, not a correctness one.** Both suites pass
either way — 24/24 cases, 213091 assertions, under wine (T0122's evidence). wine
running the Windows suite is a supported path that T0001 proved. What is lost is
the *better* path silently, which is the part worth fixing: the failure mode of
a silent fallback is that it keeps working until the day it does not.

**CI is not affected in the way it might look.** The Linux job runs on a real
Linux runner where wine is the correct choice and interop does not exist, so CI
was always going to take the wine branch. This only misfires on a WSL
developer's machine — which is now, after D18, the primary way this project is
developed, so it is worth more than it was.

**Found while closing T0122**, from a wine warning that had no business
appearing in that output. It is unrelated to T0122's cause and was deliberately
kept out of it.
