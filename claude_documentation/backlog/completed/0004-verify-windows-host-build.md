# T0004 — Verify building *on* a Windows host

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 1 — Harden the build |
| **Created** | 2026-08-02 |
| **Refs** | [../../documentation/02-decision-log.md](../../documentation/02-decision-log.md) D3 |

## Why

Half of the agreed 2×2 matrix (D3) is unproven. Linux→Linux and Linux→Windows
both work. **No code path specific to a Windows host has ever executed**, and
there are several:

- `bootstrap.ps1` in its entirety
- `.cmd` compiler shims instead of `.sh` (different quoting, `%*` vs `"$@"`,
  errorlevel propagation)
- the case-sensitivity probe taking its **false** branch, which suppresses the
  header forwarders and import-library generation (G2/G3) — logic that has only
  ever run in its true branch
- CMake invoking a `.cmd` as `CMAKE_CXX_COMPILER`, which is known to be fussier
  than a shell script

This is the largest block of write-once-never-run code in the harness.

## Done when

- [x] `.\bootstrap.ps1` installs zig, cmake and ninja on a clean Windows machine
- [x] `zig build windows` completes natively
- [x] `zig build linux` **cross-compiles from Windows** — the untested diagonal
      of the matrix, and the one that needs the vendored sysroot to pay off
- [x] Any fixes folded back, and `05-verification-status.md` updated

## Subtasks

- [x] 4.1 Run `bootstrap.ps1`; check the archive-extract/rename logic, which
      differs from the Linux path
- [x] 4.2 Inspect a generated `.cmd` shim before trusting it
- [x] 4.3 Native Windows build
- [x] 4.4 Cross-build the Linux target from Windows
- [x] 4.5 Confirm the case probe correctly skipped `wininc/`

## Notes / findings

- ~~Needs a real Windows machine or VM; cannot be done from here.~~ **Wrong —
  corrected 2026-08-03.** See below: WSL interop runs genuine Win32 processes.
- Expect problems in the `.cmd` shims first — that is the least conventional part.

### 2026-08-03 — driving a Windows host from WSL

The note above assumed a Linux-only machine. The working copy at
`/mnt/c/Development/hollow_point` **is** `C:\Development\hollow_point` on a real
Windows 10.0.26200 host, and WSL interop (`/proc/sys/fs/binfmt_misc/WSLInterop`
= enabled) launches `.exe` files as **genuine Win32 processes**, not emulated
ones — cwd under `/mnt/c` is translated to the drive path automatically.

So `powershell.exe -File bootstrap.ps1` and the bootstrapped `zig.exe` /
`cmake.exe` / `ninja.exe` all execute as native Windows programs against NTFS.
That covers the four code paths this ticket exists for: the `.ps1`, the `.cmd`
shims, `CMAKE_HOST_WIN32` branches, and the case probe's false branch.

**What it still does not prove:** a pristine Windows machine. This checkout was
made by WSL git, and that difference is not cosmetic — see the symlink finding
below. Treat results as "Windows host, WSL-made working tree".

#### 4.1 `bootstrap.ps1` — clean pass, no fixes needed ✅

`.harness/` did not exist beforehand, so this was a true cold bootstrap.

```
PS> powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\Development\hollow_point\bootstrap.ps1
==> downloading zig-x86_64-windows-0.16.0.zip
==> installing zig 0.16.0
==> downloading cmake-3.31.12-windows-x86_64.zip
==> installing cmake 3.31.12
==> downloading ninja-win.zip
==> installing ninja 1.13.2

toolchain ready in .harness\
  zig    0.16.0
  cmake  3.31.12
  ninja  1.13.2
```

Windows PowerShell **5.1**.26100.8875, not pwsh 7 — worth recording, because the
script uses nothing that needs 7. The `Install-Archive` extract-then-rename logic
(the part that differs from the Linux path) worked first time for both zig and
cmake, and ninja's unpack-straight-into-place special case was also correct. All
three checksums matched. Verified as real Windows binaries:

```
$ ./.harness/zig/0.16.0/zig.exe version      -> 0.16.0
$ ./.harness/cmake/3.31.12/bin/cmake.exe --version | head -1
                                             -> cmake version 3.31.12
$ ./.harness/ninja/1.13.2/ninja.exe --version -> 1.13.2
$ file .harness/zig/0.16.0/zig.exe
  PE32+ executable (console) x86-64, for MS Windows, 7 sections
```

