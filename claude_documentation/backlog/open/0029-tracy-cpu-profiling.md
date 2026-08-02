# T0029 — Tracy: vendor and CPU profiling

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | High |
| **Complexity** | Moderate |
| **Phase** | 5 — Profiling |
| **Created** | 2026-08-02 |

## Why

Profiling is going to be central to this project, and guessing at performance is
how engines end up slow in ways nobody can explain. Tracy is a frame profiler
with nanosecond-resolution CPU zones, a mature external viewer, and — critically —
GPU timing (T0030), which a hand-rolled `ScopeTimer` could never give us.

T0019 already put the macro surface in place and instrumented as code was
written, so this ticket is largely a mapping exercise rather than a retrofit.

## Done when

- [ ] Tracy vendored as a submodule at a pinned tag, cross-compiling to both
      targets
- [ ] `HP_PROFILE_*` macros map onto Tracy when profiling is enabled
- [ ] The Tracy viewer connects to a running build and shows the frame loop,
      layer updates and render submission as distinct zones
- [ ] Disabled builds contain no Tracy code at all — verified, not assumed
- [ ] Worker threads (T0026) appear named in the timeline
- [ ] `BUILDING.md` documents how to enable it and connect the viewer

## Subtasks

- [ ] 29.1 Vendor `wolfpld/tracy` at a pinned tag; confirm the client builds for
      `x86_64-windows-gnu` (see notes — this is the main risk)
- [ ] 29.2 Map `HP_PROFILE_ZONE` / `_NAMED` / `_FRAME` onto Tracy macros
- [ ] 29.3 Wire `HP_PROFILING` through the toolchain to `TRACY_ENABLE`
- [ ] 29.4 Name threads via Tracy's thread-naming API
- [ ] 29.5 Confirm a disabled build has no Tracy symbols (`nm`/`objdump`)
- [ ] 29.6 Document the workflow, including that the viewer is a separate app

## Notes / findings

**Main risk is the MinGW cross-build.** The Tracy client uses OS threading and
timing primitives; it supports MinGW, but this project's toolchain has already
turned up three separate MinGW-vs-MSVC issues (G2, G3, G4 — header case, import
library case, MSVC-style `.lib` names). Expect something similar and check early
rather than at the end.

Tracy's *client* is what we vendor and link. The *viewer* is a separate desktop
application; it does not need to be part of this build and probably should not be
(it pulls in its own GUI dependencies).

Tracy connects over TCP by default, which also means it works against the Windows
build running under wine.

Keep profiling **off by default** in Release and on in a dedicated profiling
configuration, so shipped builds carry no instrumentation.

### Architecture review (2026-08-03) — several Phase 4 tickets cannot close without this

T0045 ("culling cost visible in Tracy"), T0040 ("verified in Tracy, not
assumed") and T0050 ("threads named and visible in Tracy") all carry Done-when
conditions that require Tracy — and they are Phase 4, while this is Phase 5.
Either those verifications are deferred, or this ticket is pulled to the
*start* of Phase 4. The latter is the better plan: the renderer is exactly the
code that should be built with a profiler attached, and the T0019 macro
surface means the wiring cost is small. Flagged rather than re-phased —
owner's call.
