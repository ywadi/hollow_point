# T0007 — Retire `apps/imgui_probe`

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | Low |
| **Complexity** | Simple |
| **Phase** | 1 — Harden the build |
| **Created** | 2026-08-02 |
| **Closed** | 2026-08-02 |

## Why

`apps/imgui_probe` is a copy of Diligent's `Tutorial10_DataStreaming` kept only
as a build-and-run smoke test. It served its purpose: it caught G7 (removed ImGui
modifier aliases), G8 (mandatory `readme.md`), the sysroot glibc mismatch (D4)
and the RPATH-to-stubs bug (G6) — none of which a compile-only check would have
found.

It is Apache-licensed Diligent sample code carrying a `Copyright 2019-2025
Diligent Graphics LLC` header. Fine to keep while it earns its place; not
something to leave lying around indefinitely once a real app exists.

## Done when

- [x] A real application exists and builds for both targets (T0006)
- [x] Whatever the probe was validating is covered elsewhere, or consciously
      dropped
- [x] `apps/imgui_probe/` deleted, and references removed from `BUILDING.md`,
      `01-project-overview.md` and `02-decision-log.md` (D9)

## Subtasks

- [x] 7.1 Confirm the real app exercises ImGui docking; if not, keep a smaller check
- [x] 7.2 Delete the directory
- [x] 7.3 Purge references from the docs
- [x] 7.4 Rebuild both targets to confirm nothing depended on it

## Notes / findings

- **Do not retire it early.** While it is the only runnable target, it is the
  only way to catch runtime regressions in the harness at all.
- Consider keeping the `HP_PROBE_EXIT_FRAMES` idea in the real app — a
  frame-limited headless self-exit is what made automated verification possible.

### Outcome — DONE, ahead of its stated precondition

Retired on request, **before** T0006 delivered a real application — so the
"Done when" condition about coverage being replaced was not met. Recording that
plainly rather than pretending the gate was satisfied.

Removed:
- `apps/imgui_probe/` (deleted; recoverable from git history)
- references in `BUILDING.md`, `documentation/01-project-overview.md`,
  `documentation/02-decision-log.md` (D9), `documentation/05-verification-status.md`
  and the T0006 ticket

`apps/` is now empty and the root `CMakeLists.txt` glob finds nothing, so the
build produces libraries only. That is expected, not a regression.

**Consequence, flagged at the time and worth repeating:** the probe was the only
runnable target, and therefore the only way to catch a *runtime* regression in
the harness. It is what caught G7, G8, the sysroot glibc mismatch (D4) and the
RPATH-to-stubs bug (G6) — all four of which compiled cleanly and would have
shipped. The ImGui docking, Vulkan and wine evidence already recorded remains
valid, but none of it can be re-run until T0006 lands.

The historical records in `completed/0001`–`0003` still refer to the probe. Those
are evidence of what was done at the time and were deliberately left unedited.
