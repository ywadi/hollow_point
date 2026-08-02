# T0002 — Verify `dist` staging for Windows

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Phase** | 1 — Harden the build |
| **Created** | 2026-08-02 |
| **Closed** | 2026-08-02 |
| **Refs** | [../../documentation/03-build-harness.md](../../documentation/03-build-harness.md), `cmake/dist.cmake` |

## Why

`zig build dist -Dtarget=linux` is verified (4 shared + 82 static libraries
staged). The Windows branch of `cmake/dist.cmake` has **never been run**, and it
is not a mirror of the Linux one — it takes different globs (`*.dll`, `*.dll.a`,
`*.lib`), and it puts shared libraries in `bin/` rather than `lib/` because
Windows only resolves DLLs from the executable's own directory.

Untested branches in a staging script fail quietly: they produce a `dist/` that
looks populated but is missing exactly the file needed at startup.

## Done when

- [x] `zig build dist -Dtarget=windows` exits 0
- [x] `dist/windows-x86_64/bin/` holds `ImGuiProbe.exe` **and** the engine DLLs
      beside it
- [x] `dist/windows-x86_64/lib/` holds the import libraries and static libraries
- [x] Nothing from `CMakeFiles/` leaked in (the script filters it; confirm)
- [x] The staged tree is what T0001 actually runs — do not verify it in the
      abstract

## Subtasks

- [x] 2.1 Run the dist step and read its summary line
      (`dist: … N executable(s), N shared, N static/import`)
- [x] 2.2 Confirm the DLL/exe co-location rule specifically
- [x] 2.3 Check `*.dll.a` did not get double-copied by both the `*.a` and
      `*.dll.a` globs — harmless, but the counts will look wrong
- [x] 2.4 Feed the result into T0001 rather than staging separately

## Notes / findings

- The executable-detection heuristic differs per platform: `*.exe` on Windows,
  versus an extension-exclusion filter under `apps/` on Linux. The Windows form
  is the more reliable of the two.

### Outcome — PASSED, after fixing two real bugs

```
-- dist: dist/windows-x86_64 -- 13 app file(s), 7 shared, 104 static/import
-- dist: dist/linux-x86_64   --  9 app file(s), 4 shared,  89 static/import
```

`dist/windows-x86_64/bin/` now holds `ImGuiProbe.exe`, its assets and the four
engine DLLs together, and it is the tree T0001 actually ran.

**Two defects found — the task was worth doing:**

1. **`CMakeFiles/` leaked into the Windows executable staging.** The shared and
   static loops filtered it; the `*.exe` glob did not. Compiler-detection
   artefacts (`CompilerIdC/a.exe`, `CompilerIdCXX/a.exe`) were being shipped.

2. **Windows staged no assets at all** — so `dist` was not runnable. The Linux
   side only appeared to work because its heuristic swept assets in by accident
   while miscounting them as executables.

Both came from the two platforms using different, ad-hoc detection. Replaced with
one deliberate rule: copy each `apps/<name>/` tree minus build bookkeeping, so
the executable and its assets stay together on both platforms. That also sidesteps
CMake having no portable "is this executable" test.
