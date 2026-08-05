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

**Corrected 2026-08-05 — the first diagnosis here was wrong.** It originally read
"the debug info arrives from Diligent's static archives, which carry it
regardless of build type". Half right, and the half that was wrong is the half
that decides the fix.

**The cause is the toolchain, not Diligent and not our CMake.** `zig c++` emits
full DWARF **by default**; clang proper emits none. Measured with the project's
own shim, `build/linux-x86_64-release/toolchain/zig-cxx`, on one TU:

| compile | object | `.debug_*` |
|---|---|---|
| `-O3 -DNDEBUG` (exactly how everything here is built) | 44,264 B | 25,957 B |
| `-O3 -DNDEBUG -g0` | 1,176 B | 0 |

A census of **every** TU in both `compile_commands.json` — 1416 Linux, 1410
Windows — finds **not one passing `-g`**. Diligent's CMake adds none either:
`set_common_target_properties`
(`third_party/DiligentEngine/DiligentCore/BuildTools/CMake/BuildUtils.cmake:197`)
sets only `CXX_STANDARD`, MSVC LTCG flags and `CXX_VISIBILITY_PRESET hidden`.

**Our own code is equally affected**, which the original note denied: the 26
engine objects total 34,341,808 B, of which **18,733,440 B (54.5%) is `.debug_*`**.
Diligent dominates by *volume* — 804 Diligent TUs against 33 of ours — not by
being uniquely at fault. So "suppress `-g` for third-party only" would leave
half our own object size in place.

### Windows does not escape it — it externalizes it (and `dist` misses that)

The second wrong claim was that Windows avoids the problem. It does not. The
Windows objects carry `.debug$S` 9,360,008 B and `.debug$T` 6,910,628 B. The
linked DLL has **zero** debug sections and a 41-byte `RSDS` CodeView entry
pointing at **`libhp_engine.pdb`, which is 137,748,480 B (131.4 MiB)** beside a
16.5 MiB DLL.

PE moves debug info into a sidecar; ELF has no such mechanism, so ld.lld copies
DWARF into the `.so`. **128.4's `.pdb` question and this one are the same
question in two platform costumes and must be decided together.**

Note the accident: Windows `dist` stages only the two *app* PDBs, because the
engine's 131 MiB PDB is outside `apps/` and no glob matches it. Windows looks
tidy by luck, not by decision — which is exactly this ticket's thesis.

### What `dist` actually stages, measured

| | Linux | Windows |
|---|---|---|
| **total staged** | **1.4 GB** | 471 MiB |
| shared objects / DLLs | 768.9 MiB (7) | 43.9 MiB (6) |
| static / import libs | 574.2 MiB (93) | — |

**92.2% of it is debug info plus symtab** (909.7 of 987.1 MiB). And
`libhp_engine.so` is staged **twice at 203 MiB** — once in `bin/`, once in
`lib/` — which 128.2's declared staging fixes independently of any stripping.

### The options, measured by relinking the real engine

| approach | size | `.symtab` | link time | works? |
|---|---|---|---|---|
| status quo | 203.0 MiB | 1,225,488 | 1.07 s | — |
| `-Wl,--strip-debug` | **16.71 MiB** | **0** | **0.28 s** | yes |
| `-Wl,--strip-all` | 16.71 MiB — md5-identical | 0 | 0.28 s | yes |
| `--compress-debug-sections=zlib` | 74.5 MiB | kept | 1.42 s | yes |
| `-g0` everywhere | ~21.6 MiB *(estimate)* | kept | ~16% faster | yes |
| `zig objcopy` split | — | — | — | **no — unimplemented** |
| lld `--separate-debug-file` | — | — | — | **no such flag** |
| `-gsplit-dwarf` | — | — | — | **unusable** |

