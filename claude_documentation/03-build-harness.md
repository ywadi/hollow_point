# Build harness internals

User-facing instructions live in `BUILDING.md`. This is how it works inside.

```
build.zig  ──►  cmake (pinned 3.31.12)  ──►  ninja (pinned 1.13.2)  ──►  zig cc
```

Each layer does one job: `build.zig` decides *whether* to configure, CMake
configures, Ninja decides *what* to rebuild, Zig compiles and links.

## Pinned toolchain

`bootstrap.sh` / `bootstrap.ps1` install into `.harness/`:

| Tool | Version | Why pinned there |
|---|---|---|
| zig | 0.16.0 | the compiler, libc and Windows SDK |
| cmake | 3.31.12 | **must stay on 3.x** — see D5 in the decision log |
| ninja | 1.13.2 | incremental engine |

`build.zig`'s `harnessTool()` prefers `.harness/`, falling back to `PATH` so the
harness still works with system tools. The bootstrap scripts checksum-verify
every download and delete corrupt archives so a bad download is not reused.

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

## Measured incremental behaviour

| Change | Steps rebuilt (of ~1100) |
|---|---|
| nothing | 0 — `ninja: no work to do` |
| one `.cpp` (`Timer.cpp`) | 8 (compile + dependent archive/so relinks) |
| hot header (`DeviceContext.h`) | 246 |

## dist

`cmake/dist.cmake` is run with `cmake -P`, not included — kept as a CMake script
so globbing and copying behave identically on both hosts and so `dist` works
without the harness. Produces `dist/<target>/{bin,lib}`; on Windows the DLLs go
next to the exe, since that is the only place Windows will find them.
