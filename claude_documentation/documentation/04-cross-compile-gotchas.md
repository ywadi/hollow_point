# Cross-compile gotchas

Every defect hit making this build. Each is fixed **without patching
`third_party/DiligentEngine`** — that constraint is what keeps engine updates
clean, and it shaped every fix below.

Recurring theme: several of these were *silent*. They produced a working-looking
build while being wrong, and only surfaced later. Where that happened it is
called out, because it is the reason to distrust "it compiled" as evidence.

---

## G1 — `zig env` reports `lib_dir` relative to the cwd

`zig env` shortens the path when the lib dir sits under the current directory:

```
$ cd /repo && zig env | grep lib_dir
    .lib_dir = ".harness/zig/linux-x86_64/0.16.0/lib",      # relative!
$ cd /tmp && zig env | grep lib_dir
    .lib_dir = "/repo/.harness/zig/linux-x86_64/0.16.0/lib" # absolute
```

**Silent failure mode:** the relative path works at configure time (CMake runs
from the source dir) and breaks under Ninja, which runs every command from the
*build* directory.

**Fix:** `zig-common.cmake` runs `zig env` with `WORKING_DIRECTORY` pinned to
zig's own directory and resolves the result against it. Absolute results pass
through unchanged.

---

## G2 — MinGW ships Windows SDK headers lowercase

Diligent spells them as Microsoft documents them — `<Windows.h>`, `<SDKDDKVer.h>`,
`<D3D12.h>`, `<GL/GL.h>` — while MinGW-w64 ships `windows.h`, `gl.h`. Invisible
on Windows, fatal from a case-sensitive filesystem.

**Fix:** generated forwarding headers in `build/<target>/toolchain/wininc/`,
placed **last** on the include path so a real header of the same name always wins.

Two things to get right:

- **Only the file name differs in case, never the directory.** `<GL/GL.h>`
  resolves to `GL/gl.h`, not `gl/gl.h`. A naive full-lowercase scan misses it —
  which is how `GL/GL.h` was missed on the first pass and cost a build cycle.
- **Only generate on a case-sensitive filesystem.** Where `Windows.h` and
  `windows.h` are the same file, the forwarder would `#include` *itself*. This is
  probed by writing a file and testing for it under a different case, not
  inferred from the host OS — macOS is case-insensitive by default but not
  always.

Rescan with `tools/find_win_header_case.sh` after updating Diligent or Zig.

---

## G3 — MinGW import libraries are lowercase too

Diligent links `Shlwapi`; MinGW names it `shlwapi`. Zig *synthesises* import
libraries from `.def` files rather than shipping `.a` files, so there is nothing
to symlink.

**Fix:** generate the capitalised import library with `zig dlltool` into
`build/<target>/toolchain/winlib/`.

**Do not turn this into a sweep.** That directory is first on the library search
path. `-lSPIRV` is also capitalised but is a *project* library (glslang) —
auto-generating `libSPIRV.a` there would shadow the real one and silently produce
a broken link. The list is explicit for that reason.

---

## G4 — MSVC-style `.lib` names are opened relative to the cwd

`Diligent-Imgui` does `target_link_libraries(... dwmapi.lib)` under MinGW. CMake
passes such an item through **verbatim**, and the linker opens a bare filename
relative to the working directory — `-L` is *not* consulted:

```
$ zig cc … -L/path/to/winlib dwmapi.lib
error: dwmapi.lib: file not found
```

**Fix:** generate MSVC-named copies into the build root, which is where Ninja runs
every command from.

**Latent, not yet observed:** this only reaches a link line when something links
`Diligent-Imgui`, i.e. the first real app. It was fixed pre-emptively.

---

## G5 — `--push-state` and the WHOLE_ARCHIVE feature

Appeared only after upgrading CMake 3.22 → 3.31.

Diligent links its static engines into the shared libraries with
`$<LINK_LIBRARY:WHOLE_ARCHIVE,...>`. CMake implements that on GNU-like linkers as
`--push-state,--whole-archive … --pop-state`, and Zig rejects it:

```
error: unsupported linker arg: --push-state
```

CMake *does* have the correct fallback and chooses by **probing the linker** —
but it probes `CMAKE_LINKER`, which resolved to the host `/usr/bin/ld` (which
supports the option), while linking actually goes through `zig cc` (which does
not). The probe answered for the wrong binary.

**Fix:** declare `CMAKE_{,C_,CXX_}LINKER_PUSHPOP_STATE_SUPPORTED FALSE`.
`Platform/Linker/GNU.cmake` only probes `if(NOT DEFINED …)`, so this both skips
the bogus probe and selects CMake's own fallback form.

**The trap that cost two cycles:** the answer is cached in
`build/<target>/CMakeFiles/<ver>/CMakeCXXCompiler.cmake`, written at
compiler-detection time and reloaded on every reconfigure. Changing it requires a
**fresh build tree**, not a reconfigure. Two fixes appeared to do nothing before
this was understood.

---

## G6 — Sysroot stubs must never be found at runtime

After switching the sysroot to generated stubs (see D4), `find_package(X11)`
returned absolute paths into the sysroot, and CMake adds the directory of every
such library to the build `RPATH`:

```
RUNPATH  /…/third_party/sysroot/linux-x86_64/lib:/…/GraphicsEngineOpenGL:…
```

The sysroot holds stubs with **empty function bodies**. A binary finding them
first at runtime calls into nothing.

**Fix:** `list(APPEND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${HP_SYSROOT}/lib")`.
CMake omits implicit link directories from RPATH, exactly as it does `/usr/lib`.
The loader then resolves `libX11.so.6` from the user's system by SONAME.

Verified afterwards: RUNPATH contains only the two engine directories.

---

## G7 — ImGui removed the obsolete modifier-key aliases

ImGui renamed `ImGuiKey_ModCtrl` → `ImGuiMod_Ctrl` in **1.89** and kept aliases.
Those were still live in the **1.92.1** DiligentEngine bundles but are **removed
by 1.92.9**, and Diligent's Linux/Emscripten ImGui impls still use the old names:

```
ImGuiImplLinuxXCB.cpp:102: error: use of undeclared identifier 'ImGuiKey_ModCtrl'
```

**Fix:** plain enum renames, so the root `CMakeLists.txt` maps them back with
compile definitions on `Diligent-Imgui` only. The Win32 impl is unaffected — it
comes from ImGui's own up-to-date backend, which is why the Windows build never
showed this. Remove once DiligentEngine updates those three files.

---

## G8 — `add_sample_app()` requires a `readme.md`

`DiligentSamples/CMakeLists.txt:113` does
`target_sources(${APP_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/readme.md")`
unconditionally. Every app directory needs one or configure hard-fails.

---

## G9 — `.gitignore` does not support trailing comments

```gitignore
/.harness/          # bootstrapped toolchain     <-- matches NOTHING
```

The whole line is the pattern. This staged 229 MB of toolchain for commit. Keep
comments on their own lines.

---

## Things that were *not* problems

Worth recording so they are not re-investigated:

- **D3D on Windows disabling itself.** Expected — see D2. Not a misconfiguration.
- **Host-tool execution during the build.** Checked: every
  `COMMAND $<TARGET_FILE:...>` is in Tests/Tutorials, which are not built. No
  cross-compile blocker.
- **`zig rc` driving Diligent's `.rc`.** Works, and is genuinely exercised —
  `Win32AppResource.rc.res` is produced. Needs `/fo` and a `--` separator before
  the source, since absolute Linux paths start with `/` and would otherwise parse
  as options.