#### Submodules were never initialised in this working copy

First `zig build configure -Dtarget=windows` failed with `add_subdirectory`
errors for `third_party/{enkiTS,meshoptimizer,ozz-animation,...}` — all nine
submodules showed `-` in `git submodule status`. **Not a Windows finding**: a
Linux build in this tree would fail identically. Resolved with
`git submodule update --init --recursive --depth 1` (shallow, WSL git).

#### The vendored sysroot's symlinks are unreadable to Windows tools ⚠️

This one is a genuine portability defect, and it lands squarely on the
`zig build linux`-from-Windows diagonal that this ticket wants proven.

`third_party/sysroot/linux-x86_64/lib/` holds **8 symlinks** (`libGL.so`,
`libX11.so`, `libxcb.so`, `libXau.so`, `libXdmcp.so`, `libGLdispatch.so`,
`libGLX.so`, `libOpenGL.so`), committed as git mode `120000`. Checked out by WSL
git they become LX-style reparse points, which Win32 cannot open at all:

```
PS> [System.IO.File]::ReadAllBytes('...\sysroot\linux-x86_64\lib\libGL.so')
read FAILED: The file cannot be accessed by the system.
PS> Get-Item ...\libGL.so | Format-List Length,LinkType,Target
Length : 0     LinkType :      Target : {}
```

Checking out with Git for Windows instead does not fix it — it just fails
differently: with `core.symlinks` unset (the default for this scoop install,
confirmed) git writes a *regular text file* whose contents are the string
`libGL.so.1`, which the linker will reject as not an ELF object.

So **no Windows-host checkout can currently link the Linux target**, regardless
of which git is used. The fix belongs in `tools/mk_linux_sysroot.sh`: emit real
copies (they are tiny stubs) or generate the `.so` stubs at configure time,
rather than committing symlinks. Until then, the D3 matrix's fourth quadrant is
blocked on the sysroot's representation, not on the toolchain.

The **native Windows target is unaffected** — `x86_64-windows-gnu.cmake` never
sets `HP_SYSROOT`, so nothing in that build path touches these files.

#### 4.5 The case probe takes its false branch correctly ✅

The branch that had never executed. `_hp_case_sensitive` came out FALSE and the
whole forwarding apparatus was correctly skipped — after configure,
`build/windows-x86_64-release/toolchain/` contains **only the five shims**:

```
zig-ar.cmd  zig-cc.cmd  zig-cxx.cmd  zig-ranlib.cmd  zig-rc.cmd
```

No `wininc/`, no `winlib/`, and no MSVC-named `*.lib` copies in the build root —
all three live inside the `if(_hp_case_sensitive)` block. `zig-rc.cmd` correctly
omits the `-I.../wininc` include that `x86_64-windows-gnu.cmake` adds only when
the probe is true.

Checked separately that this is **not** a WSL artifact: a directory created from
*inside* WSL on `/mnt/c` is still case-insensitive to both sides (the drvfs
mount is `case=off`), so the probe sees what a native Windows host would.

```
$ mkdir .hp_casetest_wsl && touch .hp_casetest_wsl/hpcaseprobe
PS> Test-Path '...\.hp_casetest_wsl\HPCASEPROBE'   ->  True
```

#### 4.2 The `.cmd` shims work — with one latent defect ⚠️

