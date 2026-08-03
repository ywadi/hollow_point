# T0102 — `bootstrap.sh` and `bootstrap.ps1` destroy each other's toolchain

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | Medium |
| **Complexity** | Simple |
| **Phase** | 1 — Harden the build |
| **Created** | 2026-08-03 |
| **Found by** | T0004 |
| **Refs** | [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D5 |

## Why

Both bootstrap scripts install into `.harness/<tool>/<version>/` with **no host
discriminator**, and both delete the destination before extracting. So on a
machine that uses both — a Windows box with WSL, which is how this project is
actually developed — running one bootstrap silently destroys the other's
toolchain.

`bootstrap.sh:94`:

```sh
rm -rf "$ZIG_DIR" "$ROOT/.harness/zig/zig-$ZIG_SLUG-$ZIG_VERSION"
```

`$ZIG_DIR` is `.harness/zig/0.16.0` — precisely where `bootstrap.ps1` put
`zig.exe`. The same applies to cmake (`bootstrap.sh:111`) and ninja
(`bootstrap.sh:129`), and to `Install-Archive` in `bootstrap.ps1`, which does
`Remove-Item -Recurse -Force` on the same paths from the other direction.

Neither script notices: each checks for *its own* binary name (`zig` vs
`zig.exe`), finds it missing, and reinstalls over the top.

This is not hypothetical. During T0012 a Linux zig was needed for comparison
against the Windows one, and running `bootstrap.sh` would have wiped the
Windows toolchain mid-task. It was avoided only by reading the script first;
the Linux zig went into a scratch directory instead.

## Done when

- [x] Running one bootstrap leaves the other host's toolchain intact
- [x] Both hosts can build from the same working tree without re-bootstrapping
      each time they switch — each script proven on its own real host; see the
      caveat under "What is still not verified"
- [x] `build.zig`'s `harnessTool()` finds the right toolchain for the host it
      is running on
- [x] `BUILDING.md` says what happens on a dual-host machine
- [x] The download cache is still shared where it is safe to (`\.harness/dl/`
      holds host-specific archives but they do not collide by name)

## Subtasks

- [x] 102.1 Decide the layout. Options: key by host
      (`.harness/zig/0.16.0-linux/`), key by a zig-style slug
      (`.harness/zig/x86_64-linux-0.16.0/`, which matches the upstream archive
      naming already used in both scripts), or keep the shared path and have
      each script refuse to delete a foreign install
- [x] 102.2 Update `bootstrap.sh` and `bootstrap.ps1` together — they are
      required to stay in sync, and this is exactly the kind of change where
      they drift
- [x] 102.3 Update `harnessTool()` in `build.zig`, which currently builds the
      path from `pinned_*_version` alone and appends `.exe` on Windows
- [x] 102.4 Update the paths quoted in `BUILDING.md` and in both scripts'
      "next:" hints
- [x] 102.5 Check the CI cache keys in `.github/workflows/ci.yml` still
      distinguish the two (`harness-linux-*` / `harness-windows-*` cache
      *entries* are already separate, but they populate the same path)
- [x] 102.6 Update the call sites the ticket did not list: `tools/mk_linux_sysroot.sh`
      and `tools/find_win_header_case.sh` both default `ZIG` to the old path,
      and `.github/workflows/full-build.yml` has a fourth PATH line
- [x] 102.7 Update `03-build-harness.md`, `01-project-overview.md` and
      `04-cross-compile-gotchas.md`, which all quote the old layout
- [x] 102.8 Guard the drift 102.2 warns about with a test rather than a comment

## Notes / findings

**Prefer keying by host over refusing to delete.** A refusal turns a silent
wipe into a confusing error on a machine that legitimately wants both. Keying
by host makes the dual-host case simply work, which is the case this project
actually has.

**`harnessTool()` falls back to PATH when the pinned tool is absent**
(`build.zig:179-184`), so a half-migrated layout does not fail loudly — it
quietly builds with whatever cmake the system has. That is worse than failing,
and is a reason to get 102.3 right in the same change rather than after.

**`.harness/dl/` is fine as-is.** The archives are named for their host
(`zig-x86_64-linux-0.16.0.tar.xz` vs `zig-x86_64-windows-0.16.0.zip`), so the
download cache does not collide and can stay shared. Only the *extracted*
directories collide.

**Related, not the same bug:** the two scripts must also keep their pinned
versions and checksums in sync with each other and with `build.zig`. Nothing
enforces that today. Worth considering in 102.2 whether a single pins file both
scripts read would be better than three copies, though shell/PowerShell/Zig all
reading one format is its own small problem.

---

## What was done

**Layout: `.harness/<tool>/<host-key>/<version>/`**, host first. The key is
`<os>-<arch>` — `linux-x86_64`, `windows-x86_64`, `linux-aarch64` — reusing the
vocabulary `build.zig` already has for target keys and for `build/` and `dist/`
directory names, rather than inventing a second spelling of "which machine is
this". Host-first rather than version-first so one host's installs can be swept
with a single `rm -rf .harness/zig/linux-x86_64`.

**The layout has one definition**, `tools/harness/paths.zig`, which `build.zig`
imports. It was split out for the same reason `cache.zig` was: so a test can
import it. `.harness/dl/` stays shared and unkeyed, as the ticket predicted.

**Legacy installs are reported, never deleted.** Both bootstrap scripts and
`build.zig` notice a pre-T0102 directory at the old shared path and print the
`rm -rf` to run. Nothing removes it automatically, and that is deliberate: on
the dual-host machine this ticket is about, the directory at the old path may
be the *other* host's only toolchain, so deleting it is precisely the
destruction being fixed. A refusal-to-delete design was rejected for the reason
already in the notes above; this is the reporting half of it, without the
failure.

**The PATH fallback is now loud.** The note above called the silent fallback
"worse than failing", and it was right — during this work a half-migrated
`.harness/` quietly configured the whole build with a system CMake
(`Clang 21.1.0`) and nothing said so. `harnessTool()` still falls back, because
the pin is documented as a default rather than a requirement, but it prints
what it could not find and what it is using instead, and adds a second line
when the tool is sitting at the old path. Failing outright was considered and
not taken: it would break the documented "usable with system tools" property to
fix a diagnosability problem.

## Notes / findings, second pass

**The ticket under-counted the call sites.** It listed `bootstrap.sh`,
`bootstrap.ps1`, `build.zig`, `BUILDING.md` and `ci.yml`. Also hardcoding the
old path: `tools/mk_linux_sysroot.sh:28`, `tools/find_win_header_case.sh:16`
(both default `ZIG=` to `.harness/zig/0.16.0/zig`), a fourth PATH line in
`.github/workflows/full-build.yml:62`, and three documentation files quoting
the layout. Eleven files in total, not five.

**Three copies of the pins now have a test, not a comment.**
`tests/harness/pins_test.zig` reads both bootstrap scripts and fails if their
`ZIG_VERSION` / `$ZigVersion` and friends disagree with `build.zig`'s
`pinned_*_version`, or if either script stops installing under a host key. The
single-pins-file idea in the note above is still the better fix and is still
not done; this is the cheap guard that makes its absence survivable. The
checksums are still unguarded — they are per-host by nature and there is
nothing to compare them against.

**`$HostKey` in PowerShell is safe next to the automatic `$Host` variable** —
PowerShell tokenises variable names greedily, so `$HostKey` is its own name and
`"...\$HostKey\..."` interpolates it whole. Worth stating because it looks like
a collision and is not one.

**A pre-existing gap, unrelated to this ticket:** the `doctest` submodule added
by T0012 is not fetched by a plain `git pull`, so the C++ suites fail to
compile with `fatal error: doctest/doctest.h: No such file or directory` until
`git submodule update --init third_party/doctest` is run. CI passes because
`actions/checkout` uses `submodules: recursive`. `BUILDING.md` does not mention
it. Not fixed here — it belongs to whoever owns the clone instructions.

## Evidence

**The fix, stated as the thing that used to break.** A synthetic Windows
toolchain was planted where `bootstrap.ps1` would put one, the Linux tree was
deleted to force a full re-extract, and `./bootstrap.sh` was run:

```
$ mkdir -p .harness/zig/windows-x86_64/0.16.0
$ echo "PRETEND-WINDOWS-ZIG" > .harness/zig/windows-x86_64/0.16.0/zig.exe
$ rm -rf .harness/zig/linux-x86_64 .harness/cmake/linux-x86_64 .harness/ninja/linux-x86_64
$ ./bootstrap.sh >/dev/null 2>&1
$ cat .harness/zig/windows-x86_64/0.16.0/zig.exe
PRETEND-WINDOWS-ZIG
$ cat .harness/cmake/windows-x86_64/3.31.12/bin/cmake.exe
PRETEND-WINDOWS-CMAKE
$ cat .harness/ninja/windows-x86_64/1.13.2/ninja.exe
PRETEND-WINDOWS-NINJA
```

Intact. Under the old layout that same sequence deleted all three.

**Bootstrap installs where it says it does, and reports the leftovers:**

```
$ ./bootstrap.sh
toolchain ready in .harness/<tool>/linux-x86_64/
  zig    0.16.0
  cmake  3.31.12
  ninja  1.13.2
note: an install predating T0102 remains at .harness/zig/0.16.0/
      Installs are now keyed by host (linux-x86_64), so nothing uses it. Remove it
      once you are sure no other host still needs it:
        rm -rf .harness/zig/0.16.0
[... same for cmake and ninja ...]

next:  /media/ywadi/second/hollow_point/.harness/zig/linux-x86_64/0.16.0/zig build all
```

**The loud fallback, observed before the new layout was installed:**

```
warning: cmake 3.31.12 is not installed at .harness/cmake/linux-x86_64/3.31.12/ -- falling back to 'cmake' on PATH.
  The pinned toolchain exists so the build does not vary with the host; run ./bootstrap.sh (or bootstrap.ps1) to install it.
note: an install predating T0102 remains at .harness/cmake/3.31.12/. The layout is now keyed by host
  so that a Windows and a WSL bootstrap no longer overwrite each other. Once the
  new one is in place, and if no other host still needs it, remove it.
```

**The path tests fail against the old behaviour.** Written first, then run
against a `paths.zig` that ignored the host exactly as the old layout did:

```
$ zig build test -Dtest=fast
error: 'paths_test.test.a tool directory is keyed by host first, then version' failed:
       expected:
       .harness/zig/linux-x86_64/0.16.0
                    ^ ('\x6c')
       found:
       .harness/zig/0.16.0
                    ^ ('\x30')
run test harness-paths 2 pass, 3 fail, 1 crash (6 total)
```

**The pins guard fails against drift**, checked by mutation rather than by
watching it pass — bumping `CMAKE_VERSION` in `bootstrap.sh` alone:

```
error: 'pins_test.test.bootstrap.sh pins the versions build.zig expects' failed:
       ====== expected this output: =========
       3.31.12␃
       ======== instead found this: =========
       3.31.13␃
```

and reverting `ZIG_DIR` to the old unkeyed expression, which is T0102 itself
coming back:

```
error: 'pins_test.test.bootstrap.sh installs under a host key' failed:
error: 'pins_test.test.neither script installs at the old unkeyed path' failed:
```

**Full suite, clean trees, both targets** (`build/` and `dist/` deleted first,
so both configures ran from scratch):

```
$ zig build test -Dtest=all
Build Summary: 16/16 steps succeeded; 21/21 tests passed
+- test (linux-x86_64, fast) success
+- test (windows-x86_64, fast) success        <- under wine
+- run test harness-cache 7 pass (7 total)
+- run test harness-paths 6 pass (6 total)
+- run test harness-pins  5 pass (5 total)
+- run test harness-dist  3 pass (3 total)
[doctest] assertions: 10029 | 10029 passed | 0 failed   (x2, both targets)
```

No fallback warning in that run, which is itself the evidence that
`harnessTool()` found the pinned toolchain at its new path.

`sh -n bootstrap.sh` is clean.

## `bootstrap.ps1` on a real Windows host — CI run 30833256715

The one thing that could not be checked locally: no Windows host and no `pwsh`
here, so the PowerShell side was a careful mirror of `bootstrap.sh` and nothing
more, not even syntax-checked. CI's `tests-windows-host` job runs it end to end
under `pwsh 7` on `windows-latest`. All three jobs green:

```
$ gh run view 30833256715
✓ main CI · 30833256715
JOBS
✓ Configure with FetchContent disconnected in 3m19s
✓ Tests (Linux host, both targets)     in 7m3s
✓ Tests (Windows host, native)         in 8m21s
```

The Windows bootstrap, installing where it is supposed to:

```
Run .\bootstrap.ps1
shell: C:\Program Files\PowerShell\7\pwsh.EXE -command ". '{0}'"
==> downloading zig-x86_64-windows-0.16.0.zip
==> installing zig 0.16.0
==> downloading cmake-3.31.12-windows-x86_64.zip
==> installing cmake 3.31.12
==> downloading ninja-win.zip
==> installing ninja 1.13.2

toolchain ready in .harness\<tool>\windows-x86_64\
  zig    0.16.0
  cmake  3.31.12
  ninja  1.13.2

next:  D:\a\hollow_point\hollow_point\.harness\zig\windows-x86_64\0.16.0\zig.exe build all
```

`$HostKey` interpolates correctly inside every `Join-Path` string — the
`windows-x86_64` component appears in the summary line and in the `next:` hint,
and the subsequent steps found the toolchain there. The suite then built and
ran natively:

```
[28/29] Linking CXX executable tests\hp_tests_fast.exe
[doctest] test cases:     3 |     3 passed | 0 failed | 0 skipped
[doctest] assertions: 10029 | 10029 passed | 0 failed |
[doctest] Status: SUCCESS!
```

(`zig build` prints no summary block on success without `--summary all`, which
CI does not pass, so the Zig harness suites — `harness-cache`, `harness-paths`,
`harness-pins`, `harness-dist` — are covered by the step's exit status rather
than by a line of their own.)

**The cache-key reasoning is now observed, not just argued.** Editing both
bootstrap scripts changed both `hashFiles()` keys, so each host missed its
cache exactly once and repopulated only its own subtree:

```
key: harness-windows-00d190f518d13de384784b2b93e614b472391e46e4144e4e6b9a8bd2c475ad88
Cache not found for input keys: harness-windows-00d190f5...
Cache saved with key: harness-windows-00d190f5...
```

## What is still not verified

**No full engine build against the new layout.** Only the test targets were
built. Nothing in this change touches compilation, and `full-build.yml`'s PATH
line was updated with the others, but `zig build all` has not been run since —
that workflow is dispatch-only and was not triggered here.

**The dual-host case is proven by parts, not end to end.** A Linux bootstrap
demonstrably leaves a Windows install intact (above), and each script
demonstrably installs into its own subtree on its own real host. What has not
happened is one physical machine running both bootstraps into one working tree
and building from each in turn — that needs the Windows+WSL box this ticket was
written about.

**Checksums are still unguarded.** `pins_test.zig` compares versions and
layout across the three files; the SHA256 pins are per-host by nature and there
is nothing to compare them against.
