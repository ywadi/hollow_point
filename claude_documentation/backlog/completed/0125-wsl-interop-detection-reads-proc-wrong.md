# T0125 — WSL-interop detection silently loses to wine

| | |
|---|---|
| **Status** | ✅ DONE |
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

- [x] The mechanism is confirmed by measurement, not inference — a probe that
      shows what that read actually returns for this file
- [x] `wslInteropEnabled()` reads the file correctly regardless of the
      zero-stat-size behaviour
- [x] A read *error* is distinguishable from a genuine "interop disabled",
      rather than both silently becoming wine
- [x] `zig build test -Dtest=fast` on a WSL host runs the Windows suite through
      interop, with no wine startup message, and the evidence pasted here
- [x] The build says which runner it chose, so the next person does not have to
      infer it from a wine warning appearing by accident

## Subtasks

- [x] 125.1 Probe what `readFileAlloc` returns for a zero-stat-size proc file
- [x] 125.2 Fix the read
- [x] 125.3 Separate "read failed" from "disabled"
- [x] 125.4 Announce the selected runner in the build output
- [x] 125.5 Re-verify on this WSL host, paste the output

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

## Evidence

### 125.1 — the mechanism, measured before fixing

The ticket refused to accept the stat-size story as a cause until it was
observed, which was right: the fix differs depending on the answer. Probed by
temporarily instrumenting `wslInteropEnabled()` itself, so the measurement
happened in the exact context the real call runs in rather than in a standalone
program approximating it:

```
T0125-PROBE: read 0 bytes, enabled=false
```

**Zero bytes from a file that holds 56.** The read *succeeds* — no error to
catch — and returns an empty slice, so `indexOf(data, "enabled")` is null and
the function reports interop unavailable with total confidence. That is the
whole bug.

### 125.2 / 125.3 — the fix

Two changes, and the second is the one that matters longer term:

- **Read streaming, not stat-sized.** `readFileAlloc` sizes its buffer from
  `stat`, and procfs reports `st_size == 0` for a file with real content.
  `openFile` + `readerStreaming` + `allocRemaining` reads until EOF instead.
- **Three-valued, not boolean.** `Interop { enabled, disabled, unknown }`.
  `error.FileNotFound` means "not WSL" and is silent, because a real Linux box
  legitimately has no such file and warning about it every build would be noise.
  Any *other* read failure is `unknown` and says so. Previously `catch return
  false` made a broken read indistinguishable from a genuine "disabled", which
  is precisely how this survived: the failure had no symptom of its own, only a
  wine warning that looked like it belonged to someone else.

### 125.4 / 125.5 — verified on this WSL host

```
Build Summary: 12/12 steps succeeded; 18/18 tests passed
test success
+- test (linux-x86_64, fast) natively success 33ms
|  +- build tests (linux-x86_64, fast) success 4s
+- test (windows-x86_64, fast) as a real Windows process via WSL interop success 441ms
|  +- build tests (windows-x86_64, fast) success 5s
+- run test harness-cache 7 pass (7 total) 18ms
+- run test harness-paths 6 pass (6 total) 20ms
+- run test harness-pins 5 pass (5 total) 21ms
```

The Windows suite runs as a genuine Windows process, **the wine startup warning
is gone from the output entirely**, and the step name now states the runner —
so nobody has to infer it from whether wine happens to complain. 24/24 cases and
213091 assertions in each suite, unchanged.

An unlooked-for datum: interop runs the suite in **441ms** against wine's
**2s**. Fidelity was the argument for it; it is also simply faster here.

### The CI path, which this must not break

CI's Linux job runs on a real Ubuntu runner with no `WSLInterop` file at all, so
the `FileNotFound` → `disabled` → wine path is the one four green jobs depend
on. Verified by pointing the constant at an absent path and rebuilding:

```
Build Summary: 12/12 steps succeeded; 18/18 tests passed
+- test (linux-x86_64, fast) natively success 32ms
+- test (windows-x86_64, fast) under wine success 2s
```

Falls back silently — no `unknown` warning, which is the correct behaviour for a
machine that simply is not WSL. Reverted afterwards and the real path confirmed
back in place.

## What is not verified

**The `unknown` branch has never been observed firing.** It requires a
`WSLInterop` file that exists but cannot be read, which did not occur naturally
and was not synthesised. The branch is reasoned, not measured — the honest
statement is that it is a guard against a class of failure this ticket proved
can hide, not a path with evidence behind it.

**CI has not yet run against this change.** Pushed together with T0123; the
result belongs here before it is trusted.

## Notes / findings

**The absent symptom is the lesson.** A stat-sized read of a procfs file is a
well-known trap, but what let it live here was not the trap — it was
`catch return false` collapsing "could not read" into "not available". The
fallback then worked correctly and quietly, and the only trace was a wine
warning that read as normal for a project that legitimately uses wine on CI. A
detection function whose failure mode is indistinguishable from a valid answer
has no failure mode, only a wrong answer.

**Found by accident, which is the uncomfortable part.** Nothing was looking for
this. It surfaced because a wine message appeared in T0122's output where it had
no business being, and only because that output was read in full rather than
grepped for pass/fail — which is what CLAUDE.md says to do and the reason it
says it.