**They function correctly.** Driven from a `.bat` (which is how Ninja invokes
them, and which avoids WSL's argv translation — see the trap below):

```
OK_ERRORLEVEL=0        # --version
FAIL_ERRORLEVEL=1      # compiling a nonexistent source -> propagates 1
SPACED_ERRORLEVEL=0    # -c "C:\...\hp probe.cpp" -o "C:\...\hp probe.o"
                       # -> produced hp probe.o, 10870 bytes
```

So `%*` preserves quoted paths containing spaces, and `cmd` propagates the
child's exit code — the two things subtask 4.2 was worried about. The ticket's
"expect problems in the `.cmd` shims first" prediction was wrong; they were the
smoothest part.

**The defect: every generated line ends `\r\r\n`, not `\r\n`.**

```
$ cat -A build/windows-x86_64-release/toolchain/zig-cxx.cmd
@echo off^M^M$
"C:\...\zig.exe" c++ -target x86_64-windows-gnu %*^M^M$
```

`zig-common.cmake:87` writes `"@echo off\r\n...%*\r\n"`, and on a Windows host
CMake's `file(WRITE)` opens the stream in text mode and translates the `\n`
again, giving `\r\r\n`. On a Linux host the same line produces a correct
`\r\n` — which is why this has never been seen. `cmd` tolerates the extra CR
(it strips trailing carriage returns), so nothing is broken today, but the
generator is writing something it does not intend. The fix is to write `\n`
alone and let the host translate, or write with `file(WRITE ... )` in a mode
that does not translate. Left unfixed for now; it changes no behaviour.

#### Trap: do not test Windows argv through WSL's `cmd.exe /c '...'`

A first attempt at the spaced-path test appeared to show the shim shattering
quoted arguments:

```
zig: error: no such file or directory: 'probe.cpp"'
```

That is **WSL's argv translation mangling the quotes on the way into
`CreateProcess`, not the shim.** Re-run through a `.bat` file with no quotes
crossing the interop boundary, the same shim handles the same path correctly.
Anyone verifying Windows quoting from WSL will hit this; put the command in a
script file rather than in `cmd.exe /c '...'`. Nearly recorded a shim bug that
does not exist.

#### Configure: two warnings, both benign, one worth understanding

`CMake Warning: Manually-specified variables were not used by the project:
CMAKE_TOOLCHAIN_FILE` — **the toolchain was in fact applied.** Verified in
`CMakeCache.txt`:

```
CMAKE_C_COMPILER:STRING=C:/.../toolchain/zig-cc.cmd
CMAKE_CXX_COMPILER:FILEPATH=C:/.../toolchain/zig-cxx.cmd
CMAKE_RC_COMPILER:FILEPATH=C:/.../toolchain/zig-rc.cmd
```

The warning is an artifact of the *first* configure attempt having failed (on
the missing submodules) after already writing a cache: CMake reads
`CMAKE_TOOLCHAIN_FILE` only on the initial configure of a tree, so re-passing it
on the retry counts as unused. Cosmetic, but alarming enough to be worth the
check — a build using the host compiler instead of zig would look much like this.

Configure also ran `pip install jinja2` against the host's Python 3.13 (a
DiligentCore codegen dependency) and printed `Fetching entt repository...` —
which reads alarmingly like the offline guarantee (T0010) failing on Windows.
It did not. `_deps/` holds **only build directories, no `*-src`**, so nothing
was downloaded; the message is printed before FetchContent short-circuits to
the redirect:

```
$ ls build/windows-x86_64-release/_deps
abseil-cpp-build  entt-build          # no *-src

$ grep FETCHCONTENT_SOURCE_DIR CMakeCache.txt
FETCHCONTENT_SOURCE_DIR_ABSEIL-CPP:PATH=C:/Development/hollow_point/third_party/abseil-cpp
FETCHCONTENT_SOURCE_DIR_ENTT:PATH=C:/Development/hollow_point/third_party/entt
FETCHCONTENT_SOURCE_DIR_RAPIDJSON:PATH=C:/Development/hollow_point/third_party/rapidjson
```

**T0010's offline wiring holds on a Windows host.** Note this run had
`FETCHCONTENT_FULLY_DISCONNECTED:BOOL=OFF` — `build.zig` does not pass it, so
the Linux-side proof under `unshare -r -n` has no Windows equivalent yet. The
redirects are confirmed; a genuinely network-isolated Windows configure is not.

The `pip install jinja2` is a real host dependency worth noting: a Windows
machine without Python on `PATH` would fail configure here, and nothing in
`BUILDING.md` says so.

#### 4.3 Native Windows build — clean, 1089/1089 ✅

```
$ ./.harness/zig/0.16.0/zig.exe build windows
...
[1088/1089] Building RC object .../Win32AppResource.rc.res
[1089/1089] Linking CXX static library ...\libDiligent-SampleBase.a
exit code 0
```

Artifacts, all confirmed `PE32+ ... for MS Windows`:

| | |
|---|---|
| DLLs | `GraphicsEngineVk_64r.dll`, `GraphicsEngineOpenGL_64r.dll`, `Archiver_64r.dll`, `SuperResolution_64r.dll` |
| Executables | `Diligent-RenderStatePackager.exe`, `HLSL2GLSLConverter.exe` |
| Static libs | 92 |

**A produced binary was run natively** — the first time anything from this
project has executed on Windows without wine:

```
$ .../Diligent-RenderStatePackager.exe --help
Diligent Engine: Info:   Diligent-RenderStatePackager.exe {OPTIONS}
    Render state packager
  OPTIONS: ...
exit=0
```

`zig rc` compiled `Win32AppResource.rc` to a 289,880-byte `.res` with only a
`'__forceinline' macro redefined` warning from MinGW's `_mingw.h`. That path is
identical on both hosts, but it had never been exercised with the case-probe
false branch (no `wininc` on the rc include path) — it works.

**Target count differs from the Linux-hosted cross-build: 1089 here vs the
1105/1105 recorded in `05-verification-status.md`.** Not investigated. Most
likely the case-sensitive branch's extra generated inputs, but it should be
explained before the two are treated as equivalent builds.

#### 4.4 Linux-from-Windows fails at *configure*, not link — root cause proven

Predicted a link-time failure; it is earlier and cleaner than that:

```
CMake Error at .../FindPackageHandleStandardArgs.cmake:233 (message):
  Could NOT find X11 (missing: X11_X11_LIB)
Call Stack:
  .../FindX11.cmake:676 (find_package_handle_standard_args)
  third_party/DiligentEngine/DiligentCore/Graphics/GraphicsEngineOpenGL/CMakeLists.txt:211 (find_package)
```

`find_library` looks for `libX11.so`, and to a Windows process that name is a
**0-byte file** — the WSL symlink. The real stub is next to it under the
versioned name, which `find_library` does not look for. Both views side by side:

```
WSL:      libX11.so -> libX11.so.6        libX11.so.6   248272
Windows:  libX11.so           0           libX11.so.6   248272
```

Confirms the earlier prediction, and locates the fix precisely: it is
`find_library` failing on an unreadable name, not the linker rejecting a
malformed input.

**The fix, tested:** replace the 8 symlinks with real copies of their targets.
The copy keeps the versioned `SONAME`, so nothing about linking or runtime
resolution changes —

```
$ readelf -d libX11.so | grep -i soname
 0x000000000000000e (SONAME)  Library soname: [libX11.so.6]
```

— which matters, because the RPATH reasoning in `zig-common.cmake` (and gotcha
G6) depends on the loader resolving the *system* libX11 by SONAME at runtime,
not the stub. A copy preserves that; a renamed-to-unversioned stub would not.

Cost is ~1.4 MB of duplication in the repo. A tidier variant is to have
`tools/mk_linux_sysroot.sh` emit each stub **once**, named `libX11.so` but with
`SONAME=libX11.so.6` — one file, no symlink, no duplication — but that is a
larger change to the generator and is not what was tested here.

#### 4.4, second blocker: zig cannot build a PCH for an ELF target on Windows

With the symlink fix in place, configure passed and the build got to 249/1096
before dying on a **precompiled header**:

```
FAILED: [code=1] .../glslang/CMakeFiles/glslang.dir/cmake_pch.hxx.pch
ld.lld: error: C:\...\.zig-cache\o\3dfa5f99...\cmake_pch.hxx.o: unknown file type
```

Clang emits the PCH into zig's cache under an `.o` name, and zig then hands that
file to `ld.lld` instead of treating it as the final artifact. Deterministic —
re-running the exact command single-threaded reproduces the identical cache hash.

**This is a zig defect, not a project one.** Bypassing the `.cmd` shim and
invoking `zig.exe` directly with the same baked flags fails identically, so the
shim is not involved.

The deciding variables are the **host** and the **target's object format** —
measured, not inferred. Same zig 0.16.0 binary in every row:

| host | target | `-MD` | result |
|---|---|---|---|
| Linux | linux (ELF) | no | fail |
| Linux | linux (ELF) | yes | **pass** |
| Windows | linux (ELF) | no | fail |
| Windows | linux (ELF) | yes | **fail** |
| either | windows (COFF) | — | pass |