**`--only-keep-debug` + `.gnu_debuglink` is not available in this toolchain.**
`zig objcopy` returns `error: unimplemented` for every ELF option —
`--strip-debug`, `--only-keep-debug`, `--add-gnu-debuglink`, `--extract-to`
(`zig-0.16.0/lib/compiler/objcopy.zig`, the `.elf` branch calls
`fatal("unimplemented")`; still unimplemented on zig master).
[ziglang/zig#24522](https://github.com/ziglang/zig/issues/24522) is open.
**Trap worth recording: it exits 1 but truncates the output to 0 bytes first**,
so a naive `if(EXISTS)` staging check would ship an empty `.so`.

LLD has no `--separate-debug-file` (LLD 21.1.0; absent from `lld/ELF/Options.td`
at `llvmorg-21.1.0`). Split debug on ELF always needs an objcopy-class pass, and
requiring host binutils **breaks D3** — a Windows host cross-compiling to Linux
has no `objcopy`, so `dist` would differ by who built it.

`-gsplit-dwarf` is accepted and does move debug info — into
`~/.cache/zig/tmp/<hash>-t.dwo`, outside the build tree, subject to cache GC,
with no `dwp` shipped. Dangerous precisely because the build succeeds and objects
shrink ([ziglang/zig#11858](https://github.com/ziglang/zig/issues/11858)).

**Two constraints on the choice:**

- **Under LLD, `--strip-debug` also removes `.symtab`** — byte-identical to
  `--strip-all`. With `CXX_VISIBILITY_PRESET hidden`, `.dynsym` is only
  133,080 B, so a link-stripped engine gives near-useless backtraces. `-g0`
  keeps `.symtab` and function-name-level stacks; `--strip-debug` does not.
- **Stripping is safe for T0104.** The build-id stamp is an exported
  `extern "C"` function resolved by name, so it lives in `.dynsym`, which every
  variant preserves. All variants `dlopen` successfully.

### Recommendation for 128.6

1. **Record `--only-keep-debug` + `.gnu_debuglink` as rejected against a measured
   constraint**, not deferred. It is unimplementable here and requiring host
   binutils violates D3 — a decision-log-shaped fact, not a TODO.
2. **Strip shipped artifacts at link with `-Wl,--strip-debug`**, behind a CMake
   option defaulting **OFF** so developer builds stay debuggable. Only hermetic
   ELF strip available, and it makes the link 4× faster.
3. **Keep the unstripped build as the symbol archive.** Breakpad's `dump_syms`
   and `sentry-cli` both accept an unstripped binary directly;
   `--only-keep-debug` is a size optimization on the archive, not a functional
   requirement. Nothing is lost but archive bytes.
4. **Add `-Wl,--build-id=sha1` now.** Currently **absent** — no
   `.note.gnu.build-id` in any binary. It costs ~180 bytes and is the key symbol
   stores index on; retrofitting after a release means those builds are
   unsymbolicatable forever.
5. **Fix the double-staging regardless** — worth ~200 MiB on its own (128.2).
6. **Decide `.pdb` (128.4) and this together.** Consistent pair: ship stripped
   `.so` and strip-equivalent DLL; retain the unstripped `.so` and
   `libhp_engine.pdb` as the symbol archive; ship neither.

Considered and **not** recommended as primary: `-g0` on third-party targets buys
~16% build time and ~1.2 GB of build tree while keeping `.symtab` — attractive,
but it destroys the symbol archive, so it should only follow a decision that we
never symbolicate third-party frames. `--compress-debug-sections=zlib` is a good
option for the *build tree*, not for shipping.

### Not verified — do not promote these to fact without measuring

- **The `-g0` figure (21.6 MiB) is an estimate**, derived as stripped size plus
  `.symtab`/`.strtab`. A real `-g0` tree is a 16–18 min cold build, not run.
- **The editor was not run against a stripped engine.** `dlopen` of the stripped
  `libhp_engine.so` succeeds with all dependencies resolved — which exercises
  every relocation — but that is not "it renders a frame".
- **No debugger symbolication test.** No hermetic debugger exists in `.harness/`.
  `SHF_COMPRESSED` was confirmed set on the compressed variant; no frame was
  watched resolving.
- **`dump_syms` / `sentry-cli` behaviour is sourced, not measured here.** Neither
  is vendored; the claim rests on Chromium and Sentry docs.
- **Windows figures come from the Linux-host cross-compile only.** A Windows-host
  build was not checked for the same PDB layout.
- Interaction between `--strip-debug` and the `dist` RUNPATH verification
  (T0105.4) is untested, though stripping does not touch `.dynamic`.

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
