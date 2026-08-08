# Build harness internals

User-facing instructions live in `BUILDING.md`. This is how it works inside.

```
build.zig  ──►  cmake (pinned 3.31.12)  ──►  ninja (pinned 1.13.2)  ──►  zig cc
```

Each layer does one job: `build.zig` decides *whether* to configure, CMake
configures, Ninja decides *what* to rebuild, Zig compiles and links.

## Pinned toolchain

`bootstrap.sh` / `bootstrap.ps1` install into `.harness/<tool>/<host-key>/<version>/`:

| Tool | Version | Why pinned there |
|---|---|---|
| zig | 0.16.0 | the compiler, libc and Windows SDK |
| cmake | 3.31.12 | **must stay on 3.x** — see D5 in the decision log |
| ninja | 1.13.2 | incremental engine |
| slang | 2026.14.1 | the shader language (D28, T0142) — see below, it is keyed differently |

The host key is `<os>-<arch>` — `linux-x86_64`, `windows-x86_64`,
`linux-aarch64` — the same vocabulary as the target keys and the `build/` and
`dist/` directory names. It is there because one working tree can be used from
two hosts at once (Windows plus WSL), and until T0102 both scripts installed to
`.harness/<tool>/<version>/` and deleted the destination before extracting, so
either bootstrap destroyed the other's toolchain. `.harness/dl/` stays shared
and unkeyed: the archives are already named for their host, so they cannot
collide, and a dual-host machine downloads each one once.

**Slang is keyed by the *package's* platform, not the host's, and both packages
are installed on every host.** It is not a build tool in the way the other three
are: nothing runs `slangc` during `zig build`. The engine loads the platform's
slang *library* at run time to compile `.slang` shaders at pipeline-build time
(T0142), so the Linux suite needs `libslang-compiler.so` and the Windows suite —
native or under wine — needs `slang-compiler.dll`, and either host cross-builds
both targets. CMake resolves `.harness/slang/<platform>/<version>` at configure
time (headers, plus the runtime library it stages beside every executable) and
fails with a "run bootstrap" message if it is absent. The two bootstrap scripts
write identical content to identical paths, so the T0102 collision hazard does
not apply to it. The pin lives in three files — both bootstrap scripts and the
root `CMakeLists.txt` — and `pins_test.zig` fails if they drift.

The layout has one definition, `tools/harness/paths.zig`, which `build.zig` uses
and `tests/harness/paths_test.zig` pins down. The bootstrap scripts cannot
import it — a shell script, a PowerShell script and a Zig file have no way to
share a constant — so `tests/harness/pins_test.zig` reads both scripts and fails
if their versions or their layout drift from what `build.zig` expects.

`build.zig`'s `harnessTool()` prefers `.harness/`, falling back to `PATH` so the
harness still works with system tools. It warns when it falls back, and points
out an install left at the pre-T0102 path: a silent fallback means a
half-migrated `.harness/` builds with whatever the distribution ships while
looking like it used the pin, which is worse than an error because the build
still succeeds. The bootstrap scripts checksum-verify every download and delete
corrupt archives so a bad download is not reused.

On Linux, ninja ships as a zip; rather than require `unzip`, it is extracted with
the CMake installed a moment earlier (`cmake -E tar xf` handles zip).

## The compiler shims

CMake requires a compiler to be a *single executable*, but a Zig invocation is
`zig cc -target <triple> …`. So `cmake/toolchains/zig-common.cmake` generates
shims into `build/<target>/toolchain/` (`.sh` on Linux hosts, `.cmd` on Windows)
that bake in the target and forward the rest.

Sysroot and case-fixup flags are baked into the shim rather than
`CMAKE_<LANG>_FLAGS_INIT`, so path quoting stays in the shell where it is
reliable.

Generated per target: `zig-cc`, `zig-cxx`, `zig-ar`, `zig-ranlib`, and for
Windows `zig-rc`.

`HP_SHIM_DIR` **must be absolute** — CMake rejects a relative
`CMAKE_CXX_COMPILER`.

## Toolchain files