**`-MD` is the whole reason this was never seen.** CMake always passes it, so a
Linux host silently takes a code path that works; a Windows host does not. The
Windows target is unaffected because it has no PCH rules at all — glslang's PCH
is off for that configuration (`grep -c cmake_pch build.ninja`: **0** for the
windows target, **99** for the linux target).

Getting to this needed a Linux zig for comparison, which could not come from
`bootstrap.sh` — see the clobbering finding below — so 0.16.0 was unpacked into
a scratch directory instead. A Linux-host configure of the same tree produces
the same **99** PCH rules and builds them fine, which is what isolates the
failure to the host rather than to the project's configuration.

**Fix applied** in `cmake/toolchains/zig-common.cmake`: set
`CMAKE_DISABLE_PRECOMPILE_HEADERS` when `CMAKE_HOST_WIN32` and the target is not
Windows. Scoped as tightly as the evidence allows — a Linux host keeps its
precompiled headers and its build times; only the affected pair pays. It should
be revisited when the zig pin moves, since the underlying defect is upstream.

#### `bootstrap.sh` and `bootstrap.ps1` clobber each other ⚠️

Both install into `.harness/<tool>/<version>/` with **no host discriminator**,
and both delete the destination first — `bootstrap.sh:94` does
`rm -rf "$ZIG_DIR"` on exactly the directory where `bootstrap.ps1` put
`zig.exe` (same for cmake at :111 and ninja at :129).

On a machine that uses both — which is precisely the WSL arrangement this
verification ran on — running one bootstrap silently destroys the other's
toolchain. Not hit here only because the risk was spotted before running
`bootstrap.sh`; a Linux zig was unpacked into a scratch directory instead.

Worth fixing if dual-host machines are to be supported: either key the install
directory by host (`.harness/zig/0.16.0-windows/`) or have each script detect
and refuse to remove a foreign install. Note this also means the two scripts
cannot share a download cache safely, though `dl/` itself is host-neutral.

### Closing evidence (2026-08-03)

Both targets re-verified **after** the two fixes were in place, so this is the
state of the tree as committed, not an earlier run:

```
$ ./.harness/zig/0.16.0/zig.exe build windows
WINDOWS_EXIT=0        FAILED/stopped lines: 0

$ ./.harness/zig/0.16.0/zig.exe build linux
LINUX_EXIT=0          FAILED/stopped lines: 0

$ ./.harness/zig/0.16.0/zig.exe build dist
DIST_EXIT=0
  dist/linux-x86_64    4 shared, 89 static
  dist/windows-x86_64  4 shared, 92 static
```

Full builds, from scratch, earlier in the same session: **windows 1089/1089**,
**linux 1095/1095**, both exit 0. Binaries from both were executed — the
Windows one natively, the Linux one under WSL — and the Linux executable
carries `GLIBC_2.27` max with no RPATH, matching the Linux-host build.

**Fixes landed:**

| File | Change |
|---|---|
| `cmake/toolchains/zig-common.cmake` | `CMAKE_DISABLE_PRECOMPILE_HEADERS` when host is Windows and target is not |
| `tools/mk_linux_sysroot.sh` | stub `.so` emitted as a copy, not a symlink |
| `third_party/sysroot/linux-x86_64/lib/*.so` | 8 files, symlink → regular file |
| `documentation/05-verification-status.md` | both new quadrants recorded, Windows-host caveats rewritten |

**What this ticket did *not* establish** — carried into the status doc rather
than quietly dropped:

- The working tree was checked out by **WSL git**, not a Windows clone, and the
  build was driven through WSL interop rather than a Windows shell. A clone made
  with Git for Windows has never been built. The symlink fix removes the one
  known difference between the two, but that is reasoning, not evidence.
- Configure needs **Python on `PATH`** (`pip install jinja2`); `BUILDING.md`
  does not say so, and a Windows machine without it fails at configure.
- **`FETCHCONTENT_FULLY_DISCONNECTED` was OFF.** The vendored-source redirects
  are confirmed, but the network-isolated proof that exists for Linux
  (`unshare -r -n`) has no Windows equivalent.
- The **1089 vs 1105** target-count difference against the Linux-hosted Windows
  cross-build is unexplained.

**Follow-ups worth their own tickets:** the two bootstrap scripts clobbering each
other on a dual-host machine, and the `\r\r\n` shim line endings. Neither blocks
anything today; both are recorded above with the reproduction.
