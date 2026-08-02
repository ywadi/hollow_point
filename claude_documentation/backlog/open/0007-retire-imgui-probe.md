# T0007 — Retire `apps/imgui_probe`

| | |
|---|---|
| **Status** | ⏸ BLOCKED by [T0006](0006-define-real-application.md) |
| **Priority** | Low |
| **Created** | 2026-08-02 |

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

- [ ] A real application exists and builds for both targets (T0006)
- [ ] Whatever the probe was validating is covered elsewhere, or consciously
      dropped
- [ ] `apps/imgui_probe/` deleted, and references removed from `BUILDING.md`,
      `01-project-overview.md` and `02-decision-log.md` (D9)

## Subtasks

- [ ] 7.1 Confirm the real app exercises ImGui docking; if not, keep a smaller check
- [ ] 7.2 Delete the directory
- [ ] 7.3 Purge references from the docs
- [ ] 7.4 Rebuild both targets to confirm nothing depended on it

## Notes / findings

- **Do not retire it early.** While it is the only runnable target, it is the
  only way to catch runtime regressions in the harness at all.
- Consider keeping the `HP_PROBE_EXIT_FRAMES` idea in the real app — a
  frame-limited headless self-exit is what made automated verification possible.