| File | Role |
|---|---|
| `zig-common.cmake` | shim generation, find() behaviour, all the cross-compile fix-ups |
| `x86_64-linux-gnu.cmake` | `CMAKE_SYSTEM_NAME=Linux`, glibc 2.28, points at the sysroot |
| `x86_64-windows-gnu.cmake` | `CMAKE_SYSTEM_NAME=Windows`, RC compiler wiring |

They are usable standalone, without `build.zig`:

```sh
cmake -B out -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/x86_64-linux-gnu.cmake \
      -DHP_ZIG=/path/to/zig -DHP_SHIM_DIR=$PWD/out/toolchain
```

`try_compile()` re-reads the toolchain file in a fresh CMake process, so
`HP_ZIG`, `HP_TARGET`, `HP_SYSROOT`, `HP_SHIM_DIR` and `HP_ZIG_LIB_DIR` are
forwarded via `CMAKE_TRY_COMPILE_PLATFORM_VARIABLES` — otherwise the probe builds
would silently target the host.

## find() behaviour

`LIBRARY` and `INCLUDE` are restricted to the sysroot (`…_MODE_… ONLY`) so a
Linux-host build cannot silently pick up `/usr/lib` and lose reproducibility.
`PROGRAM` stays on the host (`NEVER`) because SPIRV-Tools shells out to the host
python during generation, and `PACKAGE` is left at its `BOTH` default for the
same reason.

## Incremental strategy

`build.zig` re-runs CMake configure **only** when `needsConfigure()` says so:
build tree missing, or `CMakeCache.txt` records a different `HP_ZIG`,
`CMAKE_BUILD_TYPE`, `CMAKE_COMMAND` or `CMAKE_MAKE_PROGRAM`.

After that, Ninja re-runs CMake by itself whenever a `CMakeLists.txt` *or a
toolchain file* changes — both are tracked as build inputs (verified: editing
`zig-common.cmake` triggers `[0/1] Re-running CMake...`). An unconditional
configure would only add latency, never accuracy.

**Cache-key trap:** values passed as `-DFOO=...` without a declared type land in
`CMakeCache.txt` as `FOO:UNINITIALIZED=`, *not* `FOO:FILEPATH=`. `cacheHas()`
therefore matches on the name and ignores the type. Getting this wrong made
CMake re-run on every single invocation — silently defeating the entire
"don't rebuild what didn't change" requirement while appearing to work.

Each target × config gets its own tree (`build/<target>-<config>/`), so switching
between them never invalidates the other. `zig build all` runs targets serially —
`has_side_effects = true` takes a global lock, and each target already saturates
the machine.

## Running one gpu case, and running none of them on your desktop

Two flags that already paid for themselves, both measured 2026-08-08 on the
RTX 2080 box.

### `-Dtest-filter` — you almost never need all seventy cases

It has existed since the harness was written and went unused for months, which
is the only reason a "rerun everything for a one-line change" habit ever formed.
It is passed straight through to doctest as `--test-case=`, so the pattern is
doctest's glob, not a regex:

```sh
zig build test -Dtest=gpu -Dtest-filter="*tangent*"
```

| | wall clock | cases | windows opened |
|---|---|---|---|
| whole bucket | **100.7 s** | 70 | **114** |
| `-Dtest-filter="*tangent*"` | **2.3 s** | 2 | **2** |

Run the filtered case while iterating and the whole bucket once before you
commit. **43× is the difference between checking a change and putting it off.**

### `-Dtest-headless` — on by default, because nobody remembers a flag while being interrupted

**Every gpu case opens a real window and no gpu case ever presents to one.**
Nothing in `tests/gpu/` calls `Present`; they render to an offscreen target and
read it back, so the window exists purely because Diligent's swapchain wants a
native handle. The cost is borne entirely by whoever is using the machine: 114
windows across the desktop for a full run, plus a window-manager *"application is
not responding"* dialog whenever a case blocks for a few seconds in a cold shader
compile without pumping the event queue. **That dialog is cosmetic — the process
is working, not hung — but never click "Force quit"**: it kills the suite
mid-case, and because a gpu case tears its device down in the test body, the
result looks exactly like T0163's crash in the *next* case.

The runs now go to a throwaway X server. It engages only where every part is
true — a Linux host, the gpu bucket, a display that would otherwise be used, and
`xvfb-run` present — so CI, which is already headless, is untouched.

