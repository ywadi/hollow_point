# T0128 — `dist` stages by glob, so nobody decides what ships

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
| **Complexity** | Moderate |
| **Phase** | 8 — Runtime & export |
| **Order** | 740 |
| **Created** | 2026-08-04 |
| **Found by** | T0105.4 |
| **Refs** | [../completed/0105-module-linkage-loose-ends.md](../completed/0105-module-linkage-loose-ends.md), T0109, T0043, T0013 |

## Why

`cmake/dist.cmake` stages a build tree by walking it with recursive globs and
copying everything that matches `*.so` / `*.dll` / `*.a` / `*.lib`, minus a
short exclusion list. **Nothing decides what ships; the glob decides, and it
decides by file extension.**

That worked while the tree was small. T0105.4 was the first time anyone read the
staged output rather than checking that it ran, and found `dist` shipping every
fixture the test suite builds — including `hp_unload_module_broken`, an artifact
that exists *because* it segfaults the process at exit, staged into `bin/` on
Windows next to `hp_editor.exe`. That specific leak is fixed. The mechanism that
produced it is not.

Three things it still cannot get right, all for the same reason:

- **Orphans from deleted source directories.** `build/windows-x86_64-release/`
  still holds `game/libhp_game.dll`, from a `game/` directory that no longer
  exists — it became `samples/sandbox/`. Ninja does not delete orphans, and a
  glob cannot tell one from live output. So a stale build tree ships a library
  built from source that is gone, silently.
- **`.pdb` files ship** in `bin/` on Windows. That may be deliberate (crash
  dumps) or accidental (nobody chose); today it is the second.
- **90–109 static and import libraries ship** in `lib/` on both targets, which
  is most of Diligent. Correct for an SDK an external game links against
  (T0109), wrong for a player-facing build, and `dist` does not distinguish the
  two cases.

The underlying question is the one nobody has answered: **what is `dist` for?**
A runnable game, an SDK for T0109's external projects, or both under separate
layouts. Until that is decided, the glob keeps making the decision by accident.

## Done when

- [ ] `dist` is stated to produce a specific thing (or two named things), and
      the layout follows from that rather than from what the globs happened to
      match
- [ ] Staging is driven by what the build *declares* — CMake `install()` rules,
      an explicit target list, or an equivalent — so an artifact ships because
      something said it should
- [ ] An orphan in the build tree cannot reach `dist`, and this is tested
- [ ] The `.pdb` and static-library questions are each decided and recorded,
      including "yes, deliberately"
- [ ] `tests/harness/dist_test.zig` covers the chosen rule, not just the
      exclusions

## Subtasks

- [ ] 128.1 Decide what `dist` produces. Coordinate with T0109, which needs an
      SDK-shaped output, and T0043, which owns export
- [ ] 128.2 Replace glob-based staging with declared staging
- [ ] 128.3 Orphan test: a stale artifact in the build tree must not stage
- [ ] 128.4 Decide `.pdb` and static/import library inclusion per output kind
- [ ] 128.5 Extend `tests/harness/dist_test.zig`
- [ ] 128.6 Decide whether ELF debug info is stripped or split on staging — see
      "92% of the Linux engine is debug info" below. `.pdb` is named in 128.4;
      the Linux equivalent was not named anywhere

## Notes / findings

### 92% of the Linux engine is debug info, and `dist` ships all of it (2026-08-05, from T0028)

Measured while linking DiligentFX for T0028, and recorded here rather than fixed
there, because staging policy is this ticket's:

| | |
|---|---|
| `libhp_engine.so` unstripped | **203.0 MiB** |
| the same file stripped | **16.7 MiB** |
| debug info | **186.3 MiB — 92% of the file** |
| `hp_engine.dll` (Windows) | 16.5 MiB |

**It is not our compiler flags.** `CMAKE_BUILD_TYPE=Release`,
`CMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG`, and the engine's own translation units
compile with `-O3` and no `-g` — checked in `compile_commands.json`, not assumed.
The debug info arrives from **Diligent's static archives**, which carry it
regardless of build type: `libDiligent-GraphicsEngineVk-static.a` is 41 MB,
`libDiligent-AssetLoader.a` 16 MB, `libDiligentFX.a` 25 MB, `libDiligent-Imgui.a`
8.6 MB, and every one of them has `.debug_*` sections.

The stripped Linux size matching the Windows DLL almost exactly — 16.7 against
16.5 MiB — is the evidence that the *code* is the expected size and only the
symbols are not.

**`cmake/dist.cmake` performs no stripping**, so a staged build carries all
186 MB. 128.4 already decides `.pdb` inclusion for Windows; **the ELF side was
not named anywhere in this ticket**, which is why it is added as 128.6 rather
than assumed to be covered.

Worth deciding rather than reflexively stripping: separate `.debug` files with
`objcopy --only-keep-debug` keep a shipped artifact small *and* keep crash
symbolication possible, which matters more here than raw size.

**Not a regression, and specifically not DiligentFX's doing.** Measured by
relinking with the `DiligentFX` line removed: byte-identical at 212,847,352.
A static archive contributes only the objects something references, and no engine
code calls into FX yet — so it will grow when T0028's renderer lands, and this
baseline is what that growth should be measured against.

**The exclusion added by T0105.4 is a patch, not the fix.** `_never_stage`
excludes `/CMakeFiles/` and `/tests/`. It is correct and tested, and it is still
a blacklist on a mechanism that should be a whitelist — the next directory that
builds something not meant to ship will leak the same way, and the failure is
silent.

**Do not simply delete the recursive glob.** It is doing necessary work:
Diligent's `GraphicsEngineVk` / `GraphicsEngineOpenGL` are loaded dynamically by
Diligent's own factory loader, so they are *not* imports of the executable and
`$<TARGET_RUNTIME_DLLS>` cannot see them. Measured on `hp_editor.exe`, whose
only non-system import is `libhp_engine.dll`. Any replacement has to name those
explicitly.

**The RUNPATH half is already correct and should not be disturbed** —
`$ORIGIN:$ORIGIN/../lib`, with `BUILD_WITH_INSTALL_RPATH ON`, verified by
running an export with the build tree deleted (T0105.4). Whatever replaces the
staging must keep executables in `bin/` and Linux shared objects in `lib/`, or
that RUNPATH stops matching the layout.
