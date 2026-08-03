# T0099 — Crash handling and shipped-build diagnostics

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Low |
| **Complexity** | Moderate |
| **Phase** | 8 — Runtime & export |
| **Order** | 790 |
| **Created** | 2026-08-03 |
| **Refs** | T0054, T0055, T0083, T0084 |

## Why

When the exported game crashes on a player's machine, there is currently
nothing: no handler, no stack trace, no flushed log — just a vanished process
and an unactionable bug report. Two existing tickets make this worse in small
ways that are cheap to fix now: T0054 buffers log output for performance,
which means a crash loses the most recent lines *exactly when they matter*;
and T0055's assert policy needs a defined failure path in shipped builds
(assert → what, exactly?) or release asserts are just crashes with extra steps.

Filed against Phase 8 because it matters the moment builds leave the dev
machine, but the log-flush-on-fatal hook is worth wiring when T0054 is built.

## Done when

- [ ] A crash handler on both targets captures a stack trace and writes a
      crash report file (log tail included) somewhere a player can find it
- [ ] The logging system flushes on fatal/crash — the buffered tail is not lost
- [ ] Symbolication strategy decided and recorded: the zig toolchain emits
      DWARF for **both** targets (no PDBs under MinGW — unusual for Windows,
      and actually a simplification), so either ship symbols or keep a
      per-release symbol archive and resolve offline
- [ ] A save being written during a crash does not corrupt existing saves —
      T0083's atomic-rename already claims this; the crash test is what proves it
- [ ] A deliberate-crash path exists for testing the whole chain, including
      under wine for the Windows binary
- [ ] Release assert behaviour (T0055) routes through this rather than
      straight to abort

## Subtasks

- [ ] 99.1 Handlers: signals (`SIGSEGV`/`SIGABRT`/…) on Linux,
      `SetUnhandledExceptionFilter` on Windows — check what MinGW provides for
      stack capture (`RtlCaptureStackBackTrace` is in ntdll and available)
- [ ] 99.2 Stack capture and best-effort in-process symbolication; offline
      resolution via addr2line against archived binaries as the fallback
- [ ] 99.3 Crash report location beside save data, with rotation
- [ ] 99.4 Flush-on-fatal hook in T0054's sink design (one interface method,
      cheap to add now, awkward to add later)
- [ ] 99.5 Decide explicitly whether reports are ever uploaded anywhere —
      privacy decision, defaults to no; local file only
- [ ] 99.6 CI (T0084) archives symbols per release build so old crashes stay
      resolvable

## Notes / findings

- Keep the handler *minimal*: allocate nothing, format into a preallocated
  buffer, write, flush, re-raise. A crash handler that itself crashes is
  worse than none, and most elaborate ones do.
- Editor crashes are worth the same treatment eventually — losing an unsaved
  scene to a silent crash is the editor equivalent of losing a save — but the
  editor has a terminal and a developer attached, so the runtime ships first.