**The thing worth measuring was whether a virtual display costs the real device.
It does not.** The whole bucket under `xvfb-run`: 70/70 cases, 1700 assertions,
100.2 s, all 137 device lines reading `NVIDIA GeForce RTX 2080`, and **zero**
software-rasteriser fallbacks. Vulkan selects its physical device independently
of the X display; only the windows move.

The build says so, in the same line that names the runner (T0125's rule — a
thing the build did should be *stated*, not inferred):

```
+- test (linux-x86_64, gpu) natively, headless via xvfb-run success
+- test (windows-x86_64, gpu) under wine, headless via xvfb-run success
```

The wrapper is outermost, so the Windows target's `wine` inherits the throwaway
display rather than reaching for yours — measured, not assumed.

**`-Dtest-headless=false` when you want to watch a test render.** That is a real
thing to want: the rock cube's black top face was noticed by a person looking at
a window, and no assertion in the suite had anything to say about it.

## Measured incremental behaviour

| Change | Steps rebuilt (of ~1100) |
|---|---|
| nothing | 0 — `ninja: no work to do` |
| one `.cpp` (`Timer.cpp`) | 8 (compile + dependent archive/so relinks) |
| hot header (`DeviceContext.h`) | 246 |

## Vendored libraries that reach for global CMake state

**Check what a new `add_subdirectory` does to cache variables, not only what
targets it defines.** A dependency that sets a `CACHE ... FORCE` variable changes
*this whole build*, and the symptom rarely points at the cause.

The worked example is PhysicsFS (T0103). `third_party/physfs/CMakeLists.txt`
contains:

```cmake
if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(CMAKE_SKIP_RPATH ON CACHE BOOL "Skip RPATH" FORCE)
endif()
```

Not scoped to its own targets, not optional, and `FORCE` — so it lands in our
cache and strips RPATH from **every target in the build**: the engine, the
editor, the runtime, every test binary. Reasonable for a library that expects to
be installed into a system prefix; wrong for a subdirectory dependency.

What it looks like from the outside:

```text
./build/linux-x86_64-release/tests/hp_tests_integration:
  error while loading shared libraries: libhp_engine.so:
  cannot open shared object file: No such file or directory
```

The file is *sitting right beside the binary*. Nothing in the message suggests a
CMake option, everything links cleanly, and Diligent's backends fail identically
one step later once the first is worked around. It **survives a clean rebuild**,
because the cause is in the source rather than the tree — which is the expensive
part, since deleting the build tree is the natural first move and it changes
nothing.

`CMakeCache.txt` is what answers it in one line:

```sh
grep -iE "SKIP_RPATH|BUILD_RPATH" build/linux-x86_64-release/CMakeCache.txt
```

The fix is to reset the variable immediately after the `add_subdirectory`, both
the cache entry and the directory-scope variable — the `FORCE` wrote the cache,
so a plain `set()` alone is shadowed by it.

Other vendored trees here set global state more politely, but none of that is
guaranteed. When adding a dependency, grep it for `CACHE` and `FORCE` before
trusting it.

## Reading a build log

Two failure modes, both observed here, both of which make a correct build look
broken or a broken one look fine.

**The exit code must come from a redirect, not a pipe.** A shell pipeline
reports the last command's status:

```sh
zig build all | tail -60          # WRONG -- this is tail's exit code
zig build all > build.log 2>&1    # right
echo "EXIT: $?"
```

Reading a truncated `tail` of a 1500-target build also cuts the summary line the
check exists to read. This has hidden a failing test bucket twice.

**Grepping for `error` or `failed` produces false positives on a clean build.**
Measured on a full clean cold build of both targets (2026-08-05):

| pattern | hits on a **clean** build |
|---|---|
| `grep -icE 'error\|failed'` | **19** |
| `grep -cE '^FAILED:\|error:'` | **0** |

The 19 are of two kinds and neither indicates a problem:

- **CMake feature probes.** `-- Performing Test LIBC_HAS_ISINFF - Failed`,
  `HAVE_GAMEINPUT_H - Failed`, `ABSL_INTERNAL_AT_LEAST_CXX20 - Failed`. A probe
  failing *is* the mechanism working — it is how CMake discovers the platform.
