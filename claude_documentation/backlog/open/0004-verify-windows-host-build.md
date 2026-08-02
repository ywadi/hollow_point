# T0004 — Verify building *on* a Windows host

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Medium |
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

- [ ] `.\bootstrap.ps1` installs zig, cmake and ninja on a clean Windows machine
- [ ] `zig build windows` completes natively
- [ ] `zig build linux` **cross-compiles from Windows** — the untested diagonal
      of the matrix, and the one that needs the vendored sysroot to pay off
- [ ] Any fixes folded back, and `05-verification-status.md` updated

## Subtasks

- [ ] 4.1 Run `bootstrap.ps1`; check the archive-extract/rename logic, which
      differs from the Linux path
- [ ] 4.2 Inspect a generated `.cmd` shim before trusting it
- [ ] 4.3 Native Windows build
- [ ] 4.4 Cross-build the Linux target from Windows
- [ ] 4.5 Confirm the case probe correctly skipped `wininc/`

## Notes / findings

- Needs a real Windows machine or VM; cannot be done from here.
- Expect problems in the `.cmd` shims first — that is the least conventional part.
