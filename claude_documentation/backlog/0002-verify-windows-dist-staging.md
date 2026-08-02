# T0002 — Verify `dist` staging for Windows

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Created** | 2026-08-02 |
| **Refs** | [../03-build-harness.md](../03-build-harness.md), `cmake/dist.cmake` |

## Why

`zig build dist -Dtarget=linux` is verified (4 shared + 82 static libraries
staged). The Windows branch of `cmake/dist.cmake` has **never been run**, and it
is not a mirror of the Linux one — it takes different globs (`*.dll`, `*.dll.a`,
`*.lib`), and it puts shared libraries in `bin/` rather than `lib/` because
Windows only resolves DLLs from the executable's own directory.

Untested branches in a staging script fail quietly: they produce a `dist/` that
looks populated but is missing exactly the file needed at startup.

## Done when

- [ ] `zig build dist -Dtarget=windows` exits 0
- [ ] `dist/windows-x86_64/bin/` holds `ImGuiProbe.exe` **and** the engine DLLs
      beside it
- [ ] `dist/windows-x86_64/lib/` holds the import libraries and static libraries
- [ ] Nothing from `CMakeFiles/` leaked in (the script filters it; confirm)
- [ ] The staged tree is what T0001 actually runs — do not verify it in the
      abstract

## Subtasks

- [ ] 2.1 Run the dist step and read its summary line
      (`dist: … N executable(s), N shared, N static/import`)
- [ ] 2.2 Confirm the DLL/exe co-location rule specifically
- [ ] 2.3 Check `*.dll.a` did not get double-copied by both the `*.a` and
      `*.dll.a` globs — harmless, but the counts will look wrong
- [ ] 2.4 Feed the result into T0001 rather than staging separately

## Notes / findings

- The executable-detection heuristic differs per platform: `*.exe` on Windows,
  versus an extension-exclusion filter under `apps/` on Linux. The Windows form
  is the more reliable of the two.