- **Filenames.** `jerror.c`, `pngerror.c`, `tif_error.c`, `SDL_error.c`,
  `c4core/src/c4/error.cpp`. Each appears in its compile line, so a
  case-insensitive substring match hits it on every rebuild.

Use:

```sh
grep -nE '^FAILED:|error:' build.log
```

`^FAILED:` is ninja's marker for a failed edge; `error:` with the colon is what
clang emits. Verified against a deliberately broken translation unit: the build
exits 1 and the pattern returns one of each. **The colon and the anchor are load
bearing** — `grep -i error` without them matches every one of those filenames
again.

This is the general rule in `CLAUDE.md` — *when a check fails, suspect the
check* — in its second form. A filter can invent failures as easily as it can
hide them, and a confident "the build is broken" from a bad grep costs the same
hour as a missed one.

## dist

`cmake/dist.cmake` is run with `cmake -P`, not included — kept as a CMake script
so globbing and copying behave identically on both hosts and so `dist` works
without the harness. Produces `dist/<target>/{bin,lib}`; on Windows the DLLs go
next to the exe, since that is the only place Windows will find them.

---

## Named targets, `POST_BUILD`, and the stale library that ran the tests

Three facts that compose into one silent, expensive failure. Recorded on
2026-08-05 after it cost two wrong diagnoses in a single session.

### 1. The harness builds named targets, not `all`

`build.zig` invokes `cmake --build <dir> --target hp_tests_fast`, deliberately:
Ninja then resolves just that executable's dependencies, so running the fast
suite does not build ~1100 engine targets. The same is true of the apps.

**The consequence is that `add_custom_target(… ALL …)` never runs.** `ALL` puts
a target in the `all` target; nothing ever asks for `all`. A copy step written
that way configures cleanly, builds cleanly, and does nothing — which is exactly
what happened on the first attempt at fixing the bug below, and the only reason
it was caught was a test that compared file timestamps rather than trusting the
build output.

**The dependency has to run from the consumer to the step**, not the reverse:

```cmake
add_dependencies(hp_tests_fast my_copy_step)   # right: asking for the binary asks for the step
add_dependencies(my_copy_step hp_tests_fast)   # wrong: nothing ever asks for my_copy_step
```

### 2. `OUTPUT` accepts only a restricted set of generator expressions

`$<TARGET_FILE_NAME:hp_engine>` in an `add_custom_command(OUTPUT …)` fails at
generate time with **`No target "hp_engine"`**, which is a thoroughly misleading
message — the target exists. `COMMAND` has no such restriction. Use a **stamp
file** as the output and put the generator expression in the command:

```cmake
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/hp_engine_copy.stamp"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:hp_engine>" "${CMAKE_CURRENT_BINARY_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_CURRENT_BINARY_DIR}/hp_engine_copy.stamp"
    DEPENDS hp_engine
    VERBATIM)
```

### 3. `POST_BUILD` does not fire when the target is up to date

This is the one that did the damage. `add_custom_command(TARGET … POST_BUILD)`
runs only when the target is **rebuilt**.

On Windows an executable links against the **import library**, and recompiling
an engine `.cpp` without changing the exported symbol set leaves the import
library untouched. So the `.exe` is up to date, nothing relinks, the POST_BUILD
copy never runs, and the binary executes against the **previous**
`libhp_engine.dll` sitting beside it. Linux happened to escape only because a
`.so` is a direct link input, so the executable relinks — luck, not a guarantee,
and it is fixed the same way.

**The symptom is a test result that is silently about the wrong code.** It was
seen in both directions in one session:

- a **green** Windows suite while the identical source **segfaulted** on Linux;
- **two Windows failures** for a bug that had already been fixed, which sent the
  investigation after a phantom platform difference in `entt`'s container
  support that did not exist.

**If a result differs between the two targets and you cannot explain why, check
the timestamp of the library beside the binary before you debug the code:**

```sh
ls -l build/windows-x86_64-release/{engine,tests}/libhp_engine.dll
```

The fix in `tests/CMakeLists.txt` and both `apps/*/CMakeLists.txt` is a stamped
copy rule the binary depends on, so the engine beside it is refreshed on every
build whether or not anything relinks. The full transitive DLL set still rides
on `POST_BUILD`, which is correct for it: a *new* dependency changes the import
library, so the executable does relink.
